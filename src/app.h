/* app.h — 公共定义 */
#ifndef XYAPP_H
#define XYAPP_H

#include <windows.h>

#define APP_TITLE    L"小雨超级文件批量改名专家 V1.4.1 正式版"
#define APP_NAME     L"小雨超级文件批量改名专家"
#define APP_VER      L"V1.4.1 正式版"

/* 改名方式 */
enum {
    MODE_NUMBER = 0,   /* 统一编号 */
    MODE_AFFIX,        /* 添加前后缀 */
    MODE_REPLACE,      /* 查找替换（可选用正则表达式） */
    MODE_EXT,          /* 修改扩展名 */
    MODE_ALL,          /* 全部替换 */
    MODE_CASE,         /* 大小写转换 */
    MODE_DATE,         /* 按拍摄日期编号 */
    MODE_COUNT
};

/* 列表列 */
enum {
    COL_ORIG = 0,
    COL_NEW,
    COL_EXT,
    COL_SIZE,
    COL_TIME,
    COL_COUNT
};

typedef struct {
    WCHAR     path[MAX_PATH];     /* 完整路径 */
    WCHAR     dir[MAX_PATH];      /* 所在目录 */
    WCHAR     base[MAX_PATH];     /* 主文件名（不含扩展名） */
    WCHAR     ext[64];            /* 扩展名（不含点号） */
    WCHAR     newname[MAX_PATH];  /* 预览的新文件名 */
    ULONGLONG size;
    FILETIME  mtime;
    SYSTEMTIME taken;             /* 拍摄日期（无 EXIF 时取文件修改时间） */
    int       hasTaken;           /* 1 = taken 来自 EXIF 拍摄时间 */
    int       status;             /* 0 正常  1 有问题  2 扩展名变化 */
} FileItem;

extern HINSTANCE g_hInst;

int  S(int v);                       /* 按 DPI 缩放 */
HFONT UiFontCreate(int pt, BOOL bold);   /* 经典界面字体：宋体 9pt，不平滑 */
void ApplyClassicLook(HWND h);       /* 强制经典绘制、关掉 Win11 圆角 */
void ShowAboutBox(HWND owner);       /* 关于对话框 */
void ShowHelpWindow(HWND owner);     /* 帮助窗口 */

#endif /* XYAPP_H */
