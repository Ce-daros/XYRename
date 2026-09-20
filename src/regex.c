/* regex.c — 极简正则表达式引擎（回溯法，纯 C，约 400 行）
 *
 * 设计说明：
 *   把表达式编译成节点链表，每个节点有一个 next 指向后面的节点。
 *   量词和分组带有子表达式（sub）。匹配时用“续延”（Continuation）
 *   的方式递归：mSeq(节点, 续延, 位置) 表示“匹配这个节点以及它后面
 *   的全部内容”。分组在续延里记下结束位置，量词通过把自己再次挂到
 *   续延上实现贪婪重复。
 *
 *   为了避免用户写出病态表达式导致程序卡死，加了步数和递归深度上限；
 *   文件名很短，正常表达式远达不到上限。
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#include "regex.h"

#define MAX_GROUPS 10          /* 0 号不用，1 ～ 9 */
#define MAX_STEPS  400000L
#define MAX_DEPTH  900
#define MAX_RANGES 32

enum {
    RN_CHAR = 0,   /* 普通字符 */
    RN_ANY,        /* . */
    RN_CLASS,      /* [...] 或 \d \w \s */
    RN_GROUP,      /* (...) */
    RN_BACKREF,    /* \1 ～ \9 */
    RN_BOL,        /* ^ */
    RN_EOL,        /* $ */
    RN_STAR,       /* * */
    RN_PLUS,       /* + */
    RN_QUES        /* ? */
};

typedef struct RRange { WCHAR lo, hi; } RRange;

typedef struct RNode {
    int           type;
    struct RNode *next;
    struct RNode *sub;          /* GROUP / 量词的子表达式 */
    WCHAR         ch;           /* CHAR */
    int           gidx;         /* GROUP / BACKREF 的组号 */
    RRange        ranges[MAX_RANGES];
    int           nranges;
    int           negate;       /* 字符类取反 */
    unsigned      special;      /* 1=数字 2=单词字符 4=空白 */
} RNode;

struct Regex {
    RNode *prog;
    int    ci;                  /* 1 = 不区分大小写 */
};

/* ─────────────────────────── 编译 ─────────────────────────── */
typedef struct {
    const WCHAR *p;
    int          ngroups;
    WCHAR       *err;
    int          errCap;
    BOOL         failed;
} Parser;

static void SetErr(Parser *ps, const WCHAR *msg)
{
    if (!ps->failed && ps->err && ps->errCap) lstrcpynW(ps->err, msg, ps->errCap);
    ps->failed = TRUE;
}

static RNode *NewNode(int type)
{
    RNode *n = (RNode *)calloc(1, sizeof(RNode));
    if (n) n->type = type;
    return n;
}

static void FreeNode(RNode *n)
{
    while (n) {
        RNode *next = n->next;
        if (n->sub) FreeNode(n->sub);
        free(n);
        n = next;
    }
}

static RNode *ParseSeq(Parser *ps);

