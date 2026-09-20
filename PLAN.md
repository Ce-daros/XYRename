# 小雨超级文件批量改名专家　后续版本路线图（PLAN）

本文件记录 V1.0 之后准备继续做的功能，以及每条功能在代码里的实现路径，
方便日后接着往下写。计划中的内容以实际开发为准，做完一项勾掉一项。

当前版本：**V1.1 正式版（已完成并发布）**

---

## 通用发布流程（每个版本都要走一遍）

1. 改 `src/app.rc` 里的 `FILEVERSION` / `FileVersion` / `ProductVersion`
2. 同步更新下列文案，别出现自相矛盾的地方：
   - `src/topics.c`（程序内帮助正文）与 `帮助文档.txt`（文本稿）
   - `README.txt` 的功能列表与更新历史
   - `关于文字.txt`、`开发计划.txt`、本文件
3. `build.bat` 编译，确认 `-Wall` 无警告、体积没有明显膨胀
4. 按第七节的测试清单手工过一遍
5. `git commit` → 打标签 → `gh release create`，把 `XYRename.exe` 作为附件上传
6. 更新历史按当年体例写，例如：

   ```
   V1.1 正式版　二○○七年
   　1、增加按拍摄日期重命名；
   　2、修正若干小问题。
   ```

---

## V1.1　功能增强版（已完成 ✔）

主题：把“能改名”变成“更省事”。四条功能互相独立，可以分四次提交。

> 说明：V1.1 已经开发完成并发布。下面保留原来的实现思路，
> 实际做法与计划基本一致，细节以代码为准：
> - 子文件夹导入：`AddDirectoryItemsEx()` + `HasSubDir()` + `AskSubDirOnce()`，
>   选择记在 `rename.ini` 的 `Options\SubDir`，菜单增加可勾选项；
> - 拍摄日期：新增 `src/exif.c`（约 260 行），只读 JPEG 的 APP1 段，
>   无 EXIF 时回退到文件修改时间；新增 `MODE_DATE`；
> - 正则替换：新增 `src/regex.c`（约 420 行，回溯法），
>   支持 `. * + ? [] () \1..\9 ^ $ \d \w \s`；
> - 方案保存：`rename.ini` 的 `[Preset1]`～`[Preset9]`，
>   参数区增加“方案”下拉框与“保存方案... / 删除”按钮。

### 1.1.1　导入时包含子文件夹（已完成 ✔）

- **要解决的问题**：现在“添加目录”只导入一层，整理照片时往往要一个个文件夹点。
- **实现路径**：
  - `src/main.c` → `AddDirectoryItems()`：增加 `BOOL recursive` 参数，
    递归用 `FindFirstFileW` 遍历子目录（注意跳过 `.` 与 `..`，
    并加一个深度上限，避免目录环）
  - `src/main.c` → `DoAddDir()`：导入前用 `MessageBoxW` 询问
    “是否包含子文件夹？”，保持不引入自定义对话框
  - `src/main.c` → `WM_DROPFILES` 分支：拖入文件夹时同样处理
  - `src/main.c` → `LoadSettings()` / `SaveSettings()`：把选择记到
    `rename.ini` 的 `Options\SubDir`，下次默认沿用
- **验收**：拖入含多层子目录的文件夹，文件全部进列表且不重复；
  列表顺序稳定（按目录、文件名排序）。
- **注意**：递归时先判断 `FILE_ATTRIBUTE_DIRECTORY` 再判断是否为重解析点
  （符号链接/联接），避免无限递归。

### 1.1.2　按拍摄日期重命名（已完成 ✔）

