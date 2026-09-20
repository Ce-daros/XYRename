/* help.c — 帮助窗口
 * 模仿 Windows HTML Help（CHM）阅读器的界面：
 *   顶部工具栏（后退 / 前进 / 主页 / 打印 / 隐藏目录）
 *   左侧“目录 / 索引”标签页 + 树形目录
 *   右侧内容区（RichEdit 显示 RTF 专题）
 * 帮助正文以标记文本形式内嵌在 topics.c 中，显示时即时转换为 RTF，
 * 因此不需要额外的 CHM 文件，也不依赖 hhctrl.ocx。
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <richedit.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "resource.h"
#include "app.h"
#include "topics.h"

static HWND g_hHelp, g_hHTB, g_hTabs, g_hTree, g_hIdxEdit, g_hIdxList, g_hRich;
static HFONT g_hHFont;
static int  g_panel = 1;
static HIMAGELIST g_hTreeImg;

/* 浏览历史 */
static int g_hist[128];
static int g_histPos = -1;
static int g_histCount = 0;
static int g_navigating = 0;   /* 防止树选中与导航相互递归 */

/* ─────────────────────────── RTF 生成 ─────────────────────────── */
typedef struct {
    char  *buf;
    size_t len, cap;
} RtfBuf;

static void RtfRaw(RtfBuf *b, const char *s)
{
    size_t n = strlen(s);
    if (b->len + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 4096;
        while (nc < b->len + n + 1) nc *= 2;
        b->buf = (char *)realloc(b->buf, nc);
        b->cap = nc;
    }
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = 0;
}

static void RtfEsc(RtfBuf *b, const WCHAR *s)
{
    char tmp[16];
    for (; *s; s++) {
        WCHAR c = *s;
        if (c == L'\\') RtfRaw(b, "\\\\");
        else if (c == L'{') RtfRaw(b, "\\{");
        else if (c == L'}') RtfRaw(b, "\\}");
        else if (c >= 32 && c < 127) { tmp[0] = (char)c; tmp[1] = 0; RtfRaw(b, tmp); }
        else { sprintf(tmp, "\\u%d?", (int)c); RtfRaw(b, tmp); }
    }
}

static void RtfHeader(RtfBuf *b)
{
    RtfRaw(b, "{\\rtf1\\ansi\\ansicpg936\\deff0"
              "{\\fonttbl{\\f0\\fnil\\fcharset134 SimHei;}{\\f1\\fnil\\fcharset134 SimHei;}}"
              "{\\colortbl ;\\red0\\green0\\blue128;\\red160\\green0\\blue0;"
              "\\red0\\green0\\blue255;\\red90\\green90\\blue90;}"
              "\\viewkind4\\uc1\\pard\\f0\\fs18\\cf0 ");
}

