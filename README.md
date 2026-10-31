# if-ink OS

**稳定版 v0.1.7 · 测试版 v0.2.0-beta** · 支持 Markdown 文档的 Pico 固件。仅适用 MindReset Read Pico RDP-G01-W。

[在线刷机与下载](https://yiifanrecyclable.github.io/if-ink-OS/) · [版本与离线包](https://github.com/yiifanrecyclable/if-ink-OS/releases/tag/v0.1.7) · [安装说明](docs/安装说明.md) · [构建与重新链接](BUILDING.md)

- EPUB、TXT、Markdown 离线阅读，支持图片与文字混排、表格、标题字号、粗体、斜体与高亮。
- 坚果云 WebDAV 获取文件列表，按需下载 MD/TXT/EPUB，保存到 TF 卡。
- 自定义 Banner 首页、每日阅读打卡与目标设定、壁纸轮播、最近阅读，以及统一字号、字体和分割线界面。
- 阅读控制面板、书签、目录、可选快捷手势、WiFi 密码记忆、中文/English系统界面及独立在线升级。
- WiFi／热点网页可编辑 Banner 与锁屏、预览实际设备排版、同步电脑TTF字体并管理TF文件。

首次安装用电脑 Chrome/Edge 打开在线刷机页；已有 IF/if-ink OS 的设备可使用 TF 升级包。在线安装不需要 Python。离线 USB 包包含固件及刷写参数，手动刷写需提前准备 Python 和 esptool。TF 卡固定目录为 `books`、`fonts`、`markdown`；首次可下载字体与拼音词库资源包。含更新器的版本可在“设置 → 固件信息 → 在线升级”检查后续版本。同版本无需重复安装。0.1.7已通过完整构建与宿主检查，本版新增功能的最终墨水屏与休眠效果仍需真机验证。

Obsidian 推送插件为独立项目，本固件发布包不包含插件及任何账号配置。坚果云的应用密码在设备设置中手动填写。

基于 [Pico Reader 0.3.3-rc83](https://github.com/wegooo-cell/read-pico-reader)，选择性采用后续 rc88/rc95 的阅读、刷新与存储改进；保留 if-ink OS 的界面与刷新策略。感谢[原作者主页](https://wegooo-cell.github.io/read-pico-reader/)。本项目是独立衍生版本，不是原作者或 MindReset 官方发布。

主体 [Apache-2.0](LICENSE)；epdiy 使用 LGPL-3.0-or-later，其他组件及字体保留原许可。每版同时提供对应源码、固定依赖、实际构建配置和许可包，参见[第三方声明](THIRD_PARTY_NOTICES.md)。

### Reading progress storage

With an available SD card, resume positions, percentages and recent ordering live in the hidden `.readpico/progress` directory. Keep it when copying or backing up the card. Records use CRC-checked dual slots with sync and readback verification; write failures remain pending. Legacy NVS records remain readable and subsequent updates go to SD; without SD, NVS remains the fallback. Settings backup/restore includes the new store and accepts older backups. Unsaved positions already lost after a restart cannot be recovered.
