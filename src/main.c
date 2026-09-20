/* main.c — 小雨超级文件批量改名专家 V1.0 正式版
 * 主程序：界面、文件列表、改名规则、预览、执行、撤销、参数记忆
 * 纯 Win32 SDK + C，未使用 MFC
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
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "resource.h"
#include "app.h"

/* ─────────────────────────── 全局变量 ─────────────────────────── */
HINSTANCE g_hInst;
static HWND g_hMain, g_hTB, g_hStatus, g_hList;
static HWND g_hGrpMode, g_hGrpParam;
static HWND g_hRadMode[MODE_COUNT];
static HWND g_hLblP1, g_hEdtP1, g_hLblStart, g_hEdtStart, g_hLblWidth, g_hCmbWidth;
static HWND g_hLblPrefix, g_hEdtPrefix, g_hLblSuffix, g_hEdtSuffix;
static HWND g_hLblFind, g_hEdtFind, g_hLblRepl, g_hEdtRepl, g_hChkCase;
static HWND g_hLblExt, g_hEdtExt, g_hLblExtWarn;
static HWND g_hRadUpper, g_hRadLower, g_hChkExtCase;
static HFONT g_hFont;
static int   g_mode = MODE_NUMBER;
static FileItem *g_items = NULL;
static int   g_count = 0, g_cap = 0;
static int   g_sortCol = -1, g_sortAsc = 1;
static WCHAR g_exeDir[MAX_PATH];
static HWND  g_hProg = NULL;
static int   g_cancel = 0;
static HACCEL g_hAccel = NULL;

/* 列表列宽（96dpi 基准） */
static const int g_colW[COL_COUNT] = { 200, 200, 60, 80, 120 };
static const WCHAR *g_colName[COL_COUNT] = { L"原文件名", L"新文件名", L"扩展名", L"大小", L"修改日期" };

/* 改名方式名称 */
static const WCHAR *g_modeName[MODE_COUNT] = {
    L"统一编号", L"添加前后缀", L"查找替换", L"修改扩展名", L"全部替换", L"大小写转换"
};

/* ─────────────────────────── 小工具 ─────────────────────────── */
/* 不做任何 DPI 缩放：一律按 96dpi 像素布局，由系统整体拉伸。
   在 150% 等高缩放屏幕上即得到经典的“老程序”观感（略微发糊）。 */
int S(int v) { return v; }

/* 经典界面字体：宋体 9pt（12px），不平滑，走字体自带的点阵 */
HFONT UiFontCreate(int pt, BOOL bold)
{
    HFONT f = CreateFontW(-MulDiv(pt, 96, 72), 0, 0, 0,
                          bold ? FW_BOLD : FW_NORMAL, 0, 0, 0,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          NONANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"宋体");
    if (!f) f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    return f;
}

/* 让窗口/控件尽量走经典（非主题）绘制，并去掉 Win11 的圆角。
   uxtheme.dll / dwmapi.dll 都动态加载，XP 上没有也不影响运行。 */
void ApplyClassicLook(HWND h)
{
    if (!h) return;
    HMODULE ux = LoadLibraryW(L"uxtheme.dll");
    if (ux) {
        typedef HRESULT (WINAPI *PFNSETWINDOWTHEME)(HWND, LPCWSTR, LPCWSTR);
        PFNSETWINDOWTHEME p = (PFNSETWINDOWTHEME)GetProcAddress(ux, "SetWindowTheme");
        if (p) p(h, L"", L"");
    }
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (dwm) {
        typedef HRESULT (WINAPI *PFNDWMSET)(HWND, DWORD, LPCVOID, DWORD);
        PFNDWMSET p = (PFNDWMSET)GetProcAddress(dwm, "DwmSetWindowAttribute");
        if (p) {
            int pref = 1;   /* DWMWCP_DONOTROUND */
            p(h, 33, &pref, sizeof(pref));   /* DWMWA_WINDOW_CORNER_PREFERENCE */
        }
    }
}

static void SetCtlFont(HWND h)
{
    if (h && g_hFont) SendMessageW(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
}

static void StatusSet(int part, const WCHAR *text)
{
    SendMessageW(g_hStatus, SB_SETTEXTW, (WPARAM)part, (LPARAM)text);
}

static void CenterOnOwner(HWND hWnd, HWND hOwner)
{
    RECT rc, ro;
    GetWindowRect(hWnd, &rc);
    if (!hOwner || !IsWindow(hOwner)) {
        int w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN);
        SetWindowPos(hWnd, NULL, (w - (rc.right - rc.left)) / 2,
                     (h - (rc.bottom - rc.top)) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        return;
    }
    GetWindowRect(hOwner, &ro);
    SetWindowPos(hWnd, NULL,
                 ro.left + ((ro.right - ro.left) - (rc.right - rc.left)) / 2,
                 ro.top + ((ro.bottom - ro.top) - (rc.bottom - rc.top)) / 2,
                 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

/* 取出程序所在目录（不含末尾反斜杠） */
static void GetExeDir(WCHAR *out)
{
    GetModuleFileNameW(NULL, out, MAX_PATH);
    WCHAR *p = wcsrchr(out, L'\\');
    if (p) *p = 0;
}

static BOOL HasIllegalChar(const WCHAR *name)
{
    static const WCHAR bad[] = L"\\/:*?\"<>|";
    const WCHAR *p;
    for (p = name; *p; p++)
        if (wcschr(bad, *p)) return TRUE;
    return FALSE;
}

/* 拆分路径为 目录 / 主名 / 扩展名 */
static void SplitPath3(const WCHAR *path, WCHAR *dir, WCHAR *base, WCHAR *ext)
{
    WCHAR tmp[MAX_PATH];
    lstrcpynW(tmp, path, MAX_PATH);
    WCHAR *p = wcsrchr(tmp, L'\\');
    if (p) {
        *p = 0;
        lstrcpynW(dir, tmp, MAX_PATH);
        lstrcpynW(base, p + 1, MAX_PATH);
    } else {
        dir[0] = 0;
        lstrcpynW(base, tmp, MAX_PATH);
    }
    WCHAR *dot = wcsrchr(base, L'.');
    if (dot && dot != base) {
        lstrcpynW(ext, dot + 1, 64);
        *dot = 0;
    } else {
        ext[0] = 0;
    }
}

/* 大小写不敏感比较（用于重名判断） */
static int NameCmpI(const WCHAR *a, const WCHAR *b)
{
    return CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, a, -1, b, -1) - CSTR_EQUAL;
}

/* 全局替换（可选区分大小写） */
static void ReplaceAll(const WCHAR *src, const WCHAR *find, const WCHAR *repl,
                       BOOL caseSensitive, WCHAR *out, int outCap)
{
    int flen = lstrlenW(find);
    int oi = 0;
    out[0] = 0;
    if (flen == 0) { lstrcpynW(out, src, outCap); return; }

    for (int i = 0; src[i]; ) {
        BOOL match = FALSE;
        if (caseSensitive) {
            match = (wcsncmp(src + i, find, flen) == 0);
        } else {
            match = (CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE,
                                    src + i, flen, find, flen) == CSTR_EQUAL);
        }
        if (match) {
            int rlen = lstrlenW(repl);
            if (oi + rlen < outCap - 1) { lstrcpynW(out + oi, repl, outCap - oi); oi += rlen; }
            i += flen;
        } else {
            if (oi < outCap - 1) out[oi++] = src[i];
            i++;
        }
    }
    out[oi] = 0;
}

/* ─────────────────────────── 文件列表管理 ─────────────────────────── */
static void FreeItems(void)
{
    free(g_items);
    g_items = NULL;
    g_count = g_cap = 0;
}

static BOOL ListHasPath(const WCHAR *path)
{
    for (int i = 0; i < g_count; i++)
        if (NameCmpI(g_items[i].path, path) == 0) return TRUE;
    return FALSE;
}

static BOOL AddFileItem(const WCHAR *path)
{
    if (ListHasPath(path)) return FALSE;

    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fad)) return FALSE;
    if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return FALSE;

    if (g_count >= g_cap) {
        int ncap = g_cap ? g_cap * 2 : 256;
        FileItem *ni = (FileItem *)realloc(g_items, sizeof(FileItem) * ncap);
        if (!ni) return FALSE;
        g_items = ni;
        g_cap = ncap;
    }
    FileItem *it = &g_items[g_count];
    ZeroMemory(it, sizeof(*it));
    lstrcpynW(it->path, path, MAX_PATH);
    SplitPath3(path, it->dir, it->base, it->ext);
    it->size = ((ULONGLONG)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
    it->mtime = fad.ftLastWriteTime;
    it->status = 0;
    lstrcpynW(it->newname, it->base, MAX_PATH);
    g_count++;
    return TRUE;
}