/* 把一篇专题正文转成 RTF */
static char *TopicToRtf(const HelpTopic *t)
{
    RtfBuf b;
    ZeroMemory(&b, sizeof(b));
    RtfHeader(&b);

    /* 标题 */
    RtfRaw(&b, "\\pard\\sb60\\sa160\\f1\\fs24\\b\\cf1 ");
    RtfEsc(&b, t->title);
    RtfRaw(&b, "\\b0\\cf0\\par ");

    const WCHAR *p = t->text;
    while (*p) {
        /* 取出一行 */
        WCHAR line[2048];
        int n = 0;
        while (*p && *p != L'\n' && n < 2047) line[n++] = *p++;
        line[n] = 0;
        if (*p == L'\n') p++;

        if (n == 0) { RtfRaw(&b, "\\pard\\sa60\\par "); continue; }

        WCHAR kind = line[0];
        const WCHAR *body = line;
        if (kind == L'#' || kind == L'*' || kind == L'-' || kind == L'!' ||
            kind == L'>' || kind == L'|') {
            body = line + 1;
            if (*body == L' ') body++;
        } else {
            kind = 0;
        }

        switch (kind) {
        case L'#':
            RtfRaw(&b, "\\pard\\sb160\\sa80\\f1\\fs20\\b\\cf1 ");
            RtfEsc(&b, body);
            RtfRaw(&b, "\\b0\\cf0\\par ");
            break;
        case L'*':
            RtfRaw(&b, "\\pard\\sb120\\sa60\\b\\cf0 ");
            RtfEsc(&b, body);
            RtfRaw(&b, "\\b0\\par ");
            break;
        case L'-':
            RtfRaw(&b, "\\pard\\li420\\fi-180\\sa60 ");
            RtfRaw(&b, "\\u183? ");
            RtfEsc(&b, body);
            RtfRaw(&b, "\\par ");
            break;
        case L'!':
            RtfRaw(&b, "\\pard\\li360\\sa120\\cf2\\b ");
            RtfEsc(&b, body);
            RtfRaw(&b, "\\b0\\cf0\\par ");
            break;
        case L'>':
            RtfRaw(&b, "\\pard\\sb120\\cf3 ");
            RtfEsc(&b, body);
            RtfRaw(&b, "\\cf0\\par ");
            break;
        case L'|':
            RtfRaw(&b, "\\pard\\li420\\sa40 ");
            RtfEsc(&b, body);
            RtfRaw(&b, "\\par ");
            break;
        default:
            RtfRaw(&b, "\\pard\\sa120 ");
            RtfEsc(&b, body);
            RtfRaw(&b, "\\par ");
            break;
        }
    }
    RtfRaw(&b, "}");
    return b.buf;
}

/* ─────────────────────────── 显示专题 ─────────────────────────── */
typedef struct { const char *p; size_t len, pos; } StreamCookie;

static DWORD CALLBACK RtfStreamIn(DWORD_PTR cookie, LPBYTE pb, LONG cb, LONG *pcb)
{
    StreamCookie *sc = (StreamCookie *)cookie;
    size_t left = sc->len - sc->pos;
    if (left > (size_t)cb) left = (size_t)cb;
    memcpy(pb, sc->p + sc->pos, left);
    sc->pos += left;
    *pcb = (LONG)left;
    return 0;
}

static void SetRichText(const char *rtf)
{
    StreamCookie sc;
    sc.p = rtf;
    sc.len = strlen(rtf);
    sc.pos = 0;

    EDITSTREAM es;
    es.dwCookie = (DWORD_PTR)&sc;
    es.dwError = 0;
    es.pfnCallback = RtfStreamIn;

    SendMessageW(g_hRich, EM_STREAMIN, SF_RTF, (LPARAM)&es);
    SendMessageW(g_hRich, EM_SETSEL, 0, 0);
    SendMessageW(g_hRich, EM_SCROLLCARET, 0, 0);
}

/* 选中树中的某个专题（不触发导航递归） */
static HTREEITEM FindTreeItem(HTREEITEM parent, int topic, int *found)
{
    HTREEITEM it = TreeView_GetChild(g_hTree, parent);
    while (it) {
        TVITEMW tv;
        ZeroMemory(&tv, sizeof(tv));
        tv.mask = TVIF_PARAM | TVIF_HANDLE;
        tv.hItem = it;
        TreeView_GetItem(g_hTree, &tv);
        if (tv.lParam == topic) { *found = 1; return it; }
        HTREEITEM r = FindTreeItem(it, topic, found);
        if (*found) return r;
        it = TreeView_GetNextSibling(g_hTree, it);
    }
    return NULL;
}

static void UpdateNavButtons(void)
{
    SendMessageW(g_hHTB, TB_ENABLEBUTTON, IDH_BACK, MAKELONG(g_histPos > 0, 0));
    SendMessageW(g_hHTB, TB_ENABLEBUTTON, IDH_FWD, MAKELONG(g_histPos < g_histCount - 1, 0));
}

