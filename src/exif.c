/* exif.c — 极简 JPEG / EXIF 拍摄日期读取
 *
 * 思路（只读文件头，绝不整文件读入）：
 *   1. 校验 JPEG 的 SOI 标记 FF D8；
 *   2. 逐个跳过标记段，找到 APP1（FF E1）并且以 "Exif\0\0" 开头的那一段；
 *   3. 段内是 TIFF 结构：字节序 + 0x002A + IFD0 偏移；
 *   4. 在 IFD0 里找 ExifIFD 指针（0x8769），再到 ExifIFD 里找
 *      DateTimeOriginal（0x9003）；找不到就退而求其次找
 *      DateTimeDigitized（0x9004），再找不到就用 IFD0 里的
 *      DateTime（0x0132）。
 *
 * JPEG 一个标记段的长度字段是 16 位，所以 APP1 最多 65533 字节，
 * 一次分配 64KB 缓冲即可，不需要动态增长。
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <stdio.h>
#include "exif.h"

#define APP1_MAX 65536

typedef struct {
    const BYTE *data;   /* TIFF 头起始位置 */
    DWORD       size;   /* TIFF 数据长度 */
    int         big;    /* 1 = MM（大端），0 = II（小端） */
} Tiff;

static WORD Rd16(const Tiff *t, DWORD off)
{
    if (off + 2 > t->size) return 0;
    const BYTE *p = t->data + off;
    if (t->big) return (WORD)(((WORD)p[0] << 8) | p[1]);
    return (WORD)(((WORD)p[1] << 8) | p[0]);
}

static DWORD Rd32(const Tiff *t, DWORD off)
{
    if (off + 4 > t->size) return 0;
    const BYTE *p = t->data + off;
    if (t->big)
        return ((DWORD)p[0] << 24) | ((DWORD)p[1] << 16) | ((DWORD)p[2] << 8) | p[3];
    return ((DWORD)p[3] << 24) | ((DWORD)p[2] << 16) | ((DWORD)p[1] << 8) | p[0];
}

/* 把 n 个数字字符转成整数，遇到非数字返回 FALSE */
static BOOL Digits(const char *s, int n, int *out)
{
    int v = 0;
    for (int i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return FALSE;
        v = v * 10 + (s[i] - '0');
    }
    *out = v;
    return TRUE;
}

/* 解析 "YYYY:MM:DD HH:MM:SS" 形式的日期字符串 */
static BOOL ParseDateString(const char *s, SYSTEMTIME *st)
{
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
    if (lstrlenA(s) < 19) return FALSE;
    if (s[4] != ':' || s[7] != ':' || s[10] != ' ' || s[13] != ':' || s[16] != ':')
        return FALSE;
    if (!Digits(s + 0, 4, &y) || !Digits(s + 5, 2, &mo) || !Digits(s + 8, 2, &d) ||
        !Digits(s + 11, 2, &h) || !Digits(s + 14, 2, &mi) || !Digits(s + 17, 2, &sec))
        return FALSE;
    if (y < 1900 || y > 2200 || mo < 1 || mo > 12 || d < 1 || d > 31 ||
        h > 23 || mi > 59 || sec > 59)
        return FALSE;

    ZeroMemory(st, sizeof(*st));
    st->wYear = (WORD)y;
    st->wMonth = (WORD)mo;
    st->wDay = (WORD)d;
    st->wHour = (WORD)h;
    st->wMinute = (WORD)mi;
    st->wSecond = (WORD)sec;
    return TRUE;
}

/* 从某个 IFD 条目读出 ASCII 日期串并解析 */
static BOOL ReadDateEntry(const Tiff *t, DWORD entryOff, SYSTEMTIME *st)
{
    WORD type = Rd16(t, entryOff + 2);
    DWORD count = Rd32(t, entryOff + 4);
    if (type != 2) return FALSE;              /* 只认 ASCII */
    if (count < 19 || count > 64) return FALSE;

    DWORD valOff = entryOff + 8;
    DWORD strOff;
    if (count <= 4) strOff = valOff;          /* 短字符串内联存放 */
    else strOff = Rd32(t, valOff);
    if (strOff + count > t->size) return FALSE;

    char buf[64];
    DWORD n = count < 63 ? count : 63;
    memcpy(buf, t->data + strOff, n);
    buf[n] = 0;
    return ParseDateString(buf, st);
}

