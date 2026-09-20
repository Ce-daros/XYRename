/* topics.h — 帮助专题数据 */
#ifndef XYTOPICS_H
#define XYTOPICS_H

#include <windows.h>

typedef struct {
    const WCHAR *title;
    const WCHAR *text;   /* 标记格式：
                            #  小节标题（加粗、深蓝）
                            *  小标题（加粗）
                            -  项目符号
                            !  注意 / 提示（深红加粗）
                            >  参见（蓝色）
                            |  缩进正文
                            空行 → 段落间隔
                            其余为正文 */
} HelpTopic;

typedef struct {
    int          level;   /* 0 根  1 章  2 主题 */
    const WCHAR *title;
    int          topic;   /* -1 表示章（不可显示） */
} HelpNode;

extern const HelpTopic g_topics[];
extern const int       g_topicCount;
extern const HelpNode  g_nodes[];
extern const int       g_nodeCount;

#endif /* XYTOPICS_H */