static void NavigateTo(int topic, BOOL pushHistory)
{
    if (topic < 0 || topic >= g_topicCount) return;
    if (g_navigating) return;
    g_navigating = 1;

    if (pushHistory) {
        /* 丢掉当前位置之后的历史 */
        g_histCount = g_histPos + 1;
        if (g_histCount < 128) {
            g_hist[g_histCount++] = topic;
            g_histPos = g_histCount - 1;
        }
    }

    char *rtf = TopicToRtf(&g_topics[topic]);
    if (rtf) { SetRichText(rtf); free(rtf); }

    WCHAR title[512];
    swprintf(title, 512, L"%ls - %ls", g_topics[topic].title, APP_NAME);
    SetWindowTextW(g_hHelp, title);

    /* 同步树选中项 */
    int found = 0;
    HTREEITEM it = FindTreeItem(TVI_ROOT, topic, &found);
    if (found && it) {
        SendMessageW(g_hTree, WM_SETREDRAW, FALSE, 0);
        TreeView_SelectItem(g_hTree, it);
        TreeView_EnsureVisible(g_hTree, it);
        SendMessageW(g_hTree, WM_SETREDRAW, TRUE, 0);
    }
    UpdateNavButtons();
    g_navigating = 0;
}

/* ─────────────────────────── 索引 ─────────────────────────── */
static void RebuildIndex(void)
{
    WCHAR filter[128];
    GetWindowTextW(g_hIdxEdit, filter, 128);

    SendMessageW(g_hIdxList, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_hIdxList, LB_RESETCONTENT, 0, 0);

    for (int i = 0; i < g_topicCount; i++) {
        if (filter[0]) {
            /* 简单包含判断（不区分大小写） */
            if (!StrStrIW(g_topics[i].title, filter)) continue;
        }
        int idx = (int)SendMessageW(g_hIdxList, LB_ADDSTRING, 0, (LPARAM)g_topics[i].title);
        SendMessageW(g_hIdxList, LB_SETITEMDATA, idx, (LPARAM)i);
    }
    SendMessageW(g_hIdxList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hIdxList, NULL, TRUE);
}

/* ─────────────────────────── 打印 ─────────────────────────── */
static void DoPrint(void)
{
    PRINTDLGW pd;
    ZeroMemory(&pd, sizeof(pd));
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = g_hHelp;
    pd.Flags = PD_RETURNDC | PD_USEDEVMODECOPIESANDCOLLATE | PD_NOSELECTION | PD_NOPAGENUMS;

    if (!PrintDlgW(&pd)) return;
    if (!pd.hDC) return;

    DOCINFOW di;
    ZeroMemory(&di, sizeof(di));
    di.cbSize = sizeof(di);
    di.lpszDocName = L"小雨超级文件批量改名专家 使用帮助";
    if (StartDocW(pd.hDC, &di) <= 0) { DeleteDC(pd.hDC); return; }

    int dpiX = GetDeviceCaps(pd.hDC, LOGPIXELSX);
    int dpiY = GetDeviceCaps(pd.hDC, LOGPIXELSY);
    int pageW = GetDeviceCaps(pd.hDC, PHYSICALWIDTH);
    int pageH = GetDeviceCaps(pd.hDC, PHYSICALHEIGHT);
    int offX = GetDeviceCaps(pd.hDC, PHYSICALOFFSETX);
    int offY = GetDeviceCaps(pd.hDC, PHYSICALOFFSETY);
    int margin = dpiX;   /* 1 英寸页边距 */

    FORMATRANGE fr;
    fr.hdc = pd.hDC;
    fr.hdcTarget = pd.hDC;
    fr.rcPage.left = 0;
    fr.rcPage.top = 0;
    fr.rcPage.right = pageW;
    fr.rcPage.bottom = pageH;
    fr.rc.left = margin;
    fr.rc.top = margin;
    fr.rc.right = pageW - margin;
    fr.rc.bottom = pageH - margin;

    LONG textLen = (LONG)SendMessageW(g_hRich, WM_GETTEXTLENGTH, 0, 0);
    LONG start = 0;
    int pages = 0;

    while (start < textLen && pages < 200) {
        if (StartPage(pd.hDC) <= 0) break;
        fr.chrg.cpMin = start;
        fr.chrg.cpMax = -1;
        LONG end = (LONG)SendMessageW(g_hRich, EM_FORMATRANGE, TRUE, (LPARAM)&fr);
        if (EndPage(pd.hDC) <= 0) break;
        pages++;
        if (end <= start) break;
        start = end;
    }

    SendMessageW(g_hRich, EM_FORMATRANGE, FALSE, 0);
    EndDoc(pd.hDC);
    DeleteDC(pd.hDC);
    (void)offX; (void)offY; (void)dpiY;

    if (pages == 0)
        MessageBoxW(g_hHelp, L"打印时出现问题，未能输出任何页面。", APP_NAME, MB_OK | MB_ICONWARNING);
}

