开发本项目时，请完整采用 2007 年中国 Windows 共享软件、软件园与个人软件作者的产品和文字风格。将本项目视为作者认真公开发布的一款完整软件，而非现代开源项目或互联网产品。

界面、帮助、README、About、错误提示、版本信息和文案统一使用当年的简体中文软件语言：正式、热情、具体，带适度技术自豪感。明确介绍软件名称、版本、用途、主要特点、运行环境、使用方法、注意事项、更新历史、作者与技术信息；主动列出“功能强大、操作简单、占用资源少、运行稳定、绿色方便”等可由实际功能支持的优势。功能再小也认真说明，不使用现代互联网、SaaS、创业公司、极简文案或玩梗式措辞。

整体应像 2007 年软件园下载页面中的优秀国产共享软件：作者珍惜自己的作品，也尊重第一次使用它的普通电脑用户。所有功能应有清楚提示，错误信息告诉用户原因和解决办法，帮助内容足以让陌生用户独立上手。About 页面保留软件名称、版本、版权、作者/工作室、联系方式占位符及开发技术，并自然体现“国产软件”的时代语感。

视觉采用 Windows 2000/XP Classic 风格：标准系统控件、灰色界面、传统菜单栏、工具栏、状态栏、分组框和标准对话框；避免现代扁平化、卡片、圆角、大留白和移动 App 式设计。最终效果应让人第一眼觉得这是一个认真维护、刚刚于 2007 年上传到华军软件园或天空软件站的软件。

技术栈是原生 win32，classic主题。

---

# 工程说明（开发时必读）

## 一、软件与作者信息

- 软件名称：小雨超级文件批量改名专家（英文名 XYRename）
- 版本：V1.1 正式版（发布年份设定为 2007 年）
- 作者：小雨　工作室：小雨软件工作室
- 主页：angelkawaii.xyz　邮箱：maryssmith2021@gmail.com
- 声明口径：免费软件（Freeware），绿色免安装，不写注册表，拷贝即用
- 版本信息写在 `src/app.rc` 的 VERSIONINFO 中，改版本时务必同步修改
  `FILEVERSION`、`FileVersion`、`ProductVersion` 以及 About 框、README、帮助文档

## 二、技术栈与硬性约束

- 语言：C（C99 即可），**纯 Win32 SDK，禁止引入 MFC / Qt / 第三方库**
- 编译：MSYS2 MinGW-w64 gcc（见 `build.bat`），资源用 windres
- 字符：全部 Unicode（`-municode`、`wWinMain`、`WCHAR`、`...W` 系列 API）
- 依赖：只允许系统 DLL（user32 / gdi32 / comctl32 / comdlg32 / shell32 /
  shlwapi / ole32）。XP 上不存在的 API（uxtheme、dwmapi、SetProcessDPIAware 等）
  **必须动态加载**，保持“可在 XP 运行”的说法站得住
- 不做 DPI 感知：**不要调用 SetProcessDPIAware**，一律按 96dpi 像素布局，
  让系统整体拉伸——这是本项目“经典发糊观感”的来源，属于刻意设计
- 界面字体：`UiFontCreate(9, FALSE)`，即宋体 9pt + `NONANTIALIASED_QUALITY`，
  目的是直出 simsun.ttc 内 12px 那一档 1bpp 点阵
- 主题：无清单文件（manifest），使用 comctl32 v5 的经典灰色 3D 控件；
  窗口与控件调用 `ApplyClassicLook()`（SetWindowTheme 置空 + 关 Win11 圆角）
- 体积：目标 300KB 上下，不要引入会显著增大的依赖

## 三、目录结构与文件职责

```
XYRename.exe        编译产物（绿色单文件）
build.bat           一键编译脚本（windres + gcc）
README.txt          软件说明，发布用
帮助文档.txt        帮助文案源稿（21 个专题，与 src/topics.c 对应）
关于文字.txt        关于框文案与控件明细
开发计划.txt        计划书与完成情况
PLAN.md             后续版本路线图（V1.1 / V1.2 / V1.3）
src/main.c          主窗口、界面布局、文件列表、改名规则、预览、执行、撤销、参数记忆、改名方案
src/exif.c/.h       JPEG/EXIF 拍摄日期读取（V1.1 新增，纯 C，无第三方库）
src/regex.c/.h      极简正则表达式引擎（V1.1 新增，纯 C）
src/help.c          仿 CHM 帮助窗口（工具栏 + 目录/索引标签页 + 树 + RichEdit）
src/topics.c/.h     帮助正文（标记文本）与目录树数据
src/about.c         关于对话框
src/app.h           公共定义：改名方式枚举、列表列枚举、FileItem、S()、UiFontCreate()
src/resource.h      全部资源与控件编号
src/app.rc          图标与版本信息
res/*.ico           15 个 Windows XP 原版图标（16/32/48 三尺寸）
tools/              图标导出与对照表生成工具（可复现，不参与程序编译）
```