/* 把目录下的文件（不含子目录）加入列表 */
static int AddDirectoryItems(const WCHAR *dir)
{
    WCHAR pat[MAX_PATH];
    WIN32_FIND_DATAW fd;
    int added = 0;
    swprintf(pat, MAX_PATH, L"%ls\\*", dir);
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        WCHAR full[MAX_PATH];
        swprintf(full, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName);
        if (AddFileItem(full)) added++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return added;
}

/* ─────────────────────────── 预览计算 ─────────────────────────── */
static int GetEditInt(HWND h, int def)
{
    WCHAR buf[64];
    GetWindowTextW(h, buf, 64);
    if (!buf[0]) return def;
    return _wtoi(buf);
}

/* 预览用的重名排序比较：先目录后新文件名（均忽略大小写） */
static int __cdecl DupCompare(const void *a, const void *b)
{
    int ia = *(const int *)a, ib = *(const int *)b;
    int r = NameCmpI(g_items[ia].dir, g_items[ib].dir);
    if (r) return r;
    return NameCmpI(g_items[ia].newname, g_items[ib].newname);
}

static void ComputePreview(void)
{
    WCHAR p1[512] = L"", prefix[512] = L"", suffix[512] = L"";
    WCHAR find[512] = L"", repl[512] = L"", newext[128] = L"";
    int start = 1, width = 3;
    BOOL caseSensitive = FALSE, extCase = FALSE, toUpper = TRUE;

    GetWindowTextW(g_hEdtP1, p1, 512);
    GetWindowTextW(g_hEdtPrefix, prefix, 512);
    GetWindowTextW(g_hEdtSuffix, suffix, 512);
    GetWindowTextW(g_hEdtFind, find, 512);
    GetWindowTextW(g_hEdtRepl, repl, 512);
    GetWindowTextW(g_hEdtExt, newext, 128);
    start = GetEditInt(g_hEdtStart, 1);
    width = GetEditInt(g_hCmbWidth, 3);
    if (width < 1) width = 1;
    if (width > 8) width = 8;
    caseSensitive = (SendMessageW(g_hChkCase, BM_GETCHECK, 0, 0) == BST_CHECKED);
    extCase = (SendMessageW(g_hChkExtCase, BM_GETCHECK, 0, 0) == BST_CHECKED);
    toUpper = (SendMessageW(g_hRadUpper, BM_GETCHECK, 0, 0) == BST_CHECKED);

    /* 去掉扩展名输入里可能带的点号 */
    WCHAR *ne = newext;
    while (*ne == L'.') ne++;
    if (ne != newext) lstrcpynW(newext, ne, 128);

    int idx = 0;
    for (int i = 0; i < g_count; i++) {
        FileItem *it = &g_items[i];
        WCHAR nm[MAX_PATH] = L"";
        WCHAR newExt[64];
        lstrcpynW(newExt, it->ext, 64);

        switch (g_mode) {
        case MODE_NUMBER:
        case MODE_ALL: {
            WCHAR num[64];
            swprintf(num, 64, L"%0*d", width, start + idx);
            if (p1[0]) swprintf(nm, MAX_PATH, L"%ls_%ls", p1, num);
            else       swprintf(nm, MAX_PATH, L"%ls", num);
            break;
        }
        case MODE_AFFIX:
            swprintf(nm, MAX_PATH, L"%ls%ls%ls", prefix, it->base, suffix);
            break;
        case MODE_REPLACE:
            ReplaceAll(it->base, find, repl, caseSensitive, nm, MAX_PATH);
            break;
        case MODE_EXT:
            lstrcpynW(nm, it->base, MAX_PATH);
            lstrcpynW(newExt, newext, 64);
            break;
        case MODE_CASE: {
            lstrcpynW(nm, it->base, MAX_PATH);
            if (toUpper) CharUpperW(nm); else CharLowerW(nm);
            if (extCase) { if (toUpper) CharUpperW(newExt); else CharLowerW(newExt); }
            break;
        }
        }
        idx++;

        if (nm[0] == 0) {
            lstrcpynW(it->newname, L"", MAX_PATH);
        } else if (newExt[0]) {
            swprintf(it->newname, MAX_PATH, L"%ls.%ls", nm, newExt);
        } else {
            lstrcpynW(it->newname, nm, MAX_PATH);
        }

        /* 扩展名是否变化（用于深红色提示） */
        it->status = 0;
        if (g_mode == MODE_EXT && NameCmpI(newExt, it->ext) != 0) it->status = 2;
        if (g_mode == MODE_CASE && extCase && NameCmpI(newExt, it->ext) != 0) it->status = 2;

        /* 非法字符 / 空名 */
        if (it->newname[0] == 0 || HasIllegalChar(it->newname) || HasIllegalChar(nm))
            it->status = 1;
    }

    /* 重名检查（同一目录内，忽略大小写）
       先按“目录 + 新文件名”排序，再比较相邻项，避免大量文件时成平方级耗时 */
    if (g_count > 1) {
        int *idx = (int *)malloc(sizeof(int) * g_count);
        if (idx) {
            for (int i = 0; i < g_count; i++) idx[i] = i;
            qsort(idx, g_count, sizeof(int), DupCompare);
            for (int i = 1; i < g_count; i++) {
                int a = idx[i - 1], b = idx[i];
                if (NameCmpI(g_items[a].dir, g_items[b].dir) == 0 &&
                    NameCmpI(g_items[a].newname, g_items[b].newname) == 0) {
                    g_items[a].status = 1;
                    g_items[b].status = 1;
                }
            }
            free(idx);
        }
    }

    /* 与磁盘上已有文件冲突（且该文件不在列表中） */
    for (int i = 0; i < g_count; i++) {
        if (g_items[i].status == 1) continue;
        WCHAR target[MAX_PATH];
        swprintf(target, MAX_PATH, L"%ls\\%ls", g_items[i].dir, g_items[i].newname);
        if (NameCmpI(target, g_items[i].path) == 0) continue;  /* 没变化 */
        if (GetFileAttributesW(target) != INVALID_FILE_ATTRIBUTES && !ListHasPath(target))
            g_items[i].status = 1;
    }
}

/* ─────────────────────────── 列表显示 ─────────────────────────── */
static void FormatSize(ULONGLONG sz, WCHAR *out)
{
    if (sz < 1024) swprintf(out, 32, L"%u 字节", (unsigned)sz);
    else if (sz < 1024ULL * 1024) swprintf(out, 32, L"%.1f KB", (double)sz / 1024.0);
    else swprintf(out, 32, L"%.2f MB", (double)sz / 1048576.0);
}

