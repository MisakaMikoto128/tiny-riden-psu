# 第三方来源说明

## 界面内核与验证工具骨架

以下文件来自本地 `tiny-win32-gui` 技能的 `assets/tiny-app` 目录，作者为本项目维护者，与本项目使用同一许可：

- `pc-ui/src/ui.cpp`、`pc-ui/src/ui.h`
- `pc-ui/src/rt.c`
- `pc-ui/tools/shot.c`、`pc-ui/tools/click.c`、`pc-ui/tools/keys.c`、`pc-ui/tools/findwin.h`
- `pc-ui/build.bat`、`pc-ui/env.bat`

本项目在这些文件上的改动：

- `ui.cpp` 与 `ui.h` 新增 `ui_button_col`，支持为按钮指定背景色与前景色。
- `ui.cpp` 新增 Consolas 20 像素文本格式，用于大号读数。
- `shot.c` 改为截取窗口客户区，避免把窗口外内容带入截图。
- `env.bat` 增加已激活构建环境检测，便于在 CI 与开发者命令提示符中复用现有环境。

## 协议资料

寄存器含义参考以下社区逆向资料，仅用于交叉核对，未复制其代码：

- msillano/RD6006-Super-power-supply 的 `RD6006_prtotocol_en.pdf`
- kbralten/device_protocols 的 `rd6xxx.md`
- Baldanos/rd6006
- sigrokproject/libsigrok 的 `rdtech-dps/protocol.c`
- rfinnie/rdserialtool

上述资料均基于 RD6006。本项目的量纲、双量程与预设组步长以 RD6012P 实测为准，差异记录在 [docs/PROTOCOL.md](docs/PROTOCOL.md)。

## 系统组件

界面使用 Windows 自带的 Direct2D、DirectWrite、GDI、User32 与 Kernel32 接口，随系统提供，不进入可执行文件体积。

## 第三方库

无。本项目不链接 C 运行库，不使用任何外部静态库或动态库。
