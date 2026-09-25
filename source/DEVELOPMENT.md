# 构建与验证

用户安装、使用与排错请阅读根目录 README。

- `hook.cpp`：词典查询、Qt 样式/绘制/测宽适配、控件生命周期、语言菜单和手动采集。
- `launcher.cpp`：定位宿主、接口预检、DLL 加载和启动失败回退。
- `qt_compatibility.h`：安装器与启动器共享的 Qt 5.12+ x64 组件检查。
- `hotkey_config.h` / `hotkey_settings.h`：快捷键持久化与设置窗口。
- `inno/`：安装包、目录及进程校验、升级备份清理。
- `translations/`：正式词典。提取脚本只辅助离线整理，不随插件运行。
- `tests/`：核心逻辑回归测试。临时产物和诊断记录位于 `build/`。

构建需要 Visual Studio 2022 Build Tools、共享目录 `_ThirdParty/Qt/5.12.5/msvc2017_64` 和 Inno Setup 7.1。根目录执行 `build.bat --no-pause`，安装包输出到 `dist/TopoGunChineseInstaller.exe`。

运行 `source/tests/run.bat` 验证全部词典及变体查询、采集筛选、F1–F24 ShortcutOverride、配置保存与损坏回退，以及缓存淘汰、重载、线程隔离与归一化冲突。`source/tests/run_perf.bat` 测量重复查询的微基准；结果不代表宿主帧率。运行 `python source/inno/package.py --test-mode` 生成独立安装测试包；只安装到 `build/inno/test-host`，不能用测试包部署正式目录。

查询缓存按线程独立，使用 QCache 的 LRU 淘汰。普通结果成本预算 1 MiB、省略文本结果 256 KiB；成本按 UTF-16 源文与译文长度加每条 128 字节元数据估算，不是进程实际内存上限。每段文本超过 1,024 个 UTF-16 单元仍正常查询，但不缓存。缓存键和值拥有独立紧凑存储，不保留调用方原始缓冲。词典只在启动时加载；若将来增加重载，必须确保加载与查询不并发，代次标记只负责缓存失效，不负责同步词典写入。

Qt 接口预检只证明导入符号可解析，不验证私有 ABI、布局语义或宿主行为。绘制和测宽 Hook 应在实际 TopoGun 版本下验证。可选的延迟模块扫描默认关闭，保留代码作为特定版本的适配后备，不应为常规运行开启持续轮询。

本轮审核修改不迁移或删除正式词典，不改变已有宿主模型和控件文本。完整实机交互与长时间内存/帧率测试仍需人工验收。

版本统一在 version.h 中维护，当前固定为用户指定的 1.0.0，未经授权不自动递增。人工翻译来源与批次存放在 translation_review.zip，不参与运行或打包。UI 后台检查入口为 tests/run_ui.bat；普通测试入口为 tests/run.bat。