static void FormatTime(const FILETIME *ft, WCHAR *out)
{
    FILETIME lt;
    SYSTEMTIME st;
    FileTimeToLocalFileTime(ft, &lt);
    FileTimeToSystemTime(&lt, &st);
    swprintf(out, 32, L"%04d-%02d-%02d %02d:%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
}

static void OrigName(const FileItem *it, WCHAR *out)
{
    if (it->ext[0]) swprintf(out, MAX_PATH, L"%ls.%ls", it->base, it->ext);
    else lstrcpynW(out, it->base, MAX_PATH);
}

static void RefreshList(void)
{
    SendMessageW(g_hList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_hList);

    for (int i = 0; i < g_count; i++) {
        FileItem *it = &g_items[i];
        WCHAR orig[MAX_PATH], size[32], time[32];
        OrigName(it, orig);
        FormatSize(it->size, size);
        FormatTime(&it->mtime, time);

        LVITEMW lvi;
        ZeroMemory(&lvi, sizeof(lvi));
        lvi.mask = LVIF_TEXT;
        lvi.iItem = i;
        lvi.pszText = orig;
        int row = ListView_InsertItem(g_hList, &lvi);
        ListView_SetItemText(g_hList, row, COL_NEW, it->newname);
        ListView_SetItemText(g_hList, row, COL_EXT, it->ext);
        ListView_SetItemText(g_hList, row, COL_SIZE, size);
        ListView_SetItemText(g_hList, row, COL_TIME, time);
    }
    SendMessageW(g_hList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hList, NULL, TRUE);

    WCHAR buf[128];
    swprintf(buf, 128, L"文件数：%d", g_count);
    StatusSet(1, buf);
    swprintf(buf, 128, L"改名方式：%ls", g_modeName[g_mode]);
    StatusSet(2, buf);
}

static void UpdatePreviewAndList(void)
{
    ComputePreview();
    RefreshList();
}

/* ─────────────────────────── 排序 ─────────────────────────── */
static int g_cmpCol, g_cmpAsc;
static int __cdecl ItemCompare(const void *a, const void *b)
{
    const FileItem *x = (const FileItem *)a, *y = (const FileItem *)b;
    int r = 0;
    WCHAR na[MAX_PATH], nb[MAX_PATH];
    switch (g_cmpCol) {
    case COL_ORIG: OrigName(x, na); OrigName(y, nb); r = NameCmpI(na, nb); break;
    case COL_NEW:  r = NameCmpI(x->newname, y->newname); break;
    case COL_EXT:  r = NameCmpI(x->ext, y->ext); break;
    case COL_SIZE: r = (x->size < y->size) ? -1 : (x->size > y->size) ? 1 : 0; break;
    case COL_TIME: r = CompareFileTime(&x->mtime, &y->mtime); break;
    }
    return g_cmpAsc ? r : -r;
}

static void SortByColumn(int col)
{
    if (g_sortCol == col) g_sortAsc = !g_sortAsc;
    else { g_sortCol = col; g_sortAsc = 1; }
    g_cmpCol = col;
    g_cmpAsc = g_sortAsc;
    qsort(g_items, g_count, sizeof(FileItem), ItemCompare);
    RefreshList();
}

/* ─────────────────────────── 添加文件 / 目录 ─────────────────────────── */
static void DoAddFiles(void)
{
    static WCHAR buf[65536];
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hMain;
    ofn.lpstrFilter = L"所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 65536;
    ofn.lpstrTitle = L"请选择要改名的文件（可多选）";
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    buf[0] = 0;

    if (!GetOpenFileNameW(&ofn)) return;

    int added = 0;
    WCHAR *p = buf;
    WCHAR dir[MAX_PATH];
    lstrcpynW(dir, p, MAX_PATH);
    p += lstrlenW(p) + 1;

    if (*p == 0) {
        /* 只选了一个文件，buf 里就是完整路径 */
        if (AddFileItem(dir)) added = 1;
    } else {
        while (*p) {
            WCHAR full[MAX_PATH];
            swprintf(full, MAX_PATH, L"%ls\\%ls", dir, p);
            if (AddFileItem(full)) added++;
            p += lstrlenW(p) + 1;
        }
    }

    UpdatePreviewAndList();
    if (added == 0) MessageBoxW(g_hMain, L"没有添加任何文件（可能文件已在列表中）。",
                                L"小雨超级文件批量改名专家", MB_OK | MB_ICONINFORMATION);
}

static void DoAddDir(void)
{
    BROWSEINFOW bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = g_hMain;
    bi.lpszTitle = L"请选择要导入的文件夹";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_DONTGOBELOWDOMAIN;

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    WCHAR dir[MAX_PATH];
    if (SHGetPathFromIDListW(pidl, dir)) {
        int added = AddDirectoryItems(dir);
        UpdatePreviewAndList();
        if (added == 0)
            MessageBoxW(g_hMain, L"该文件夹中没有找到文件。", L"小雨超级文件批量改名专家",
                        MB_OK | MB_ICONINFORMATION);
    }
    CoTaskMemFree(pidl);
}

/* ─────────────────────────── 移除 / 清空 / 全选 ─────────────────────────── */
static void DoRemoveSelected(void)
{
    int sel = ListView_GetSelectedCount(g_hList);
    if (sel == 0) { MessageBoxW(g_hMain, L"请先在列表中选择要移除的文件。", L"小雨超级文件批量改名专家",
                                MB_OK | MB_ICONINFORMATION); return; }

    int w = 0;
    for (int i = 0; i < g_count; i++) {
        if (ListView_GetItemState(g_hList, i, LVIS_SELECTED) & LVIS_SELECTED) continue;
        if (w != i) g_items[w] = g_items[i];
        w++;
    }
    g_count = w;
    UpdatePreviewAndList();
}

static void DoClearAll(void)
{
    if (g_count == 0) return;
    if (MessageBoxW(g_hMain, L"确定要清空文件列表吗？", L"小雨超级文件批量改名专家",
                    MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    g_count = 0;
    UpdatePreviewAndList();
}

/* ─────────────────────────── 进度对话框 ─────────────────────────── */
static LRESULT CALLBACK ProgProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_COMMAND:
        if (LOWORD(w) == IDC_PROG_BTN) g_cancel = 1;
        return 0;
    case WM_CLOSE:
        g_cancel = 1;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static HWND CreateProgress(const WCHAR *title, int max)
{
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = ProgProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"XYProgWnd";
    RegisterClassW(&wc);

    int W = S(340), H = S(120);
    HWND h = CreateWindowExW(WS_EX_DLGMODALFRAME, L"XYProgWnd", title,
                             WS_POPUP | WS_CAPTION | WS_SYSMENU,
                             CW_USEDEFAULT, CW_USEDEFAULT, W, H,
                             g_hMain, NULL, g_hInst, NULL);
    if (!h) return NULL;

    HWND txt = CreateWindowExW(0, L"STATIC", L"正在准备…", WS_CHILD | WS_VISIBLE | SS_LEFT,
                               S(14), S(12), W - S(28), S(18), h, (HMENU)IDC_PROG_TEXT, g_hInst, NULL);
    HWND bar = CreateWindowExW(0, PROGRESS_CLASSW, NULL, WS_CHILD | WS_VISIBLE,
                               S(14), S(38), W - S(28), S(18), h, (HMENU)IDC_PROGRESS, g_hInst, NULL);
    HWND btn = CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                               W - S(94), S(68), S(80), S(24), h, (HMENU)IDC_PROG_BTN, g_hInst, NULL);
    SetCtlFont(txt); SetCtlFont(btn);

    SendMessageW(bar, PBM_SETRANGE32, 0, max);
    SendMessageW(bar, PBM_SETPOS, 0, 0);

    CenterOnOwner(h, g_hMain);
    EnableWindow(g_hMain, FALSE);
    ShowWindow(h, SW_SHOW);
    UpdateWindow(h);
    g_hProg = h;
    g_cancel = 0;
    return h;
}

static void PumpMessages(void)
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) { PostQuitMessage((int)msg.wParam); return; }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static void DestroyProgress(void)
{
    if (g_hProg) {
        DestroyWindow(g_hProg);
        g_hProg = NULL;
    }
    EnableWindow(g_hMain, TRUE);
    SetForegroundWindow(g_hMain);
}

/* ─────────────────────────── 执行改名 ─────────────────────────── */
static void UndoPath(WCHAR *out) { swprintf(out, MAX_PATH, L"%ls\\undo.dat", g_exeDir); }

static BOOL WriteUndoRecord(const WCHAR *newPath, const WCHAR *oldPath, FILE *f)
{
    if (!f) return FALSE;
    fwrite(newPath, sizeof(WCHAR), wcslen(newPath), f);
    fwrite(L"|", sizeof(WCHAR), 1, f);
    fwrite(oldPath, sizeof(WCHAR), wcslen(oldPath), f);
    fwrite(L"\r\n", sizeof(WCHAR), 2, f);
    return TRUE;
}

static void ErrorText(DWORD err, WCHAR *out, int cap)
{
    switch (err) {
    case ERROR_ACCESS_DENIED:        lstrcpynW(out, L"拒绝访问（文件只读或没有权限）", cap); return;
    case ERROR_SHARING_VIOLATION:    lstrcpynW(out, L"文件正在被其它程序使用", cap); return;
    case ERROR_ALREADY_EXISTS:       lstrcpynW(out, L"目标文件已存在", cap); return;
    case ERROR_FILE_NOT_FOUND:       lstrcpynW(out, L"找不到文件", cap); return;
    case ERROR_PATH_NOT_FOUND:       lstrcpynW(out, L"找不到路径", cap); return;
    case ERROR_INVALID_NAME:         lstrcpynW(out, L"文件名无效", cap); return;
    default: {
        WCHAR *msg = NULL;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err,
                       MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&msg, 0, NULL);
        if (msg) {
            int n = lstrlenW(msg);
            while (n > 0 && (msg[n - 1] == L'\r' || msg[n - 1] == L'\n')) msg[--n] = 0;
            lstrcpynW(out, msg, cap);
            LocalFree(msg);
        } else {
            swprintf(out, cap, L"错误码 %lu", err);
        }
    }
    }
}