## 四、编译

```
build.bat
```

脚本内容要点（改动时注意别破坏）：

- `windres src\app.rc -o build\app.o --include-dir=res --codepage=65001 -F pe-x86-64`
  - 资源文件是 UTF-8 中文，**必须带 `--codepage=65001`**，否则中文乱码
  - **输出扩展名必须用 `.o`**：写成 `.res` 时 windres 会输出另一种格式，gcc 无法链接
- `gcc -O2 -municode -mwindows -Wall -s ...` 链接 `-lcomctl32 -lcomdlg32 -lshell32
  -lshlwapi -lole32 -luuid -lgdi32 -luser32`
- 编译时若程序正在运行会报 `cannot open output file XYRename.exe: Permission denied`，
  关掉程序再编译即可

## 五、代码约定

- 资源/控件编号统一写在 `src/resource.h`，命名：`IDI_` 图标、`IDM_` 菜单、
  `IDT_` 工具栏、`IDH_` 帮助窗口、`IDC_` 主窗口控件
- 新增改名方式：在 `app.h` 的 `MODE_*` 枚举末尾追加，同步改
  `main.c` 的 `g_modeName[]`、`CreateMainControls()` 里创建单选框、
  `LayoutParams()` 里布置参数控件、`ComputePreview()` 里计算新名字
- 新增列表列：在 `app.h` 的 `COL_*` 枚举、`main.c` 的 `g_colW[]`/`g_colName[]`、
  `RefreshList()`、`ItemCompare()` 中同步
- 界面尺寸一律用 `S()` 包裹（当前 `S()` 是恒等函数，保留是为了以后需要缩放时
  只改一处）
- 字符串处理用 `lstrcpynW` / `swprintf`（注意 `swprintf` 要带缓冲区长度）
- 缓冲区一律用 `MAX_PATH`，跨目录拼接前先检查长度
- 内存：文件列表 `g_items` 用 `realloc` 增长，退出时 `FreeItems()`
- 注释用简体中文，写得直白，说明“为什么这么做”
- 保持 `-Wall` 无警告

## 六、不可破坏的产品原则

1. **改名前必须预览**：`ComputePreview()` 先算出全部新文件名，有问题（重名、
   非法字符、空名、与磁盘已有文件冲突）的一律标红，`DoRename()` 发现红项必须
   拒绝执行并提示用户先修正
2. **失败必须说明原因**：`ErrorText()` 把 `GetLastError()` 翻译成中文原因
   （只读、被占用、无权限等），结束时统计成功/失败数量并列出失败文件
3. **可撤销**：改名成功后写 `undo.dat`（UTF-16LE + BOM，一行 `新路径|旧路径`），
   `DoUndo()` 读回并恢复，完成后删除该文件
4. **绿色**：只在程序所在目录写 `rename.ini` 与 `undo.dat`，不写注册表、
   不往系统目录拷贝文件
5. **不碰文件夹**：V1.1 只处理文件；文件夹改名是 V1.3 的规划项

## 七、测试

- 手工测试为主：改名、撤销、拖放、六种规则、重名/非法字符提示、参数记忆
- 自动化冒烟（可选，PowerShell + P/Invoke）：
  - `FindWindow("XYRenameMain", $null)` 找主窗口；帮助窗口类名 `XYHelpWnd`，
    关于框类名 `XYAboutWnd`，进度框类名 `XYProgWnd`
  - 用 `PostMessage(hwnd, WM_COMMAND, 菜单ID, 0)` 触发菜单命令
  - 注意：从 git-bash 启动 exe 时工作目录可能不是当前目录，
    传路径请用绝对路径；`wprintf` 的 `%s` 在 MinGW 下按 `char*` 解释，
    打印宽字符串要用 `%ls`
- 改名功能测试前先建临时目录，测试完删除

## 八、文案与文档同步

改动功能后，下列位置必须一起更新，避免自相矛盾：

- `src/topics.c`（程序内帮助正文）与 `帮助文档.txt`（同内容的文本稿）
- `README.txt`（功能列表、更新历史）
- `关于文字.txt`（关于框文字）
- `开发计划.txt` / `PLAN.md`（进度与路线图）
- `src/app.rc` 版本号与 `更新历史` 条目

写更新历史时用当年体例，例如：

```
V1.1 正式版　二○○七年
　1、增加按拍摄日期重命名；
　2、修正若干小问题。
```