/* 解析 [...] 字符类 */
static RNode *ParseClass(Parser *ps)
{
    ps->p++;                                   /* 跳过 [ */
    RNode *n = NewNode(RN_CLASS);
    if (!n) { SetErr(ps, L"内存不足"); return NULL; }

    if (*ps->p == L'^') { n->negate = 1; ps->p++; }

    BOOL first = TRUE;
    while (*ps->p && (*ps->p != L']' || first)) {
        first = FALSE;
        WCHAR lo;
        if (*ps->p == L'\\') {
            ps->p++;
            WCHAR e = *ps->p;
            if (e == 0) { SetErr(ps, L"方括号中的反斜杠后面缺少字符"); FreeNode(n); return NULL; }
            ps->p++;
            WCHAR lc = (WCHAR)towlower(e);
            if (lc == L'd' || lc == L'w' || lc == L's') {
                unsigned bit = (lc == L'd') ? 1u : (lc == L'w') ? 2u : 4u;
                n->special |= bit;
                if (e != lc) n->negate ^= 1;   /* \D \W \S 取反 */
                continue;
            }
            if (e == L'n') lo = L'\n';
            else if (e == L't') lo = L'\t';
            else if (e == L'r') lo = L'\r';
            else lo = e;
        } else {
            lo = *ps->p++;
        }

        if (*ps->p == L'-' && ps->p[1] && ps->p[1] != L']') {
            ps->p++;
            WCHAR hi;
            if (*ps->p == L'\\') {
                ps->p++;
                hi = *ps->p ? *ps->p++ : L'\\';
            } else {
                hi = *ps->p++;
            }
            if (hi < lo) { WCHAR t = lo; lo = hi; hi = t; }
            if (n->nranges < MAX_RANGES) {
                n->ranges[n->nranges].lo = lo;
                n->ranges[n->nranges].hi = hi;
                n->nranges++;
            }
        } else {
            if (n->nranges < MAX_RANGES) {
                n->ranges[n->nranges].lo = lo;
                n->ranges[n->nranges].hi = lo;
                n->nranges++;
            }
        }
    }
    if (*ps->p != L']') { SetErr(ps, L"方括号不配对，缺少“]”"); FreeNode(n); return NULL; }
    ps->p++;
    return n;
}

/* 解析 \ 开头的转义 */
static RNode *ParseEscape(Parser *ps)
{
    ps->p++;                                   /* 跳过 \ */
    WCHAR c = *ps->p;
    if (c == 0) { SetErr(ps, L"反斜杠“\\”后面缺少字符"); return NULL; }
    ps->p++;

    if (c >= L'1' && c <= L'9') {
        RNode *n = NewNode(RN_BACKREF);
        if (!n) { SetErr(ps, L"内存不足"); return NULL; }
        n->gidx = c - L'0';
        return n;
    }
    if (c == L'd' || c == L'D' || c == L'w' || c == L'W' || c == L's' || c == L'S') {
        RNode *n = NewNode(RN_CLASS);
        if (!n) { SetErr(ps, L"内存不足"); return NULL; }
        WCHAR lc = (WCHAR)towlower(c);
        n->special = (lc == L'd') ? 1u : (lc == L'w') ? 2u : 4u;
        if (c != lc) n->negate = 1;
        return n;
    }
    if (c == L'n') c = L'\n';
    else if (c == L't') c = L'\t';
    else if (c == L'r') c = L'\r';

    RNode *n = NewNode(RN_CHAR);
    if (!n) { SetErr(ps, L"内存不足"); return NULL; }
    n->ch = c;
    return n;
}

/* 解析一个原子，并接上可能出现的量词 */
static RNode *ParseAtom(Parser *ps)
{
    WCHAR c = *ps->p;
    RNode *n = NULL;

    if (c == L'(') {
        ps->p++;
        if (ps->ngroups >= MAX_GROUPS - 1) {
            SetErr(ps, L"括号嵌套过多（最多支持 9 个分组）");
            return NULL;
        }
        n = NewNode(RN_GROUP);
        if (!n) { SetErr(ps, L"内存不足"); return NULL; }
        n->gidx = ++ps->ngroups;
        n->sub = ParseSeq(ps);
        if (ps->failed) { FreeNode(n); return NULL; }
        if (*ps->p != L')') {
            SetErr(ps, L"括号不配对，缺少右括号“)”");
            FreeNode(n);
            return NULL;
        }
        ps->p++;
    } else if (c == L'.') {
        ps->p++;
        n = NewNode(RN_ANY);
    } else if (c == L'[') {
        n = ParseClass(ps);
    } else if (c == L'^') {
        ps->p++;
        n = NewNode(RN_BOL);
    } else if (c == L'$') {
        ps->p++;
        n = NewNode(RN_EOL);
    } else if (c == L'\\') {
        n = ParseEscape(ps);
    } else if (c == L'*' || c == L'+' || c == L'?') {
        SetErr(ps, L"量词 * + ? 前面缺少内容");
        return NULL;
    } else if (c == L')') {
        SetErr(ps, L"括号不配对，多出一个右括号“)”");
        return NULL;
    } else {
        ps->p++;
        n = NewNode(RN_CHAR);
        if (n) n->ch = c;
    }

    if (!n || ps->failed) return n;

    WCHAR q = *ps->p;
    if (q == L'*' || q == L'+' || q == L'?') {
        ps->p++;
        RNode *qn = NewNode(q == L'*' ? RN_STAR : q == L'+' ? RN_PLUS : RN_QUES);
        if (!qn) { FreeNode(n); SetErr(ps, L"内存不足"); return NULL; }
        qn->sub = n;
        n = qn;
    }
    return n;
}