- **要解决的问题**：数码相机文件名形如 `IMG_1234.jpg`，按拍摄时间整理最方便。
- **实现路径**：
  - 新增 `src/exif.c` / `src/exif.h`：只解析 JPEG 的 EXIF
    （APP1 → TIFF 头 → IFD0 的 `0x0132` 或 ExifIFD 的 `0x9003`
    `DateTimeOriginal`），**不引入任何第三方库**，约 150 行
  - `src/app.h`：`MODE_*` 枚举末尾加 `MODE_DATE`；
    `FileItem` 增加 `SYSTEMTIME taken; int hasTaken;`
  - `src/main.c` → `AddFileItem()`：读 EXIF，失败则回落到文件修改时间
  - `src/main.c` → `ComputePreview()`：加 `MODE_DATE` 分支，
    按 `起始号码/位数` 与“日期格式”（如 `yyyy-mm-dd`）拼新名
  - `src/main.c` → `g_modeName[]`、`CreateMainControls()`、`LayoutParams()`：
    加“按日期编号”单选框与“日期格式”编辑框（`resource.h` 加 `IDC_EDT_FMT`）
- **验收**：一批照片按拍摄时间顺序编号；没有 EXIF 的文件用文件时间，
  不报错、不标红。
- **注意**：EXIF 读取只读文件头若干 KB，不要整文件读入。

### 1.1.3　查找替换支持正则表达式（已完成 ✔）

- **要解决的问题**：`(\d{4})-(\d{2})` 这类批量改写，普通替换做不到。
- **实现路径**：
  - 新增 `src/regex.c` / `src/regex.h`：极简正则引擎，
    支持 `.` `*` `+` `?` `[]` `()` `\1`..`\9`，约 300 行，纯 C
  - `src/main.c` → `ComputePreview()` 的 `MODE_REPLACE` 分支：
    勾选正则时走 `RegexReplace()`，否则走现有 `ReplaceAll()`
  - `src/resource.h` 加 `IDC_CHK_REGEX`；
    `CreateMainControls()`、`LayoutParams()`、`OnCommand()` 同步
- **验收**：`(\d+)` → `No\1` 可用；表达式非法时在状态栏或提示框
  给出“正则表达式有误，请检查括号和方括号是否配对”。
- **注意**：正则替换可能产生非法字符，仍然走 `HasIllegalChar()` 校验。

### 1.1.4　改名方案（预设）保存（已完成 ✔）

- **要解决的问题**：几套常用规则每次都要重新填。
- **实现路径**：
  - `src/main.c` → `SaveSettings()` / `LoadSettings()`：`rename.ini`
    增加 `[Preset1]`..`[Preset9]` 节，字段与 `Options` 节同名
  - `src/main.c` → `CreateMainControls()`：参数区加“方案”下拉框
    （`resource.h` 加 `IDC_CMB_PRESET`）与“存为方案”按钮
  - 新增 `SavePreset(int n)` / `LoadPreset(int n)` 两个函数
- **验收**：存/取/覆盖方案可用；不破坏原有 `Options` 节（老 ini 能继续读）。
- **注意**：方案只存规则与参数，不存文件列表。

---

## V1.2　易用性版

主题：让用户在列表里能“手动插手”，并且把改名这件事接进资源管理器。

### 1.2.1　直接编辑新文件名

- **实现路径**：
  - `src/main.c` → `CreateMainControls()`：ListView 加 `LVS_EDITLABELS`
  - `src/main.c` → `MainProc()` 的 `WM_NOTIFY`：处理
    `LVN_BEGINLABELEDITW` / `LVN_ENDLABELEDITW`
  - `src/app.h`：`FileItem` 增加 `int manual;`
  - `src/main.c` → `ComputePreview()`：`manual` 为真时跳过重算，
    但仍要参与重名与非法字符检查
- **验收**：双击“新文件名”可改；改过的行不再被规则覆盖；
  改完立刻做红色校验。

### 1.2.2　重名自动编号

- **实现路径**：`src/main.c` → `ComputePreview()` 重名检查处，
  冲突时按顺序追加 `(1)`、`(2)`…；由 `rename.ini` 的
  `Options\AutoNumber` 控制，参数区加复选框 `IDC_CHK_AUTONUM`
- **验收**：开关打开后不再出现红色重名；关闭时行为与 V1.0 一致。

### 1.2.3　上移 / 下移调整顺序