/* 在 IFD 中查找指定标记，找到返回条目偏移 */
static BOOL FindTag(const Tiff *t, DWORD ifd, WORD want, DWORD *entryOff)
{
    if (ifd + 2 > t->size) return FALSE;
    WORD n = Rd16(t, ifd);
    DWORD p = ifd + 2;
    for (WORD i = 0; i < n; i++, p += 12) {
        if (p + 12 > t->size) return FALSE;
        if (Rd16(t, p) == want) { *entryOff = p; return TRUE; }
    }
    return FALSE;
}

static BOOL ParseTiff(const BYTE *tiff, DWORD size, SYSTEMTIME *st)
{
    Tiff t;
    t.data = tiff;
    t.size = size;
    t.big = 0;

    if (size < 8) return FALSE;
    if (tiff[0] == 'I' && tiff[1] == 'I') t.big = 0;
    else if (tiff[0] == 'M' && tiff[1] == 'M') t.big = 1;
    else return FALSE;
    if (Rd16(&t, 2) != 0x002A) return FALSE;

    DWORD ifd0 = Rd32(&t, 4);
    DWORD off;

    /* 1) ExifIFD 里的 DateTimeOriginal / DateTimeDigitized */
    DWORD exifIfd = 0;
    if (FindTag(&t, ifd0, 0x8769, &off)) exifIfd = Rd32(&t, off + 8);
    if (exifIfd) {
        if (FindTag(&t, exifIfd, 0x9003, &off) && ReadDateEntry(&t, off, st)) return TRUE;
        if (FindTag(&t, exifIfd, 0x9004, &off) && ReadDateEntry(&t, off, st)) return TRUE;
    }
    /* 2) IFD0 里的 DateTime（文件修改时间） */
    if (FindTag(&t, ifd0, 0x0132, &off) && ReadDateEntry(&t, off, st)) return TRUE;

    return FALSE;
}

BOOL ExifReadDateTime(const WCHAR *path, SYSTEMTIME *st)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;

    BYTE *seg = (BYTE *)malloc(APP1_MAX);
    if (!seg) { CloseHandle(h); return FALSE; }

    DWORD rd = 0;
    BOOL ok = FALSE;
    BYTE soi[2];

    if (ReadFile(h, soi, 2, &rd, NULL) && rd == 2 && soi[0] == 0xFF && soi[1] == 0xD8) {
        for (;;) {
            BYTE m[2];
            if (!ReadFile(h, m, 2, &rd, NULL) || rd != 2) break;
            if (m[0] != 0xFF) break;                     /* 标记必须以 FF 开头 */
            BYTE marker = m[1];
            if (marker == 0xFF) continue;                /* 填充字节 */
            if (marker == 0xD8) continue;                /* SOI */
            if (marker == 0xD9 || marker == 0xDA) break; /* EOI / SOS：后面没有元数据了 */
            if ((marker >= 0xD0 && marker <= 0xD7) || marker == 0x01)
                continue;                                /* 无长度字段的独立标记 */

            BYTE lb[2];
            if (!ReadFile(h, lb, 2, &rd, NULL) || rd != 2) break;
            int len = ((int)lb[0] << 8) | lb[1];
            if (len < 2) break;
            len -= 2;                                    /* 段数据长度 */

            if (marker == 0xE1 && len >= 6) {
                if (len > APP1_MAX) len = APP1_MAX;
                if (!ReadFile(h, seg, (DWORD)len, &rd, NULL)) break;
                if (rd == (DWORD)len && memcmp(seg, "Exif\0\0", 6) == 0) {
                    ok = ParseTiff(seg + 6, (DWORD)len - 6, st);
                    break;
                }
            } else {
                if (SetFilePointer(h, len, NULL, FILE_CURRENT) == INVALID_SET_FILE_POINTER)
                    break;
            }
        }
    }

    free(seg);
    CloseHandle(h);
    return ok;
}