/* 解析一串节点，遇到 ) 或字符串结束停下 */
static RNode *ParseSeq(Parser *ps)
{
    RNode *head = NULL, *tail = NULL;
    while (!ps->failed && *ps->p && *ps->p != L')') {
        RNode *a = ParseAtom(ps);
        if (!a) break;
        if (tail) tail->next = a;
        else head = a;
        tail = a;
    }
    return head;
}

static RNode *Compile(const WCHAR *pattern, WCHAR *err, int errCap, BOOL *failed)
{
    Parser ps;
    ZeroMemory(&ps, sizeof(ps));
    ps.p = pattern;
    ps.err = err;
    ps.errCap = errCap;
    *failed = FALSE;

    RNode *prog = ParseSeq(&ps);
    if (!ps.failed && *ps.p != 0) {
        /* ParseSeq 只在遇到 ) 或字符串结束时停下，这里剩下的必定是多出的右括号 */
        SetErr(&ps, L"括号不配对，多出一个右括号“)”");
    }
    if (ps.failed) { FreeNode(prog); *failed = TRUE; return NULL; }
    return prog;
}

/* ─────────────────────────── 匹配 ─────────────────────────── */
typedef struct {
    const WCHAR *text;
    int  gstart[MAX_GROUPS];
    int  gend[MAX_GROUPS];
    int  end;                 /* 整个匹配的结束位置 */
    int  ci;
    long steps;
    int  depth;
} Ctx;

typedef struct Cont {
    RNode       *list;        /* 接着要匹配的节点链表（可为 NULL） */
    int          gclose;      /* >0 表示匹配前先记录该分组的结束位置 */
    struct Cont *next;
} Cont;

static BOOL mSeq(RNode *n, Cont *k, Ctx *c, int pos);

static BOOL mCont(Cont *k, Ctx *c, int pos)
{
    if (!k) { c->end = pos; return TRUE; }
    int old = 0;
    if (k->gclose) { old = c->gend[k->gclose]; c->gend[k->gclose] = pos; }
    BOOL r;
    if (k->list) r = mSeq(k->list, k->next, c, pos);
    else         r = mCont(k->next, c, pos);
    if (!r && k->gclose) c->gend[k->gclose] = old;
    return r;
}

static BOOL ChEq(Ctx *c, WCHAR a, WCHAR b)
{
    if (a == b) return TRUE;
    if (!c->ci) return FALSE;
    return (WCHAR)towupper(a) == (WCHAR)towupper(b) ||
           (WCHAR)towlower(a) == (WCHAR)towlower(b);
}

