/* exif.h — 读取 JPEG 照片的拍摄日期（EXIF）
 * 本模块只解析 JPEG 的 APP1/EXIF 段，不依赖任何第三方库。
 */
#ifndef XYEXIF_H
#define XYEXIF_H

#include <windows.h>

/* 从指定文件中读取拍摄日期（优先 DateTimeOriginal）。
 * 成功返回 TRUE，并把日期时间填入 st；
 * 若文件不是 JPEG、没有 EXIF 信息或数据损坏，返回 FALSE。 */
BOOL ExifReadDateTime(const WCHAR *path, SYSTEMTIME *st);

#endif /* XYEXIF_H */
