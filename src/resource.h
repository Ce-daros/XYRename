/* resource.h — 小雨超级文件批量改名专家 V1.1 资源编号定义 */
#ifndef XYRESOURCE_H
#define XYRESOURCE_H

/* 图标 */
#define IDI_APP             101
#define IDI_TB_ADDFILE      102
#define IDI_TB_ADDDIR       103
#define IDI_TB_REMOVE       104
#define IDI_TB_START        105
#define IDI_TB_UNDO         106
#define IDI_HP_BACK         107
#define IDI_HP_FWD          108
#define IDI_HP_HOME         109
#define IDI_HP_PRINT        110
#define IDI_HP_PANEL        111
#define IDI_TREE_CLOSED     112
#define IDI_TREE_OPEN       113
#define IDI_TREE_PAGE       114
#define IDI_HELP            115

/* 菜单命令 */
#define IDM_FILE_ADDFILES   2001
#define IDM_FILE_ADDDIR     2002
#define IDM_FILE_EXIT       2003
#define IDM_FILE_SUBDIR     2004
#define IDM_EDIT_SELALL     2011
#define IDM_EDIT_REMOVE     2012
#define IDM_EDIT_CLEAR      2013
#define IDM_RULE_START      2021
#define IDM_RULE_UNDO       2022
#define IDM_RULE_REFRESH    2023
#define IDM_HELP_HELP       2031
#define IDM_HELP_ABOUT      2032

/* 工具栏按钮 */
#define IDT_ADDFILES        3001
#define IDT_ADDDIR          3002
#define IDT_REMOVE          3003
#define IDT_START           3004

/* 帮助窗口控件 */
#define IDH_BACK            4001
#define IDH_FWD             4002
#define IDH_HOME            4003
#define IDH_PRINT           4004
#define IDH_PANEL           4005
#define IDH_TABS            4006
#define IDH_TREE            4007
#define IDH_INDEX_EDIT      4008
#define IDH_INDEX_LIST      4009
#define IDH_RICH            4010
#define IDH_TOOLBAR         4011

/* 主窗口控件 */
#define IDC_GRP_MODE        5001
#define IDC_RAD_MODE_BASE   5010   /* 5010 ~ 5016 七种改名方式 */
#define IDC_GRP_PARAM       5020
#define IDC_LBL_P1          5021
#define IDC_EDT_P1          5022
#define IDC_LBL_START       5023
#define IDC_EDT_START       5024
#define IDC_LBL_WIDTH       5025
#define IDC_CMB_WIDTH       5026
#define IDC_LBL_PREFIX      5030
#define IDC_EDT_PREFIX      5031
#define IDC_LBL_SUFFIX      5032
#define IDC_EDT_SUFFIX      5033
#define IDC_LBL_FIND        5040
#define IDC_EDT_FIND        5041
#define IDC_LBL_REPL        5042
#define IDC_EDT_REPL        5043
#define IDC_CHK_CASE        5044
#define IDC_CHK_REGEX       5045   /* 查找替换使用正则表达式 */
#define IDC_LBL_EXT         5050
#define IDC_EDT_EXT         5051
#define IDC_LBL_EXTWARN     5052
#define IDC_RAD_UPPER       5060
#define IDC_RAD_LOWER       5061
#define IDC_CHK_EXTCASE     5062
#define IDC_LBL_DATE        5070   /* 日期格式 */
#define IDC_EDT_DATE        5071
#define IDC_LBL_DATEHINT    5072
#define IDC_LBL_PRESET      5080   /* 改名方案 */
#define IDC_CMB_PRESET      5081
#define IDC_BTN_SAVE_PRESET 5082
#define IDC_BTN_DEL_PRESET  5083
#define IDC_LIST            5100
#define IDC_STATUS          5101
#define IDC_TOOLBAR         5102
#define IDC_PROGRESS        5103
#define IDC_PROG_TEXT       5104
#define IDC_PROG_BTN        5105

/* 输入框（保存方案时输入名称）*/
#define IDC_INPUT_LABEL     5300
#define IDC_INPUT_EDIT      5301
#define IDC_INPUT_OK        5302
#define IDC_INPUT_CANCEL    5303

/* 关于对话框控件 */
#define IDC_ABOUT_ICON      5200
#define IDC_ABOUT_NAME      5201
#define IDC_ABOUT_VER       5202
#define IDC_ABOUT_TEXT      5203
#define IDC_ABOUT_COPY      5204
#define IDC_ABOUT_AUTHOR    5205
#define IDC_ABOUT_WARN      5206
#define IDC_ABOUT_HOME      5207
#define IDC_ABOUT_OK        5208
#define IDC_ABOUT_LINE1     5209
#define IDC_ABOUT_LINE2     5210

#endif /* XYRESOURCE_H */