static void DoRename(void)
{
    if (g_count == 0) {
        MessageBoxW(g_hMain, L"文件列表是空的，请先添加要改名的文件。",
                    L"小雨超级文件批量改名专家", MB_OK | MB_ICONINFORMATION);
        return;
    }
    ComputePreview();

    int bad = 0;
    for (int i = 0; i < g_count; i++) if (g_items[i].status == 1) bad++;
    if (bad > 0) {
        WCHAR msg[256];
        swprintf(msg, 256, L"列表中有 %d 个文件名存在问题（显示为红色），\n"
                           L"可能是重名或含有非法字符 \\ / : * ? \" < > | 。\n\n"
                           L"请先修正后再执行改名。", bad);
        MessageBoxW(g_hMain, msg, L"小雨超级文件批量改名专家", MB_OK | MB_ICONWARNING);
        return;
    }

    WCHAR msg[256];
    swprintf(msg, 256, L"即将对 %d 个文件执行改名操作。\n\n改名前请确认预览结果无误，"
                       L"改名后可使用“撤销上次改名”恢复。\n\n确定继续吗？", g_count);
    if (MessageBoxW(g_hMain, msg, L"小雨超级文件批量改名专家",
                    MB_YESNO | MB_ICONQUESTION) != IDYES) return;

    CreateProgress(L"正在改名…", g_count);

    WCHAR up[MAX_PATH];
    UndoPath(up);
    FILE *f = _wfopen(up, L"wb");
    if (f) { unsigned short bom = 0xFEFF; fwrite(&bom, 2, 1, f); }

    int ok = 0, fail = 0, cancel = 0;
    WCHAR failList[4096];
    failList[0] = 0;

    for (int i = 0; i < g_count; i++) {
        FileItem *it = &g_items[i];
        if (g_cancel) { cancel = 1; break; }

        WCHAR oldName[MAX_PATH], oldPath[MAX_PATH], newPath[MAX_PATH];
        OrigName(it, oldName);
        swprintf(oldPath, MAX_PATH, L"%ls\\%ls", it->dir, oldName);
        swprintf(newPath, MAX_PATH, L"%ls\\%ls", it->dir, it->newname);

        WCHAR line[256];
        swprintf(line, 256, L"正在处理：%ls", it->newname);
        SetWindowTextW(GetDlgItem(g_hProg, IDC_PROG_TEXT), line);
        SendMessageW(GetDlgItem(g_hProg, IDC_PROGRESS), PBM_SETPOS, i, 0);

        if (NameCmpI(oldPath, newPath) == 0) { ok++; continue; }  /* 名字没变，跳过 */

        if (MoveFileW(oldPath, newPath)) {
            ok++;
            WriteUndoRecord(newPath, oldPath, f);
            WCHAR nb[MAX_PATH], ne[64];
            SplitPath3(newPath, it->dir, nb, ne);
            lstrcpynW(it->base, nb, MAX_PATH);
            lstrcpynW(it->ext, ne, 64);
            lstrcpynW(it->path, newPath, MAX_PATH);
            lstrcpynW(it->newname, it->newname, MAX_PATH);
        } else {
            DWORD err = GetLastError();
            WCHAR reason[256];
            ErrorText(err, reason, 256);
            fail++;
            if (lstrlenW(failList) < 3000) {
                WCHAR one[512];
                swprintf(one, 512, L"　%ls —— %ls\n", oldName, reason);
                lstrcatW(failList, one);
            }
        }
        PumpMessages();
    }
    if (f) fclose(f);

    SendMessageW(GetDlgItem(g_hProg, IDC_PROGRESS), PBM_SETPOS, g_count, 0);
    PumpMessages();
    DestroyProgress();

    if (ok == 0 && fail == 0 && !cancel) {
        DeleteFileW(up);
    }
    if (cancel && ok == 0) DeleteFileW(up);

    UpdatePreviewAndList();

    WCHAR summary[5120];
    if (fail == 0) {
        swprintf(summary, 5120, L"改名完成！\n\n成功：%d 个\n失败：0 个\n%s",
                 ok, cancel ? L"\n（操作已被用户取消）" : L"");
    } else {
        swprintf(summary, 5120, L"改名完成。\n\n成功：%d 个\n失败：%d 个\n\n"
                                L"失败的文件及原因：\n%s", ok, fail, failList);
    }
    MessageBoxW(g_hMain, summary, L"小雨超级文件批量改名专家",
                MB_OK | (fail ? MB_ICONWARNING : MB_ICONINFORMATION));
}

/* ─────────────────────────── 撤销上次改名 ─────────────────────────── */
static void DoUndo(void)
{
    WCHAR up[MAX_PATH];
    UndoPath(up);
    HANDLE hf = CreateFileW(up, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) {
        MessageBoxW(g_hMain, L"没有找到可以撤销的改名记录。\n\n"
                             L"（撤销记录文件 undo.dat 位于本程序所在目录，"
                             L"只有成功执行过改名后才会生成。）",
                    L"小雨超级文件批量改名专家", MB_OK | MB_ICONINFORMATION);
        return;
    }
    DWORD size = GetFileSize(hf, NULL);
    if (size < 4) { CloseHandle(hf); DeleteFileW(up);
        MessageBoxW(g_hMain, L"没有找到可以撤销的改名记录。", L"小雨超级文件批量改名专家",
                    MB_OK | MB_ICONINFORMATION); return; }

    WCHAR *buf = (WCHAR *)malloc(size + 4);
    if (!buf) { CloseHandle(hf); return; }
    DWORD read = 0;
    ReadFile(hf, buf, size, &read, NULL);
    CloseHandle(hf);
    int nchars = (int)(read / sizeof(WCHAR));
    buf[nchars] = 0;
    /* 跳过 BOM */
    WCHAR *p = buf;
    if (nchars > 0 && p[0] == 0xFEFF) p++;

    /* 统计行数 */
    int lines = 0;
    for (WCHAR *q = p; *q; q++) if (*q == L'\n') lines++;
    if (lines == 0) {
        free(buf);
        MessageBoxW(g_hMain, L"撤销记录是空的。", L"小雨超级文件批量改名专家",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }

    WCHAR msg[256];
    swprintf(msg, 256, L"将要恢复 %d 个文件的原文件名。\n\n确定继续吗？", lines);
    if (MessageBoxW(g_hMain, msg, L"小雨超级文件批量改名专家",
                    MB_YESNO | MB_ICONQUESTION) != IDYES) { free(buf); return; }

    CreateProgress(L"正在撤销…", lines);

    int ok = 0, fail = 0;
    WCHAR *line = p;
    while (*line) {
        WCHAR *eol = line;
        while (*eol && *eol != L'\r' && *eol != L'\n') eol++;
        WCHAR save = *eol;
        *eol = 0;
        WCHAR *bar = wcschr(line, L'|');
        if (bar) {
            *bar = 0;
            const WCHAR *newPath = line;
            const WCHAR *oldPath = bar + 1;
            if (MoveFileW(newPath, oldPath)) ok++; else fail++;
        }
        *eol = save;
        line = eol;
        while (*line == L'\r' || *line == L'\n') line++;
        SendMessageW(GetDlgItem(g_hProg, IDC_PROGRESS), PBM_SETPOS, ok + fail, 0);
        PumpMessages();
        if (g_cancel) break;
    }
    DestroyProgress();
    free(buf);
    DeleteFileW(up);

    /* 列表里的文件名已经过时，清空以免误操作 */
    g_count = 0;
    UpdatePreviewAndList();

    WCHAR summary[512];
    swprintf(summary, 512, L"撤销完成。\n\n成功恢复：%d 个\n失败：%d 个\n\n"
                           L"文件列表已清空，如需再次改名请重新添加文件。", ok, fail);
    MessageBoxW(g_hMain, summary, L"小雨超级文件批量改名专家",
                MB_OK | (fail ? MB_ICONWARNING : MB_ICONINFORMATION));
}

/* ─────────────────────────── 界面：创建控件 ─────────────────────────── */
static HWND MakeCtl(const WCHAR *cls, const WCHAR *text, DWORD style, DWORD ex,
                    int id, HWND parent)
{
    HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style,
                             0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, g_hInst, NULL);
    SetCtlFont(h);
    return h;
}