static BOOL IsDigit(WCHAR ch) { return ch >= L'0' && ch <= L'9'; }
static BOOL IsWord(WCHAR ch)
{
    return IsDigit(ch) || (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
           ch == L'_' || ch >= 128;          /* 汉字等按单词字符处理，方便中文文件名 */
}
static BOOL IsSpace(WCHAR ch)
{
    return ch == L' ' || ch == L'\t' || ch == L'\n' || ch == L'\r' || ch == L'\f' || ch == L'\v';
}

static BOOL ClassMatch(const RNode *n, WCHAR ch, int ci)
{
    BOOL in = FALSE;
    if (n->special) {
        if ((n->special & 1) && IsDigit(ch)) in = TRUE;
        if ((n->special & 2) && IsWord(ch)) in = TRUE;
        if ((n->special & 4) && IsSpace(ch)) in = TRUE;
    }
    for (int i = 0; i < n->nranges && !in; i++) {
        if (ch >= n->ranges[i].lo && ch <= n->ranges[i].hi) { in = TRUE; break; }
        if (ci) {
            WCHAR u = (WCHAR)towupper(ch), l = (WCHAR)towlower(ch);
            if (u >= n->ranges[i].lo && u <= n->ranges[i].hi) in = TRUE;
            else if (l >= n->ranges[i].lo && l <= n->ranges[i].hi) in = TRUE;
        }
    }
    return n->negate ? !in : in;
}

static void SaveG(Ctx *c, int *gs, int *ge)
{
    memcpy(gs, c->gstart, sizeof(c->gstart));
    memcpy(ge, c->gend, sizeof(c->gend));
}

static void RestG(Ctx *c, const int *gs, const int *ge)
{
    memcpy(c->gstart, gs, sizeof(c->gstart));
    memcpy(c->gend, ge, sizeof(c->gend));
}

static BOOL mSeq(RNode *n, Cont *k, Ctx *c, int pos)
{
    if (c->steps++ > MAX_STEPS) return FALSE;
    if (c->depth >= MAX_DEPTH) return FALSE;
    c->depth++;

    BOOL r = FALSE;
    if (!n) {
        r = mCont(k, c, pos);
    } else {
        WCHAR ch = c->text[pos];
        switch (n->type) {
        case RN_CHAR:
            r = ChEq(c, ch, n->ch) && mSeq(n->next, k, c, pos + 1);
            break;
        case RN_ANY:
            r = (ch != 0) && mSeq(n->next, k, c, pos + 1);
            break;
        case RN_CLASS:
            r = (ch != 0) && ClassMatch(n, ch, c->ci) && mSeq(n->next, k, c, pos + 1);
            break;
        case RN_BOL:
            r = (pos == 0) && mSeq(n->next, k, c, pos);
            break;
        case RN_EOL:
            r = (ch == 0) && mSeq(n->next, k, c, pos);
            break;
        case RN_BACKREF: {
            int gs = c->gstart[n->gidx], ge = c->gend[n->gidx];
            if (gs < 0 || ge < gs) { r = FALSE; break; }
            int len = ge - gs;
            r = TRUE;
            for (int i = 0; i < len; i++) {
                if (!ChEq(c, c->text[pos + i], c->text[gs + i])) { r = FALSE; break; }
            }
            if (r) r = mSeq(n->next, k, c, pos + len);
            break;
        }
        case RN_GROUP: {
            int os = c->gstart[n->gidx], oe = c->gend[n->gidx];
            c->gstart[n->gidx] = pos;
            Cont kk;
            kk.list = n->next;
            kk.gclose = n->gidx;
            kk.next = k;
            r = mSeq(n->sub, &kk, c, pos);
            if (!r) { c->gstart[n->gidx] = os; c->gend[n->gidx] = oe; }
            break;
        }
        case RN_QUES: {
            int gs[MAX_GROUPS], ge[MAX_GROUPS];
            SaveG(c, gs, ge);
            Cont kk;
            kk.list = n->next;
            kk.gclose = 0;
            kk.next = k;
            if (mSeq(n->sub, &kk, c, pos)) {
                r = TRUE;
            } else {
                RestG(c, gs, ge);
                r = mSeq(n->next, k, c, pos);
            }
            break;
        }
        case RN_STAR: {
            int gs[MAX_GROUPS], ge[MAX_GROUPS];
            SaveG(c, gs, ge);
            Cont kk;
            kk.list = n;                 /* 匹配完一次子表达式后，回到本节点继续重复 */
            kk.gclose = 0;
            kk.next = k;
            if (mSeq(n->sub, &kk, c, pos)) {
                r = TRUE;
            } else {
                RestG(c, gs, ge);
                r = mSeq(n->next, k, c, pos);
            }
            break;
        }
        case RN_PLUS: {
            /* 至少匹配一次，之后按 STAR 处理 */
            RNode star;
            ZeroMemory(&star, sizeof(star));
            star.type = RN_STAR;
            star.sub = n->sub;
            star.next = n->next;
            int gs[MAX_GROUPS], ge[MAX_GROUPS];
            SaveG(c, gs, ge);
            Cont kk;
            kk.list = &star;
            kk.gclose = 0;
            kk.next = k;
            if (mSeq(n->sub, &kk, c, pos)) {
                r = TRUE;
            } else {
                RestG(c, gs, ge);
                r = FALSE;
            }
            break;
        }
        default:
            r = FALSE;
            break;
        }
    }

    c->depth--;
    return r;
}

/* 从 pos 开始尝试匹配整个表达式 */
static BOOL MatchAt(RNode *prog, Ctx *c, int pos)
{
    c->steps = 0;
    c->depth = 0;
    c->end = pos;
    for (int i = 0; i < MAX_GROUPS; i++) { c->gstart[i] = -1; c->gend[i] = -1; }
    return mSeq(prog, NULL, c, pos);
}

/* ─────────────────────────── 对外接口 ─────────────────────────── */
Regex *RegexCompile(const WCHAR *pattern, BOOL caseSensitive, WCHAR *err, int errCap)
{
    if (err && errCap) err[0] = 0;
    BOOL failed = FALSE;
    RNode *prog = Compile(pattern, err, errCap, &failed);
    if (failed) return NULL;

    Regex *re = (Regex *)calloc(1, sizeof(Regex));
    if (!re) {
        FreeNode(prog);
        if (err && errCap) lstrcpynW(err, L"内存不足", errCap);
        return NULL;
    }
    re->prog = prog;
    re->ci = !caseSensitive;
    return re;
}

void RegexFree(Regex *re)
{
    if (!re) return;
    FreeNode(re->prog);
    free(re);
}

BOOL RegexIsValid(const WCHAR *pattern, WCHAR *err, int errCap)
{
    WCHAR local[256];
    if (!err) { err = local; errCap = 256; }
    Regex *re = RegexCompile(pattern, TRUE, err, errCap);
    if (!re) return FALSE;
    RegexFree(re);
    return TRUE;
}

BOOL RegexReplaceWith(Regex *re, const WCHAR *src, const WCHAR *repl,
                      WCHAR *out, int outCap)
{
    if (!re || outCap <= 0) return FALSE;

    Ctx c;
    ZeroMemory(&c, sizeof(c));
    c.text = src;
    c.ci = re->ci;

    int oi = 0, pos = 0;
    out[0] = 0;
    while (src[pos]) {
        if (MatchAt(re->prog, &c, pos) && c.end > pos) {
            for (const WCHAR *r = repl; *r; r++) {
                if (*r == L'\\' && r[1] >= L'1' && r[1] <= L'9') {
                    int g = r[1] - L'0';
                    if (g < MAX_GROUPS && c.gstart[g] >= 0 && c.gend[g] > c.gstart[g]) {
                        for (int i = c.gstart[g]; i < c.gend[g]; i++)
                            if (oi < outCap - 1) out[oi++] = src[i];
                    }
                    r++;
                } else if (*r == L'\\' && r[1]) {
                    if (oi < outCap - 1) out[oi++] = r[1];
                    r++;
                } else {
                    if (oi < outCap - 1) out[oi++] = *r;
                }
            }
            pos = c.end;
        } else {
            if (oi < outCap - 1) out[oi++] = src[pos];
            pos++;
        }
    }
    out[oi] = 0;
    return TRUE;
}
