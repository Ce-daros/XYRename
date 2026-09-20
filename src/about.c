/* about.c — “关于”对话框
 * 采用传统 Win32 软件的关于框式样：图标 + 名称版本 + 简介 + 版权 + 联系方式 + 著作权警告
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include "resource.h"
#include "app.h"

static HFONT g_abFont, g_abFontBold, g_abFontTitle;

static void AbCtl(HWND h, int id, const WCHAR *cls, const WCHAR *text, DWORD style,
                  int x, int y, int w, int hh, HFONT f)
{
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             S(x), S(y), S(w), S(hh), h, (HMENU)(INT_PTR)id, g_hInst, NULL);
    if (f) SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
}

static LRESULT CALLBACK AboutProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE: {
        g_abFont = UiFontCreate(9, FALSE);
        g_abFontBold = UiFontCreate(9, TRUE);
        g_abFontTitle = UiFontCreate(12, TRUE);
        ApplyClassicLook(h);

        /* 图标 */
        HWND ic = CreateWindowExW(0, L"STATIC", NULL, WS_CHILD | WS_VISIBLE | SS_ICON,
                                  S(16), S(16), S(32), S(32), h, (HMENU)IDC_ABOUT_ICON, g_hInst, NULL);
        SendMessageW(ic, STM_SETICON, (WPARAM)LoadIconW(g_hInst, MAKEINTRESOURCEW(IDI_APP)), 0);

        AbCtl(h, IDC_ABOUT_NAME, L"STATIC", APP_NAME, SS_LEFT, 60, 16, 300, 24, g_abFontTitle);
        AbCtl(h, IDC_ABOUT_VER, L"STATIC", APP_VER L"　　免费软件", SS_LEFT, 62, 42, 300, 18, g_abFont);

        AbCtl(h, IDC_ABOUT_LINE1, L"STATIC", L"", SS_ETCHEDHORZ, 14, 70, 372, 2, NULL);

        AbCtl(h, IDC_ABOUT_TEXT, L"STATIC",
              L"本软件是一款小巧实用的文件批量改名工具，支持统一编号、添加前后缀、\n"
              L"查找替换（支持正则）、修改扩展名、大小写转换、按拍摄日期编号等改名\n"
              L"方式，导入目录可含子文件夹，常用规则可存为方案。全部操作均可预览，\n"
              L"安全可靠，并支持一键撤销。绿色软件，无需安装，不写注册表，拷贝即用。",
              SS_LEFT, 16, 84, 368, 68, g_abFont);

        AbCtl(h, IDC_ABOUT_COPY, L"STATIC", L"版权所有 (C) 2007 小雨软件工作室　保留所有权利",
              SS_LEFT, 16, 158, 368, 18, g_abFontBold);

        AbCtl(h, IDC_ABOUT_LINE2, L"STATIC", L"", SS_ETCHEDHORZ, 14, 182, 372, 2, NULL);

        AbCtl(h, IDC_ABOUT_AUTHOR, L"STATIC",
              L"作　者：小雨　　　　工作室：小雨软件工作室\n"
              L"主　页：angelkawaii.xyz\n"
              L"邮　箱：maryssmith2021@gmail.com\n"
              L"开发语言：C（纯 Win32 SDK）　开发工具：Visual C++ 6.0",
              SS_LEFT, 16, 192, 368, 72, g_abFont);

        AbCtl(h, IDC_ABOUT_WARN, L"STATIC",
              L"本软件为免费软件（Freeware），可自由使用和传播，转载请注明出处，\n"
              L"未经作者书面同意不得用于商业用途。\n"
              L"警告：本计算机程序受著作权法和国际条约的保护。未经授权擅自复制或\n"
              L"散布本程序的全部或任何部分，将受到严厉的民事和法律制裁。",
              SS_LEFT, 16, 268, 368, 72, g_abFont);

        AbCtl(h, IDC_ABOUT_HOME, L"BUTTON", L"访问主页", BS_PUSHBUTTON, 216, 350, 80, 26, g_abFont);
        AbCtl(h, IDC_ABOUT_OK, L"BUTTON", L"确定", BS_DEFPUSHBUTTON, 306, 350, 80, 26, g_abFont);
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDC_ABOUT_HOME:
            if ((INT_PTR)ShellExecuteW(h, L"open", L"http://angelkawaii.xyz/", NULL, NULL, SW_SHOWNORMAL) <= 32)
                MessageBoxW(h, L"无法打开默认浏览器，请手工访问：\nangelkawaii.xyz",
                            APP_NAME, MB_OK | MB_ICONINFORMATION);
            return 0;
        case IDC_ABOUT_OK:
        case IDCANCEL:
            DestroyWindow(h);
            return 0;
        }
        return 0;

    case WM_CLOSE:
        DestroyWindow(h);
        return 0;

    case WM_DESTROY:
        if (g_abFont) DeleteObject(g_abFont);
        if (g_abFontBold) DeleteObject(g_abFontBold);
        if (g_abFontTitle) DeleteObject(g_abFontTitle);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void ShowAboutBox(HWND owner)
{
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = AboutProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"XYAboutWnd";
    wc.hIcon = LoadIconW(g_hInst, MAKEINTRESOURCEW(IDI_APP));
    RegisterClassW(&wc);

    int W = S(410), H = S(424);
    HWND h = CreateWindowExW(WS_EX_DLGMODALFRAME, L"XYAboutWnd",
                             L"关于 " APP_NAME,
                             WS_POPUP | WS_CAPTION | WS_SYSMENU,
                             CW_USEDEFAULT, CW_USEDEFAULT, W, H,
                             owner, NULL, g_hInst, NULL);
    if (!h) return;

    RECT rc, ro;
    GetWindowRect(h, &rc);
    if (owner) {
        GetWindowRect(owner, &ro);
        SetWindowPos(h, NULL,
                     ro.left + ((ro.right - ro.left) - (rc.right - rc.left)) / 2,
                     ro.top + ((ro.bottom - ro.top) - (rc.bottom - rc.top)) / 2,
                     0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }

    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(h, SW_SHOW);
    UpdateWindow(h);

    MSG msg;
    while (IsWindow(h) && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (IsDialogMessageW(h, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (owner) { EnableWindow(owner, TRUE); SetForegroundWindow(owner); }
}