static void CreateMenuBar(HWND h)
{
    HMENU mb = CreateMenu();
    HMENU m;

    m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDM_FILE_ADDFILES, L"添加文件(&F)...\tCtrl+A");
    AppendMenuW(m, MF_STRING, IDM_FILE_ADDDIR,   L"添加目录(&D)...\tCtrl+D");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_FILE_EXIT,     L"退出(&X)\tAlt+F4");
    AppendMenuW(mb, MF_POPUP, (UINT_PTR)m, L"文件(&F)");

    m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDM_EDIT_SELALL, L"全选(&A)\tCtrl+L");
    AppendMenuW(m, MF_STRING, IDM_EDIT_REMOVE, L"移除所选(&R)\tDel");
    AppendMenuW(m, MF_STRING, IDM_EDIT_CLEAR,  L"清空列表(&C)");
    AppendMenuW(mb, MF_POPUP, (UINT_PTR)m, L"编辑(&E)");

    m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDM_RULE_START,   L"开始改名(&S)\tF5");
    AppendMenuW(m, MF_STRING, IDM_RULE_UNDO,    L"撤销上次改名(&U)\tCtrl+Z");
    AppendMenuW(m, MF_STRING, IDM_RULE_REFRESH, L"刷新预览(&R)\tF3");
    AppendMenuW(mb, MF_POPUP, (UINT_PTR)m, L"规则(&R)");

    m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDM_HELP_HELP,  L"使用帮助(&H)\tF1");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_HELP_ABOUT, L"关于 小雨超级文件批量改名专家(&A)...");
    AppendMenuW(mb, MF_POPUP, (UINT_PTR)m, L"帮助(&H)");

    SetMenu(h, mb);
}

static void CreateToolbar(HWND h)
{
    g_hTB = CreateWindowExW(0, TOOLBARCLASSNAMEW, NULL,
                            WS_CHILD | WS_VISIBLE | TBSTYLE_TOOLTIPS |
                            CCS_NODIVIDER | CCS_TOP,
                            0, 0, 0, 0, h, (HMENU)IDC_TOOLBAR, g_hInst, NULL);
    SendMessageW(g_hTB, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);

    HIMAGELIST himl = ImageList_Create(S(16), S(16), ILC_COLOR32 | ILC_MASK, 4, 1);
    ImageList_SetBkColor(himl, CLR_NONE);

    struct { int id, icon; const WCHAR *text; } btns[] = {
        { IDT_ADDFILES, IDI_TB_ADDFILE, L"添加文件（Ctrl+A）" },
        { IDT_ADDDIR,   IDI_TB_ADDDIR,  L"添加目录（Ctrl+D）" },
        { IDT_REMOVE,   IDI_TB_REMOVE,  L"移除所选（Del）" },
        { IDT_START,    IDI_TB_START,   L"开始改名（F5）" },
    };
    for (int i = 0; i < 4; i++) {
        HICON ic = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(btns[i].icon),
                                     IMAGE_ICON, S(16), S(16), LR_DEFAULTCOLOR);
        ImageList_AddIcon(himl, ic);
        DestroyIcon(ic);
    }
    SendMessageW(g_hTB, TB_SETIMAGELIST, 0, (LPARAM)himl);

    for (int i = 0; i < 4; i++) {
        TBBUTTON tbb;
        ZeroMemory(&tbb, sizeof(tbb));
        if (i == 3) {
            /* 在“开始改名”之前加一条分隔线 */
            ZeroMemory(&tbb, sizeof(tbb));
            tbb.fsStyle = TBSTYLE_SEP;
            tbb.iBitmap = 8;
            SendMessageW(g_hTB, TB_INSERTBUTTONW, i, (LPARAM)&tbb);
        }
        ZeroMemory(&tbb, sizeof(tbb));
        tbb.iBitmap = i;
        tbb.idCommand = btns[i].id;
        tbb.fsState = TBSTATE_ENABLED;
        tbb.fsStyle = TBSTYLE_BUTTON;
        tbb.iString = (INT_PTR)SendMessageW(g_hTB, TB_ADDSTRINGW, 0, (LPARAM)btns[i].text);
        SendMessageW(g_hTB, TB_INSERTBUTTONW, i + (i >= 3 ? 1 : 0), (LPARAM)&tbb);
    }
    SendMessageW(g_hTB, TB_AUTOSIZE, 0, 0);
}

