/* regex.h — 极简正则表达式引擎（仅供文件名查找替换使用）
 *
 * 支持的语法：
 *   .        任意一个字符
 *   * + ?    前一项重复 0 次以上 / 1 次以上 / 0 或 1 次（贪婪）
 *   [...]    字符类，可用 a-z 范围、^ 取反、\d \w \s
 *   (...)    分组，最多 9 个，可用 \1 ～ \9 反向引用
 *   ^ $      行首 / 行尾
 *   \d \w \s 数字 / 单词字符 / 空白（大写形式表示取反）
 *   \. \* \\ 等  转义为普通字符
 *
 * 不支持：{n,m}、|、非贪婪量词、Unicode 属性。够用就好。
 */
#ifndef XYREGEX_H
#define XYREGEX_H

#include <windows.h>

typedef struct Regex Regex;

/* 编译表达式。成功返回句柄（用完必须 RegexFree）；
 * 失败返回 NULL，并在 err 中写明原因（例如括号不配对）。 */
Regex *RegexCompile(const WCHAR *pattern, BOOL caseSensitive, WCHAR *err, int errCap);

/* 释放句柄 */
void RegexFree(Regex *re);

/* 只检查表达式是否合法 */
BOOL RegexIsValid(const WCHAR *pattern, WCHAR *err, int errCap);

/* 用已编译的表达式在 src 中查找替换，结果写入 out。
 * 替换串里可用 \1 ～ \9 引用分组，\\ 表示一个反斜杠。
 * 空匹配不插入替换内容，避免在文件名里到处插入字符。 */
BOOL RegexReplaceWith(Regex *re, const WCHAR *src, const WCHAR *repl,
                      WCHAR *out, int outCap);

#endif /* XYREGEX_H */