/* ─────────────────────────── 界面 ─────────────────────────── */
static void LayoutHelp(void)
{
    RECT rc;
    GetClientRect(g_hHelp, &rc);
    int W = rc.right, H = rc.bottom;

    SendMessageW(g_hHTB, TB_AUTOSIZE, 0, 0);
    RECT rt;
    GetWindowRect(g_hHTB, &rt);
    int top = rt.bottom - rt.top;
    int pad = S(4);

    int panelW = g_panel ? S(215) : 0;

    if (g_panel) {
        SetWindowPos(g_hTabs, NULL, pad, top + pad, panelW, H - top - pad * 2, SWP_NOZORDER);
        RECT rd;
        GetClientRect(g_hTabs, &rd);
        SendMessageW(g_hTabs, TCM_ADJUSTRECT, FALSE, (LPARAM)&rd);
        int tw = rd.right - rd.left, th = rd.bottom - rd.top;
        SetWindowPos(g_hTree, NULL, rd.left, rd.top, tw, th, SWP_NOZORDER);
        SetWindowPos(g_hIdxEdit, NULL, rd.left, rd.top, tw, S(22), SWP_NOZORDER);
        SetWindowPos(g_hIdxList, NULL, rd.left, rd.top + S(24), tw, th - S(24), SWP_NOZORDER);
        ShowWindow(g_hTabs, SW_SHOW);
    } else {
        ShowWindow(g_hTabs, SW_HIDE);
        ShowWindow(g_hTree, SW_HIDE);
        ShowWindow(g_hIdxEdit, SW_HIDE);
        ShowWindow(g_hIdxList, SW_HIDE);
    }

    SetWindowPos(g_hRich, NULL, pad + panelW + (g_panel ? pad : 0), top + pad,
                 W - panelW - pad * 2 - (g_panel ? pad : 0), H - top - pad * 2, SWP_NOZORDER);
}

static void ShowPanelTab(int which)
{
    if (!g_panel) return;
    ShowWindow(g_hTree, which == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hIdxEdit, which == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hIdxList, which == 1 ? SW_SHOW : SW_HIDE);
}

static void BuildTree(void)
{
    TreeView_DeleteAllItems(g_hTree);
    HTREEITEM hRoot = NULL, hChapter = NULL;

    for (int i = 0; i < g_nodeCount; i++) {
        TVINSERTSTRUCTW is;
        ZeroMemory(&is, sizeof(is));
        is.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
        is.item.pszText = (LPWSTR)g_nodes[i].title;
        is.item.lParam = g_nodes[i].topic;
        int isTopic = (g_nodes[i].topic >= 0);
        is.item.iImage = isTopic ? 2 : 0;
        is.item.iSelectedImage = isTopic ? 2 : 1;

        if (g_nodes[i].level == 0) {
            is.hParent = TVI_ROOT;
            is.hInsertAfter = TVI_LAST;
            hRoot = TreeView_InsertItem(g_hTree, &is);
        } else if (g_nodes[i].level == 1) {
            is.hParent = hRoot;
            is.hInsertAfter = TVI_LAST;
            hChapter = TreeView_InsertItem(g_hTree, &is);
        } else {
            is.hParent = hChapter;
            is.hInsertAfter = TVI_LAST;
            TreeView_InsertItem(g_hTree, &is);
        }
    }
    if (hRoot) TreeView_Expand(g_hTree, hRoot, TVE_EXPAND);
}