static void CreateMainControls(HWND h)
{
    /* 左侧：改名方式 */
    g_hGrpMode = MakeCtl(L"BUTTON", L"改名方式", BS_GROUPBOX, 0, IDC_GRP_MODE, h);
    for (int i = 0; i < MODE_COUNT; i++) {
        DWORD st = BS_AUTORADIOBUTTON;
        if (i == 0) st |= WS_GROUP;
        g_hRadMode[i] = MakeCtl(L"BUTTON", g_modeName[i], st, 0, IDC_RAD_MODE_BASE + i, h);
    }

    /* 右侧：参数设置 */
    g_hGrpParam = MakeCtl(L"BUTTON", L"参数设置", BS_GROUPBOX, 0, IDC_GRP_PARAM, h);

    g_hLblP1     = MakeCtl(L"STATIC", L"前缀：", SS_LEFT, 0, IDC_LBL_P1, h);
    g_hEdtP1     = MakeCtl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, IDC_EDT_P1, h);
    g_hLblStart  = MakeCtl(L"STATIC", L"起始号码：", SS_LEFT, 0, IDC_LBL_START, h);
    g_hEdtStart  = MakeCtl(L"EDIT", L"1", WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER, WS_EX_CLIENTEDGE, IDC_EDT_START, h);
    g_hLblWidth  = MakeCtl(L"STATIC", L"编号位数：", SS_LEFT, 0, IDC_LBL_WIDTH, h);
    g_hCmbWidth  = MakeCtl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 0, IDC_CMB_WIDTH, h);
    for (int i = 1; i <= 6; i++) {
        WCHAR t[8];
        swprintf(t, 8, L"%d", i);
        SendMessageW(g_hCmbWidth, CB_ADDSTRING, 0, (LPARAM)t);
    }
    SendMessageW(g_hCmbWidth, CB_SETCURSEL, 2, 0);

    g_hLblPrefix = MakeCtl(L"STATIC", L"前缀：", SS_LEFT, 0, IDC_LBL_PREFIX, h);
    g_hEdtPrefix = MakeCtl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, IDC_EDT_PREFIX, h);
    g_hLblSuffix = MakeCtl(L"STATIC", L"后缀：", SS_LEFT, 0, IDC_LBL_SUFFIX, h);
    g_hEdtSuffix = MakeCtl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, IDC_EDT_SUFFIX, h);

    g_hLblFind  = MakeCtl(L"STATIC", L"查找内容：", SS_LEFT, 0, IDC_LBL_FIND, h);
    g_hEdtFind  = MakeCtl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, IDC_EDT_FIND, h);
    g_hLblRepl  = MakeCtl(L"STATIC", L"替换为：", SS_LEFT, 0, IDC_LBL_REPL, h);
    g_hEdtRepl  = MakeCtl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, IDC_EDT_REPL, h);
    g_hChkCase  = MakeCtl(L"BUTTON", L"区分大小写", BS_AUTOCHECKBOX, 0, IDC_CHK_CASE, h);

    g_hLblExt     = MakeCtl(L"STATIC", L"新扩展名：", SS_LEFT, 0, IDC_LBL_EXT, h);
    g_hEdtExt     = MakeCtl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, IDC_EDT_EXT, h);
    g_hLblExtWarn = MakeCtl(L"STATIC",
                            L"注意：只修改文件名，不改变文件内容，重要文件请先备份。",
                            SS_LEFT, 0, IDC_LBL_EXTWARN, h);

    g_hRadUpper  = MakeCtl(L"BUTTON", L"全部大写", BS_AUTORADIOBUTTON | WS_GROUP, 0, IDC_RAD_UPPER, h);
    g_hRadLower  = MakeCtl(L"BUTTON", L"全部小写", BS_AUTORADIOBUTTON, 0, IDC_RAD_LOWER, h);
    g_hChkExtCase = MakeCtl(L"BUTTON", L"同时转换扩展名", BS_AUTOCHECKBOX, 0, IDC_CHK_EXTCASE, h);
    SendMessageW(g_hRadUpper, BM_SETCHECK, BST_CHECKED, 0);

    /* 文件列表 */
    g_hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, NULL,
                              WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
                              0, 0, 10, 10, h, (HMENU)IDC_LIST, g_hInst, NULL);
    SetCtlFont(g_hList);
    ListView_SetExtendedListViewStyle(g_hList, LVS_EX_GRIDLINES | LVS_EX_LABELTIP);

    LVCOLUMNW col;
    ZeroMemory(&col, sizeof(col));
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    for (int i = 0; i < COL_COUNT; i++) {
        col.iSubItem = i;
        col.pszText = (LPWSTR)g_colName[i];
        col.cx = S(g_colW[i]);
        ListView_InsertColumn(g_hList, i, &col);
    }

    /* 状态栏 */
    g_hStatus = CreateWindowExW(0, STATUSCLASSNAMEW, L"就绪",
                                WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                                0, 0, 0, 0, h, (HMENU)IDC_STATUS, g_hInst, NULL);
    SetCtlFont(g_hStatus);

    SendMessageW(g_hRadMode[g_mode], BM_SETCHECK, BST_CHECKED, 0);
}

/* ─────────────────────────── 界面：布局 ─────────────────────────── */
static void HideAllParams(void)
{
    HWND all[] = {
        g_hLblP1, g_hEdtP1, g_hLblStart, g_hEdtStart, g_hLblWidth, g_hCmbWidth,
        g_hLblPrefix, g_hEdtPrefix, g_hLblSuffix, g_hEdtSuffix,
        g_hLblFind, g_hEdtFind, g_hLblRepl, g_hEdtRepl, g_hChkCase,
        g_hLblExt, g_hEdtExt, g_hLblExtWarn,
        g_hRadUpper, g_hRadLower, g_hChkExtCase
    };
    for (int i = 0; i < (int)(sizeof(all) / sizeof(all[0])); i++)
        ShowWindow(all[i], SW_HIDE);
}

static void LayoutParams(void)
{
    RECT rc;
    GetWindowRect(g_hGrpParam, &rc);
    MapWindowPoints(NULL, g_hMain, (LPPOINT)&rc, 2);

    int x = rc.left + S(12), y = rc.top + S(24);
    int lw = S(66), ew = S(170), rh = S(26);

    HideAllParams();

    switch (g_mode) {
    case MODE_NUMBER:
    case MODE_ALL: {
        SetWindowTextW(g_hLblP1, g_mode == MODE_ALL ? L"名称：" : L"前缀：");
        SetWindowPos(g_hLblP1, NULL, x, y + S(3), lw, S(18), SWP_NOZORDER);
        SetWindowPos(g_hEdtP1, NULL, x + lw, y, ew, S(22), SWP_NOZORDER);
        SetWindowPos(g_hLblStart, NULL, x, y + rh + S(3), S(70), S(18), SWP_NOZORDER);
        SetWindowPos(g_hEdtStart, NULL, x + S(74), y + rh, S(60), S(22), SWP_NOZORDER);
        SetWindowPos(g_hLblWidth, NULL, x + S(150), y + rh + S(3), S(70), S(18), SWP_NOZORDER);
        SetWindowPos(g_hCmbWidth, NULL, x + S(222), y + rh, S(60), S(200), SWP_NOZORDER);
        ShowWindow(g_hLblP1, SW_SHOW); ShowWindow(g_hEdtP1, SW_SHOW);
        ShowWindow(g_hLblStart, SW_SHOW); ShowWindow(g_hEdtStart, SW_SHOW);
        ShowWindow(g_hLblWidth, SW_SHOW); ShowWindow(g_hCmbWidth, SW_SHOW);
        break;
    }
    case MODE_AFFIX:
        SetWindowPos(g_hLblPrefix, NULL, x, y + S(3), lw, S(18), SWP_NOZORDER);
        SetWindowPos(g_hEdtPrefix, NULL, x + lw, y, ew, S(22), SWP_NOZORDER);
        SetWindowPos(g_hLblSuffix, NULL, x, y + rh + S(3), lw, S(18), SWP_NOZORDER);
        SetWindowPos(g_hEdtSuffix, NULL, x + lw, y + rh, ew, S(22), SWP_NOZORDER);
        ShowWindow(g_hLblPrefix, SW_SHOW); ShowWindow(g_hEdtPrefix, SW_SHOW);
        ShowWindow(g_hLblSuffix, SW_SHOW); ShowWindow(g_hEdtSuffix, SW_SHOW);
        break;
    case MODE_REPLACE:
        SetWindowPos(g_hLblFind, NULL, x, y + S(3), S(70), S(18), SWP_NOZORDER);
        SetWindowPos(g_hEdtFind, NULL, x + S(74), y, ew, S(22), SWP_NOZORDER);
        SetWindowPos(g_hLblRepl, NULL, x, y + rh + S(3), S(70), S(18), SWP_NOZORDER);
        SetWindowPos(g_hEdtRepl, NULL, x + S(74), y + rh, ew, S(22), SWP_NOZORDER);
        SetWindowPos(g_hChkCase, NULL, x, y + rh * 2 + S(2), S(120), S(20), SWP_NOZORDER);
        ShowWindow(g_hLblFind, SW_SHOW); ShowWindow(g_hEdtFind, SW_SHOW);
        ShowWindow(g_hLblRepl, SW_SHOW); ShowWindow(g_hEdtRepl, SW_SHOW);
        ShowWindow(g_hChkCase, SW_SHOW);
        break;
    case MODE_EXT:
        SetWindowPos(g_hLblExt, NULL, x, y + S(3), S(70), S(18), SWP_NOZORDER);
        SetWindowPos(g_hEdtExt, NULL, x + S(74), y, S(90), S(22), SWP_NOZORDER);
        SetWindowPos(g_hLblExtWarn, NULL, x, y + rh + S(4), rc.right - rc.left - S(24), S(36), SWP_NOZORDER);
        ShowWindow(g_hLblExt, SW_SHOW); ShowWindow(g_hEdtExt, SW_SHOW);
        ShowWindow(g_hLblExtWarn, SW_SHOW);
        break;
    case MODE_CASE:
        SetWindowPos(g_hRadUpper, NULL, x, y + S(3), S(90), S(20), SWP_NOZORDER);
        SetWindowPos(g_hRadLower, NULL, x + S(96), y + S(3), S(90), S(20), SWP_NOZORDER);
        SetWindowPos(g_hChkExtCase, NULL, x, y + rh + S(2), S(140), S(20), SWP_NOZORDER);
        ShowWindow(g_hRadUpper, SW_SHOW); ShowWindow(g_hRadLower, SW_SHOW);
        ShowWindow(g_hChkExtCase, SW_SHOW);
        break;
    }
}