- **实现路径**：`src/resource.h` 加 `IDM_EDIT_UP` / `IDM_EDIT_DOWN`；
  `src/main.c` 加 `DoMoveSelected(int delta)`（交换 `g_items` 相邻项，
  再 `RefreshList()` 并恢复选中），菜单与快捷键（Ctrl+↑/↓）同步
- **验收**：选中项上下移动正确，预览随之更新；排序后索引不乱。

### 1.2.4　资源管理器右键菜单（用户主动安装）

- **实现路径**：新增 `src/shellreg.c` / `src/shellreg.h`，
  在 `HKCU\Software\Classes\*\shell\XYRename` 下写命令行
  （`"%1"` 传路径，配合 V1.3 的命令行模式使用）；
  菜单“文件→安装右键菜单 / 卸载右键菜单”
- **注意**：本软件一直宣称“不写注册表”，所以此功能必须：
  ① 由用户主动点选才安装；② 提供一键卸载；③ 在帮助与 README 中
  明确说明“只有您主动选择时才会写入当前用户的注册表”。
- **验收**：安装后右键出现“用小雨批量改名”；卸载后无残留。

### 1.2.5　改名日志

- **实现路径**：新增 `src/log.c`，可选把每次改名追加到
  `XYRename.log`（时间、旧名 → 新名、结果），由 ini 开关控制
- **验收**：日志可用记事本打开，中文不乱码（UTF-8 或 UTF-16LE + BOM）。

---

## V1.3　功能扩展版

主题：从“批量改文件名”扩展成“批量改名工具”。

### 1.3.1　支持文件夹改名

- **实现路径**：
  - `src/app.h`：`FileItem` 增加 `int isDir;`
  - `src/main.c` → `AddFileItem()` / `AddDirectoryItems()`：允许把目录本身加入列表
  - `src/main.c` → `DoRename()`：`MoveFileW` 对目录同样有效，逻辑基本不用改；
    但要禁止对列表项的子目录重复操作
  - `src/main.c` → `RefreshList()`：目录不显示大小，显示“<文件夹>”
- **验收**：文件夹可批量改名；不会误改其内部文件；帮助文档说明清楚。

### 1.3.2　命令行模式

- **实现路径**：`src/main.c` → `wWinMain()` 解析 `GetCommandLineW()`，
  支持 `XYRename.exe /silent /mode:number /prefix:照片 /start:1 /width:3
  /dir:"D:\照片"`；静默执行、写日志、以成功个数作为退出码
- **验收**：可被批处理与计划任务调用；参数错误时给出中文提示。

### 1.3.3　英文界面（多语言）

- **实现路径**：新增 `src/lang.c` / `src/lang.h`，把 `main.c` /
  `help.c` / `about.c` 里的界面字符串抽成两张表，
  按 `GetUserDefaultUILanguage()` 选择；帮助正文对应
  `src/topics.c` 增加一份英文数组
- **验收**：英文系统显示英文界面与英文帮助；中文系统不受影响。

### 1.3.4　帮助文档同步增补

- 每个新功能都要在 `src/topics.c` 与 `帮助文档.txt` 中增加专题，
  并更新目录树 `g_nodes[]`

### 1.3.5　体积与稳定性复查

- 复查目标：EXE 仍控制在 500KB 以内、`-Wall` 无警告、
  一万个文件的预览在 1 秒内完成

---

## 优先级建议

1. 先做 **1.1.1 子文件夹导入** 和 **1.1.4 方案保存**：改动小、用户最常用
2. 再做 **1.1.2 按拍摄日期** 和 **1.1.3 正则替换**：功能价值高，代码量大
3. V1.2 的五项按顺序做，其中右键菜单要配合 V1.3 的命令行模式才有意义
4. V1.3 的三项建议一起发一个大版本

## 暂不考虑的功能（与年代定位不符）

- 云同步、在线更新、账号体系
- 皮肤与主题商店
- 插件市场、脚本语言嵌入
- 移动端、网页版

---

*本文件随开发进度更新。*