static void CreateHelpToolbar(HWND h)
{
    g_hHTB = CreateWindowExW(0, TOOLBARCLASSNAMEW, NULL,
                             WS_CHILD | WS_VISIBLE | TBSTYLE_TOOLTIPS |
                             CCS_NODIVIDER | CCS_TOP,
                             0, 0, 0, 0, h, (HMENU)IDH_TOOLBAR, g_hInst, NULL);
    SendMessageW(g_hHTB, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);

    HIMAGELIST himl = ImageList_Create(S(16), S(16), ILC_COLOR32 | ILC_MASK, 5, 1);
    ImageList_SetBkColor(himl, CLR_NONE);
    struct { int id, icon; const WCHAR *text; } btns[] = {
        { IDH_BACK,  IDI_HP_BACK,  L"后退" },
        { IDH_FWD,   IDI_HP_FWD,   L"前进" },
        { IDH_HOME,  IDI_HP_HOME,  L"主页" },
        { IDH_PRINT, IDI_HP_PRINT, L"打印" },
        { IDH_PANEL, IDI_HP_PANEL, L"显示/隐藏目录" },
    };
    for (int i = 0; i < 5; i++) {
        HICON ic = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(btns[i].icon),
                                     IMAGE_ICON, S(16), S(16), LR_DEFAULTCOLOR);
        ImageList_AddIcon(himl, ic);
        DestroyIcon(ic);
    }
    SendMessageW(g_hHTB, TB_SETIMAGELIST, 0, (LPARAM)himl);

    for (int i = 0; i < 5; i++) {
        TBBUTTON tbb;
        ZeroMemory(&tbb, sizeof(tbb));
        tbb.iBitmap = i;
        tbb.idCommand = btns[i].id;
        tbb.fsState = TBSTATE_ENABLED;
        tbb.fsStyle = TBSTYLE_BUTTON;
        tbb.iString = (INT_PTR)SendMessageW(g_hHTB, TB_ADDSTRINGW, 0, (LPARAM)btns[i].text);
        SendMessageW(g_hHTB, TB_INSERTBUTTONW, i, (LPARAM)&tbb);
    }
    SendMessageW(g_hHTB, TB_AUTOSIZE, 0, 0);
    SendMessageW(g_hHTB, TB_ENABLEBUTTON, IDH_BACK, MAKELONG(FALSE, 0));
    SendMessageW(g_hHTB, TB_ENABLEBUTTON, IDH_FWD, MAKELONG(FALSE, 0));
}