static void DoLayout(void)
{
    RECT rc;
    GetClientRect(g_hMain, &rc);
    int W = rc.right, H = rc.bottom;

    SendMessageW(g_hTB, TB_AUTOSIZE, 0, 0);
    SendMessageW(g_hStatus, WM_SIZE, 0, 0);

    RECT rt, rs;
    GetWindowRect(g_hTB, &rt);
    GetWindowRect(g_hStatus, &rs);
    int top = rt.bottom - rt.top;
    int statusH = rs.bottom - rs.top;
    int pad = S(6);

    int blockH = S(104);
    SetWindowPos(g_hGrpMode, NULL, pad, top + pad, S(132), blockH, SWP_NOZORDER);
    int i;
    for (i = 0; i < MODE_COUNT; i++)
        SetWindowPos(g_hRadMode[i], NULL, pad + S(10), top + pad + S(20) + i * S(20),
                     S(112), S(18), SWP_NOZORDER);

    SetWindowPos(g_hGrpParam, NULL, pad + S(138), top + pad,
                 W - (pad + S(138)) - pad, blockH, SWP_NOZORDER);
    LayoutParams();

    int listY = top + pad + blockH + pad;
    int listH = H - listY - statusH - pad;
    if (listH < S(60)) listH = S(60);
    SetWindowPos(g_hList, NULL, pad, listY, W - pad * 2, listH, SWP_NOZORDER);

    /* 状态栏分栏 */
    int parts[3];
    parts[0] = W - S(300);
    parts[1] = W - S(150);
    parts[2] = -1;
    SendMessageW(g_hStatus, SB_SETPARTS, 3, (LPARAM)parts);
}

static void OnModeChanged(void)
{
    for (int i = 0; i < MODE_COUNT; i++)
        if (SendMessageW(g_hRadMode[i], BM_GETCHECK, 0, 0) == BST_CHECKED) g_mode = i;
    LayoutParams();
    UpdatePreviewAndList();
}

/* ─────────────────────────── 参数记忆 ─────────────────────────── */
static void IniPath(WCHAR *out) { swprintf(out, MAX_PATH, L"%ls\\rename.ini", g_exeDir); }

