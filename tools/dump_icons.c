/* dump_icons.c — 从系统 DLL 中导出全部图标资源，拼装成标准 .ico 文件
   用法: dump_icons <dll路径> <输出目录> */
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#pragma pack(push,1)
typedef struct {
    WORD idReserved;
    WORD idType;
    WORD idCount;
} ICONDIR;

typedef struct {
    BYTE  bWidth, bHeight, bColorCount, bReserved;
    WORD  wPlanes, wBitCount;
    DWORD dwBytesInRes;
    DWORD dwImageOffset;
} ICONDIRENTRY;

typedef struct {
    BYTE  bWidth, bHeight, bColorCount, bReserved;
    WORD  wPlanes, wBitCount;
    DWORD dwBytesInRes;
    WORD  nID;
} GRPICONDIRENTRY;

typedef struct {
    BYTE  bWidth, bHeight, bColorCount, bReserved;
    WORD  wPlanes, wBitCount;
    DWORD dwBytesInRes;
    WORD  nID;
} GRPICONDIRENTRY_RES; /* 资源内直接跟在 GRPICONDIR 后 */
#pragma pack(pop)

typedef BOOL (CALLBACK *ENUMRESPROC)(HMODULE, LPWSTR, LPWSTR, LONG_PTR);

static char g_outdir[MAX_PATH];

static BOOL SaveGroupIcon(HMODULE hMod, WORD grpId, int index)
{
    HRSRC hGrp = FindResourceW(hMod, MAKEINTRESOURCEW(grpId), RT_GROUP_ICON);
    if (!hGrp) return FALSE;
    HGLOBAL hG = LoadResource(hMod, hGrp);
    if (!hG) return FALSE;
    GRPICONDIRENTRY *grp = (GRPICONDIRENTRY *)LockResource(hG);
    DWORD grpSize = SizeofResource(hMod, hGrp);
    WORD count = *(WORD *)((BYTE *)grp + 4);
    if (count == 0 || grpSize < 6 + (DWORD)count * 14) return FALSE;

    /* 先收集每个 RT_ICON 的数据和大小 */
    BYTE **data = (BYTE **)calloc(count, sizeof(BYTE *));
    DWORD *sizes = (DWORD *)calloc(count, sizeof(DWORD));
    WORD i;
    for (i = 0; i < count; i++) {
        GRPICONDIRENTRY *e = (GRPICONDIRENTRY *)((BYTE *)grp + 6 + i * 14);
        HRSRC hI = FindResourceW(hMod, MAKEINTRESOURCEW(e->nID), RT_ICON);
        if (!hI) { data[i] = NULL; sizes[i] = 0; continue; }
        HGLOBAL hD = LoadResource(hMod, hI);
        if (!hD) { data[i] = NULL; sizes[i] = 0; continue; }
        data[i] = (BYTE *)LockResource(hD);
        sizes[i] = SizeofResource(hMod, hI);
    }

    char path[MAX_PATH];
    sprintf(path, "%s\\%d.ico", g_outdir, index);
    FILE *f = fopen(path, "wb");
    if (!f) { free(data); free(sizes); return FALSE; }

    ICONDIR hdr = { 0, 1, count };
    fwrite(&hdr, 1, 6, f);
    DWORD offset = 6 + (DWORD)count * 16;
    for (i = 0; i < count; i++) {
        GRPICONDIRENTRY *e = (GRPICONDIRENTRY *)((BYTE *)grp + 6 + i * 14);
        ICONDIRENTRY out;
        out.bWidth = e->bWidth; out.bHeight = e->bHeight;
        out.bColorCount = e->bColorCount; out.bReserved = 0;
        out.wPlanes = e->wPlanes; out.wBitCount = e->wBitCount;
        out.dwBytesInRes = sizes[i];
        out.dwImageOffset = offset;
        fwrite(&out, 1, 16, f);
        offset += sizes[i];
    }
    for (i = 0; i < count; i++)
        if (data[i] && sizes[i]) fwrite(data[i], 1, sizes[i], f);
    fclose(f);

    free(data); free(sizes);
    return TRUE;
}

static BOOL CALLBACK EnumProc(HMODULE hMod, LPCWSTR type, LPWSTR name, LONG_PTR lParam)
{
    (void)type; (void)lParam;
    static int idx = 0;
    if (IS_INTRESOURCE(name)) {
        SaveGroupIcon(hMod, (WORD)(ULONG_PTR)name, idx);
        printf("index %d -> group id %d\n", idx, (int)(ULONG_PTR)name);
    }
    idx++;
    return TRUE;
}

int main(int argc, char **argv)
{
    if (argc < 3) { printf("usage: dump_icons <dll> <outdir>\n"); return 1; }
    strcpy(g_outdir, argv[2]);
    HMODULE h = LoadLibraryW(L"shell32.dll"); /* 占位 */
    wchar_t wpath[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, wpath, MAX_PATH);
    FreeLibrary(h);
    h = LoadLibraryExW(wpath, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (!h) { printf("cannot load %s\n", argv[1]); return 1; }
    EnumResourceNamesW(h, RT_GROUP_ICON, EnumProc, 0);
    FreeLibrary(h);
    printf("done\n");
    return 0;
}