static LRESULT CALLBACK HelpProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE: {
        g_hHFont = UiFontCreate(9, FALSE);
        ApplyClassicLook(h);

        CreateHelpToolbar(h);

        g_hTabs = CreateWindowExW(0, WC_TABCONTROLW, NULL, WS_CHILD | WS_VISIBLE | TCS_TABS,
                                  0, 0, 10, 10, h, (HMENU)IDH_TABS, g_hInst, NULL);
        SendMessageW(g_hTabs, WM_SETFONT, (WPARAM)g_hHFont, TRUE);
        TCITEMW ti;
        ZeroMemory(&ti, sizeof(ti));
        ti.mask = TCIF_TEXT;
        ti.pszText = L"目录";
        SendMessageW(g_hTabs, TCM_INSERTITEMW, 0, (LPARAM)&ti);
        ti.pszText = L"索引";
        SendMessageW(g_hTabs, TCM_INSERTITEMW, 1, (LPARAM)&ti);

        g_hTree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, NULL,
                                  WS_CHILD | WS_VISIBLE | TVS_HASBUTTONS | TVS_HASLINES |
                                  TVS_LINESATROOT | TVS_SHOWSELALWAYS,
                                  0, 0, 10, 10, g_hTabs, (HMENU)IDH_TREE, g_hInst, NULL);
        SendMessageW(g_hTree, WM_SETFONT, (WPARAM)g_hHFont, TRUE);
        g_hTreeImg = ImageList_Create(S(16), S(16), ILC_COLOR32 | ILC_MASK, 3, 1);
        ImageList_SetBkColor(g_hTreeImg, CLR_NONE);
        int treeIcons[3] = { IDI_TREE_CLOSED, IDI_TREE_OPEN, IDI_TREE_PAGE };
        for (int i = 0; i < 3; i++) {
            HICON ic = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(treeIcons[i]),
                                         IMAGE_ICON, S(16), S(16), LR_DEFAULTCOLOR);
            ImageList_AddIcon(g_hTreeImg, ic);
            DestroyIcon(ic);
        }
        TreeView_SetImageList(g_hTree, g_hTreeImg, TVSIL_NORMAL);

        g_hIdxEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                     WS_CHILD | ES_AUTOHSCROLL,
                                     0, 0, 10, 10, g_hTabs, (HMENU)IDH_INDEX_EDIT, g_hInst, NULL);
        SendMessageW(g_hIdxEdit, WM_SETFONT, (WPARAM)g_hHFont, TRUE);
        g_hIdxList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                     WS_CHILD | WS_VSCROLL | LBS_NOTIFY,
                                     0, 0, 10, 10, g_hTabs, (HMENU)IDH_INDEX_LIST, g_hInst, NULL);
        SendMessageW(g_hIdxList, WM_SETFONT, (WPARAM)g_hHFont, TRUE);

        g_hRich = CreateWindowExW(WS_EX_CLIENTEDGE, L"RichEdit20W", L"",
                                  WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY |
                                  WS_VSCROLL | ES_AUTOVSCROLL,
                                  0, 0, 10, 10, h, (HMENU)IDH_RICH, g_hInst, NULL);
        SendMessageW(g_hRich, EM_SETBKGNDCOLOR, 0, (LPARAM)GetSysColor(COLOR_WINDOW));
        SendMessageW(g_hRich, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELONG(S(8), S(8)));

        BuildTree();
        RebuildIndex();
        LayoutHelp();
        ApplyClassicLook(g_hHTB);
        ApplyClassicLook(g_hTabs);
        ApplyClassicLook(g_hTree);
        ApplyClassicLook(g_hIdxList);
        ApplyClassicLook(g_hRich);
        NavigateTo(0, TRUE);
        return 0;
    }

    case WM_SIZE:
        LayoutHelp();
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(w), code = HIWORD(w);
        switch (id) {
        case IDH_BACK:
            if (g_histPos > 0) { g_histPos--; NavigateTo(g_hist[g_histPos], FALSE); }
            return 0;
        case IDH_FWD:
            if (g_histPos < g_histCount - 1) { g_histPos++; NavigateTo(g_hist[g_histPos], FALSE); }
            return 0;
        case IDH_HOME:
            NavigateTo(0, TRUE);
            return 0;
        case IDH_PRINT:
            DoPrint();
            return 0;
        case IDH_PANEL:
            g_panel = !g_panel;
            SendMessageW(g_hHTB, TB_PRESSBUTTON, IDH_PANEL, MAKELONG(g_panel ? TRUE : FALSE, 0));
            LayoutHelp();
            if (g_panel) {
                int sel = (int)SendMessageW(g_hTabs, TCM_GETCURSEL, 0, 0);
                ShowPanelTab(sel < 0 ? 0 : sel);
            }
            return 0;
        case IDH_INDEX_EDIT:
            if (code == EN_CHANGE) RebuildIndex();
            return 0;
        case IDH_INDEX_LIST:
            if (code == LBN_DBLCLK) {
                int sel = (int)SendMessageW(g_hIdxList, LB_GETCURSEL, 0, 0);
                if (sel != LB_ERR) {
                    int topic = (int)SendMessageW(g_hIdxList, LB_GETITEMDATA, sel, 0);
                    if (topic >= 0) NavigateTo(topic, TRUE);
                }
            }
            return 0;
        }
        return 0;
    }

    case WM_NOTIFY: {
        LPNMHDR nh = (LPNMHDR)l;
        if (nh->idFrom == IDH_TABS && nh->code == TCN_SELCHANGE) {
            int sel = (int)SendMessageW(g_hTabs, TCM_GETCURSEL, 0, 0);
            ShowPanelTab(sel);
            return 0;
        }
        if (nh->idFrom == IDH_TREE) {
            if (nh->code == TVN_SELCHANGEDW) {
                LPNMTREEVIEWW nv = (LPNMTREEVIEWW)l;
                int topic = (int)nv->itemNew.lParam;
                if (topic >= 0) NavigateTo(topic, TRUE);
                return 0;
            }
            if (nh->code == TVN_ITEMEXPANDEDW) {
                LPNMTREEVIEWW nv = (LPNMTREEVIEWW)l;
                TVITEMW tv;
                ZeroMemory(&tv, sizeof(tv));
                tv.mask = TVIF_HANDLE | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
                tv.hItem = nv->itemNew.hItem;
                tv.iImage = (nv->action == TVE_EXPAND) ? 1 : 0;
                tv.iSelectedImage = tv.iImage;
                TreeView_SetItem(g_hTree, &tv);
                return 0;
            }
        }
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(h);
        return 0;

    case WM_DESTROY:
        if (g_hHFont) DeleteObject(g_hHFont);
        if (g_hTreeImg) ImageList_Destroy(g_hTreeImg);
        g_hHelp = NULL;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void ShowHelpWindow(HWND owner)
{
    static int registered = 0;
    if (g_hHelp) {
        SetForegroundWindow(g_hHelp);
        return;
    }

    /* RichEdit 2.0 动态加载：XP 及以上系统均自带 riched20.dll */
    static int richedLoaded = 0;
    if (!richedLoaded) {
        if (!LoadLibraryW(L"riched20.dll")) {
            MessageBoxW(owner, L"无法加载 riched20.dll，帮助窗口无法显示。\n\n"
                               L"该文件是 Windows 系统自带组件，若确实缺失，"
                               L"请从系统安装盘恢复后再试。",
                        APP_NAME, MB_OK | MB_ICONERROR);
            return;
        }
        richedLoaded = 1;
    }

    if (!registered) {
        WNDCLASSEXW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = HelpProc;
        wc.hInstance = g_hInst;
        wc.hIcon = LoadIconW(g_hInst, MAKEINTRESOURCEW(IDI_HELP));
        wc.hIconSm = LoadIconW(g_hInst, MAKEINTRESOURCEW(IDI_HELP));
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"XYHelpWnd";
        if (!RegisterClassExW(&wc)) return;
        registered = 1;
    }

    g_panel = 1;
    g_histPos = -1;
    g_histCount = 0;

    int W = S(760), H = S(560);
    g_hHelp = CreateWindowExW(0, L"XYHelpWnd", APP_NAME L" 帮助",
                              WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, W, H,
                              owner, NULL, g_hInst, NULL);
    if (!g_hHelp) return;

    RECT rc, ro;
    GetWindowRect(g_hHelp, &rc);
    if (owner) {
        GetWindowRect(owner, &ro);
        SetWindowPos(g_hHelp, NULL,
                     ro.left + ((ro.right - ro.left) - (rc.right - rc.left)) / 2,
                     ro.top + ((ro.bottom - ro.top) - (rc.bottom - rc.top)) / 2,
                     0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    ShowWindow(g_hHelp, SW_SHOW);
    UpdateWindow(g_hHelp);
    SetForegroundWindow(g_hHelp);
}