static void SaveSettings(void)
{
    WCHAR ini[MAX_PATH];
    IniPath(ini);
    WINDOWPLACEMENT wp;
    wp.length = sizeof(wp);
    if (GetWindowPlacement(g_hMain, &wp)) {
        WCHAR b[64];
        swprintf(b, 64, L"%d", wp.rcNormalPosition.left);
        WritePrivateProfileStringW(L"Window", L"X", b, ini);
        swprintf(b, 64, L"%d", wp.rcNormalPosition.top);
        WritePrivateProfileStringW(L"Window", L"Y", b, ini);
        swprintf(b, 64, L"%d", wp.rcNormalPosition.right - wp.rcNormalPosition.left);
        WritePrivateProfileStringW(L"Window", L"W", b, ini);
        swprintf(b, 64, L"%d", wp.rcNormalPosition.bottom - wp.rcNormalPosition.top);
        WritePrivateProfileStringW(L"Window", L"H", b, ini);
    }
    WCHAR b[64];
    swprintf(b, 64, L"%d", g_mode);
    WritePrivateProfileStringW(L"Options", L"Mode", b, ini);

    WCHAR t[512];
    GetWindowTextW(g_hEdtP1, t, 512);      WritePrivateProfileStringW(L"Options", L"P1", t, ini);
    GetWindowTextW(g_hEdtPrefix, t, 512);  WritePrivateProfileStringW(L"Options", L"Prefix", t, ini);
    GetWindowTextW(g_hEdtSuffix, t, 512);  WritePrivateProfileStringW(L"Options", L"Suffix", t, ini);
    GetWindowTextW(g_hEdtFind, t, 512);    WritePrivateProfileStringW(L"Options", L"Find", t, ini);
    GetWindowTextW(g_hEdtRepl, t, 512);    WritePrivateProfileStringW(L"Options", L"Replace", t, ini);
    GetWindowTextW(g_hEdtExt, t, 128);     WritePrivateProfileStringW(L"Options", L"Ext", t, ini);
    GetWindowTextW(g_hEdtStart, t, 64);    WritePrivateProfileStringW(L"Options", L"Start", t, ini);
    swprintf(b, 64, L"%d", (int)SendMessageW(g_hCmbWidth, CB_GETCURSEL, 0, 0) + 1);
    WritePrivateProfileStringW(L"Options", L"Width", b, ini);
    swprintf(b, 64, L"%d", SendMessageW(g_hChkCase, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0);
    WritePrivateProfileStringW(L"Options", L"Case", b, ini);
    swprintf(b, 64, L"%d", SendMessageW(g_hChkExtCase, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0);
    WritePrivateProfileStringW(L"Options", L"ExtCase", b, ini);
    swprintf(b, 64, L"%d", SendMessageW(g_hRadUpper, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0);
    WritePrivateProfileStringW(L"Options", L"Upper", b, ini);
}

static void LoadSettings(void)
{
    WCHAR ini[MAX_PATH];
    IniPath(ini);
    WCHAR t[512];

    g_mode = GetPrivateProfileIntW(L"Options", L"Mode", 0, ini);
    if (g_mode < 0 || g_mode >= MODE_COUNT) g_mode = 0;

    GetPrivateProfileStringW(L"Options", L"P1", L"", t, 512, ini);      SetWindowTextW(g_hEdtP1, t);
    GetPrivateProfileStringW(L"Options", L"Prefix", L"", t, 512, ini);  SetWindowTextW(g_hEdtPrefix, t);
    GetPrivateProfileStringW(L"Options", L"Suffix", L"", t, 512, ini);  SetWindowTextW(g_hEdtSuffix, t);
    GetPrivateProfileStringW(L"Options", L"Find", L"", t, 512, ini);    SetWindowTextW(g_hEdtFind, t);
    GetPrivateProfileStringW(L"Options", L"Replace", L"", t, 512, ini); SetWindowTextW(g_hEdtRepl, t);
    GetPrivateProfileStringW(L"Options", L"Ext", L"", t, 128, ini);     SetWindowTextW(g_hEdtExt, t);
    GetPrivateProfileStringW(L"Options", L"Start", L"1", t, 64, ini);   SetWindowTextW(g_hEdtStart, t);

    int w = GetPrivateProfileIntW(L"Options", L"Width", 3, ini);
    if (w < 1) w = 1;
    if (w > 6) w = 6;
    SendMessageW(g_hCmbWidth, CB_SETCURSEL, w - 1, 0);

    if (GetPrivateProfileIntW(L"Options", L"Case", 0, ini))
        SendMessageW(g_hChkCase, BM_SETCHECK, BST_CHECKED, 0);
    if (GetPrivateProfileIntW(L"Options", L"ExtCase", 0, ini))
        SendMessageW(g_hChkExtCase, BM_SETCHECK, BST_CHECKED, 0);
    if (GetPrivateProfileIntW(L"Options", L"Upper", 1, ini))
        SendMessageW(g_hRadUpper, BM_SETCHECK, BST_CHECKED, 0);
    else
        SendMessageW(g_hRadLower, BM_SETCHECK, BST_CHECKED, 0);

    SendMessageW(g_hRadMode[g_mode], BM_SETCHECK, BST_CHECKED, 0);
    LayoutParams();
}

/* ─────────────────────────── 快捷键 ─────────────────────────── */
static void CreateAccels(void)
{
    ACCEL acc[9];
    int n = 0;
    acc[n].fVirt = FVIRTKEY | FCONTROL; acc[n].key = 'A'; acc[n++].cmd = IDM_FILE_ADDFILES;
    acc[n].fVirt = FVIRTKEY | FCONTROL; acc[n].key = 'D'; acc[n++].cmd = IDM_FILE_ADDDIR;
    acc[n].fVirt = FVIRTKEY | FCONTROL; acc[n].key = 'L'; acc[n++].cmd = IDM_EDIT_SELALL;
    acc[n].fVirt = FVIRTKEY;            acc[n].key = VK_DELETE; acc[n++].cmd = IDM_EDIT_REMOVE;
    acc[n].fVirt = FVIRTKEY;            acc[n].key = VK_F5; acc[n++].cmd = IDM_RULE_START;
    acc[n].fVirt = FVIRTKEY | FCONTROL; acc[n].key = 'Z'; acc[n++].cmd = IDM_RULE_UNDO;
    acc[n].fVirt = FVIRTKEY;            acc[n].key = VK_F3; acc[n++].cmd = IDM_RULE_REFRESH;
    acc[n].fVirt = FVIRTKEY;            acc[n].key = VK_F1; acc[n++].cmd = IDM_HELP_HELP;
    g_hAccel = CreateAcceleratorTableW(acc, n);
}

/* ─────────────────────────── 主窗口过程 ─────────────────────────── */
static void OnCommand(int id, HWND hCtl, UINT code)
{
    switch (id) {
    case IDM_FILE_ADDFILES: case IDT_ADDFILES: DoAddFiles(); return;
    case IDM_FILE_ADDDIR:   case IDT_ADDDIR:   DoAddDir(); return;
    case IDM_FILE_EXIT:     SendMessageW(g_hMain, WM_CLOSE, 0, 0); return;
    case IDM_EDIT_SELALL:
        for (int i = 0; i < g_count; i++)
            ListView_SetItemState(g_hList, i, LVIS_SELECTED, LVIS_SELECTED);
        SetFocus(g_hList);
        return;
    case IDM_EDIT_REMOVE:   case IDT_REMOVE:   DoRemoveSelected(); return;
    case IDM_EDIT_CLEAR:                       DoClearAll(); return;
    case IDM_RULE_START:    case IDT_START:    DoRename(); return;
    case IDM_RULE_UNDO:                        DoUndo(); return;
    case IDM_RULE_REFRESH:                     UpdatePreviewAndList(); return;
    case IDM_HELP_HELP:                        ShowHelpWindow(g_hMain); return;
    case IDM_HELP_ABOUT:                       ShowAboutBox(g_hMain); return;
    }

    if (id >= IDC_RAD_MODE_BASE && id < IDC_RAD_MODE_BASE + MODE_COUNT) {
        OnModeChanged();
        return;
    }
    if (id == IDC_RAD_UPPER || id == IDC_RAD_LOWER) { UpdatePreviewAndList(); return; }

    /* 参数变化 → 刷新预览 */
    switch (id) {
    case IDC_EDT_P1: case IDC_EDT_PREFIX: case IDC_EDT_SUFFIX:
    case IDC_EDT_FIND: case IDC_EDT_REPL: case IDC_EDT_EXT:
    case IDC_EDT_START:
        if (code == EN_CHANGE) UpdatePreviewAndList();
        return;
    case IDC_CHK_CASE: case IDC_CHK_EXTCASE:
        if (code == BN_CLICKED) UpdatePreviewAndList();
        return;
    case IDC_CMB_WIDTH:
        if (code == CBN_SELCHANGE) UpdatePreviewAndList();
        return;
    }
    (void)hCtl;
}

static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        CreateMenuBar(h);
        CreateToolbar(h);
        CreateMainControls(h);
        CreateAccels();
        DragAcceptFiles(h, TRUE);
        ApplyClassicLook(h);
        ApplyClassicLook(g_hTB);
        ApplyClassicLook(g_hList);
        ApplyClassicLook(g_hStatus);
        LoadSettings();
        DoLayout();
        UpdatePreviewAndList();
        StatusSet(0, L"就绪");
        return 0;

    case WM_SIZE:
        DoLayout();
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO *)l;
        mmi->ptMinTrackSize.x = S(620);
        mmi->ptMinTrackSize.y = S(420);
        return 0;
    }

    case WM_COMMAND:
        OnCommand(LOWORD(w), (HWND)l, HIWORD(w));
        return 0;

    case WM_NOTIFY: {
        LPNMHDR nh = (LPNMHDR)l;
        if (nh->idFrom == IDC_LIST) {
            if (nh->code == LVN_COLUMNCLICK) {
                LPNMLISTVIEW nv = (LPNMLISTVIEW)l;
                SortByColumn(nv->iSubItem);
                return 0;
            }
            if (nh->code == NM_CUSTOMDRAW) {
                LPNMLVCUSTOMDRAW cd = (LPNMLVCUSTOMDRAW)l;
                switch (cd->nmcd.dwDrawStage) {
                case CDDS_PREPAINT:
                    return CDRF_NOTIFYITEMDRAW;
                case CDDS_ITEMPREPAINT: {
                    int i = (int)cd->nmcd.dwItemSpec;
                    if (i >= 0 && i < g_count) {
                        if (g_items[i].status == 1) cd->clrText = RGB(255, 0, 0);
                        else if (g_items[i].status == 2) cd->clrText = RGB(150, 0, 0);
                    }
                    return CDRF_NEWFONT;
                }
                }
                return CDRF_DODEFAULT;
            }
        }
        return 0;
    }

    case WM_DROPFILES: {
        HDROP hd = (HDROP)w;
        UINT n = DragQueryFileW(hd, 0xFFFFFFFF, NULL, 0);
        int added = 0;
        for (UINT i = 0; i < n; i++) {
            WCHAR path[MAX_PATH];
            DragQueryFileW(hd, i, path, MAX_PATH);
            DWORD attr = GetFileAttributesW(path);
            if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
                added += AddDirectoryItems(path);
            else if (AddFileItem(path))
                added++;
        }
        DragFinish(hd);
        UpdatePreviewAndList();
        if (added == 0)
            MessageBoxW(h, L"拖入的项目中没有可添加的文件。", L"小雨超级文件批量改名专家",
                        MB_OK | MB_ICONINFORMATION);
        SetForegroundWindow(h);
        return 0;
    }

    case WM_CLOSE:
        if (g_count > 0) {
            if (MessageBoxW(h, L"文件列表中还有文件，退出后列表将丢失。\n\n确定要退出吗？",
                            L"小雨超级文件批量改名专家",
                            MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
        }
        SaveSettings();
        DestroyWindow(h);
        return 0;

    case WM_DESTROY:
        FreeItems();
        if (g_hAccel) DestroyAcceleratorTable(g_hAccel);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

/* ─────────────────────────── 入口 ─────────────────────────── */
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR cmd, int show)
{
    (void)hPrev; (void)cmd;
    g_hInst = hInst;

    /* 高 DPI 感知：故意不声明，保持 DPI 不感知状态，
       让系统把 96dpi 的画面整体拉伸，从而得到经典（略糊）的观感。 */

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES |
                ICC_TREEVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_UPDOWN_CLASS;
    InitCommonControlsEx(&icc);
    CoInitialize(NULL);

    /* 经典界面字体：宋体 9pt，不平滑 */
    g_hFont = UiFontCreate(9, FALSE);

    GetExeDir(g_exeDir);

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hInst;
    wc.hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                 GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"XYRenameMain";
    wc.lpszMenuName = NULL;
    if (!RegisterClassExW(&wc)) return 1;

    int W = S(760), H = S(540);
    HWND hwnd = CreateWindowExW(0, L"XYRenameMain", APP_TITLE,
                                WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, W, H,
                                NULL, NULL, hInst, NULL);
    if (!hwnd) return 1;

    /* 恢复上次窗口位置 */
    {
        WCHAR ini[MAX_PATH];
        IniPath(ini);
        int x = GetPrivateProfileIntW(L"Window", L"X", -100000, ini);
        if (x != -100000) {
            int y = GetPrivateProfileIntW(L"Window", L"Y", 0, ini);
            int cw = GetPrivateProfileIntW(L"Window", L"W", W, ini);
            int ch = GetPrivateProfileIntW(L"Window", L"H", H, ini);
            if (cw < S(620)) cw = W;
            if (ch < S(420)) ch = H;
            SetWindowPos(hwnd, NULL, x, y, cw, ch, SWP_NOZORDER);
        }
    }

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (g_hAccel && TranslateAcceleratorW(hwnd, g_hAccel, &msg)) continue;
        if (IsDialogMessageW(hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return (int)msg.wParam;
}
