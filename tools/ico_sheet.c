/* ico_sheet.c — 生成 .ico 对照表（48px 大图 + 真实资源编号标签，支持分页）
   用法: ico_sheet <目录> <输出.bmp> [起始序号] [每页个数]
   文件名形如 ICON141_1.ico 时，标签显示 141（真实资源编号） */
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>

static int NameToId(const wchar_t *name)
{
    /* 取文件名中的第一段数字，例如 ICON141_1.ico -> 141 */
    const wchar_t *p = name;
    while (*p && (*p < L'0' || *p > L'9')) p++;
    return *p ? _wtoi(p) : -1;
}

int wmain(int argc, wchar_t **argv)
{
    int n = 0;
    wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &n);
    if (wargv && n >= 3) { argv = wargv; argc = n; }
    else { wprintf(L"usage: ico_sheet <dir> <out.bmp> [start] [count]\n"); return 1; }

    int start = (argc > 3) ? _wtoi(argv[3]) : 0;
    int perPage = (argc > 4) ? _wtoi(argv[4]) : 63;
    int iconSz = (argc > 5) ? _wtoi(argv[5]) : 48;
    if (iconSz < 8) iconSz = 48;

    WIN32_FIND_DATAW fd;
    wchar_t pattern[MAX_PATH];
    swprintf(pattern, MAX_PATH, L"%ls\\*.ico", argv[1]);

    static wchar_t names[1024][MAX_PATH];
    int count = 0;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) { wprintf(L"no ico found, gle=%lu\n", GetLastError()); return 1; }
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            lstrcpyW(names[count++], fd.cFileName);
    } while (FindNextFileW(h, &fd) && count < 1024);
    FindClose(h);

    int cols = 9, cellW = 72, cellH = iconSz + 60;
    int pageCount = (count - start + perPage - 1) / perPage;
    if (pageCount < 1) pageCount = 1;

    for (int page = 0; page < pageCount; page++) {
        int from = start + page * perPage;
        int to = from + perPage; if (to > count) to = count;
        int items = to - from;
        if (items <= 0) break;
        int rows = (items + cols - 1) / cols;
        int W = cols * cellW, H = rows * cellH;

        BITMAPINFO bi = {0};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = W;
        bi.bmiHeader.biHeight = -H;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 24;
        bi.bmiHeader.biCompression = BI_RGB;
        void *bits = NULL;
        HDC screen = GetDC(NULL);
        HDC hdc = CreateCompatibleDC(screen);
        HBITMAP hbmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        SelectObject(hdc, hbmp);
        RECT rc = {0, 0, W, H};
        FillRect(hdc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));

        HFONT font = CreateFontW(-16, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                                 0, 0, 0, 0, L"Tahoma");
        HGDIOBJ oldFont = SelectObject(hdc, font);
        SetBkMode(hdc, TRANSPARENT);

        for (int i = from; i < to; i++) {
            int k = i - from;
            int x = (k % cols) * cellW, y = (k / cols) * cellH;
            wchar_t path[MAX_PATH];
            swprintf(path, MAX_PATH, L"%ls\\%ls", argv[1], names[i]);
            HICON ic = (HICON)LoadImageW(NULL, path, IMAGE_ICON, iconSz, iconSz, LR_LOADFROMFILE);
            if (ic) {
                DrawIconEx(hdc, x + (cellW - iconSz) / 2, y + 4, ic, iconSz, iconSz, 0, NULL, DI_NORMAL);
                DestroyIcon(ic);
            }
            wchar_t label[32];
            int id = NameToId(names[i]);
            if (id >= 0) swprintf(label, 32, L"ID %d", id);
            else lstrcpynW(label, names[i], 31);
            SetTextColor(hdc, RGB(0, 0, 0));
            TextOutW(hdc, x + 8, y + 60, label, lstrlenW(label));
            SetTextColor(hdc, RGB(120, 120, 120));
            wchar_t seq[16];
            swprintf(seq, 16, L"#%d", i);
            TextOutW(hdc, x + 8, y + 80, seq, lstrlenW(seq));
        }

        SelectObject(hdc, oldFont);
        DeleteObject(font);

        wchar_t out[MAX_PATH];
        if (pageCount > 1) swprintf(out, MAX_PATH, L"%ls_%d.bmp", argv[2], page + 1);
        else lstrcpynW(out, argv[2], MAX_PATH);
        FILE *f = _wfopen(out, L"wb");
        if (f) {
            DWORD rowBytes = ((W * 3 + 3) / 4) * 4;
            BITMAPFILEHEADER bfh = {0};
            bfh.bfType = 0x4D42;
            bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
            bfh.bfSize = bfh.bfOffBits + rowBytes * H;
            fwrite(&bfh, sizeof(bfh), 1, f);
            fwrite(&bi.bmiHeader, sizeof(BITMAPINFOHEADER), 1, f);
            for (int r = 0; r < H; r++)
                fwrite((BYTE *)bits + (DWORD)r * rowBytes, 1, rowBytes, f);
            fclose(f);
            wprintf(L"page %d: items %d-%d -> %ls\n", page + 1, from, to - 1, out);
        }
        DeleteObject(hbmp);
        DeleteDC(hdc);
        ReleaseDC(NULL, screen);
    }
    return 0;
}
