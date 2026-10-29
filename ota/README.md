# if-ink OS 在线升级 / Online updates

稳定通道：`ota/stable.json`。设备只在用户点击“在线升级”时检查，确认后才安装。稳定版本以清单为准；旧 IF 版本和开发测试包不会自动成为稳定通道目标。

Stable feed: `ota/stable.json`. Devices check on request and install after confirmation. The manifest defines the current stable version; legacy IF releases and development builds are excluded.

- 固定仓库编号 `1412628734`，设备清单地址为 `https://api.github.com/repositories/1412628734/contents/ota/stable.json?ref=main`，原始文件 Accept 为 `application/vnd.github.raw+json`。仓库改名后编号不变。
- 应用身份保留 `Read_Pico_IF`；独立系统名称为 if-ink OS。布局 `if-16m-ota-v1`：两个4MiB应用槽、原有OTA数据分区。只升级应用，不改分区表、bootloader或用户数据，不依赖TF卡。
- 固件优先通过同一仓库的不可变Git blob编号直接下载，避开Release CDN；连接或读取中断时在数字asset ID的Release备用线路与原线路间进行有界恢复。限定HTTPS和GitHub资源重定向；校验芯片、项目、版本、大小及SHA256，ESP-IDF验证完整镜像后才切换启动槽。失败可重试，当前运行槽保持；新版本首次启动按既有逻辑确认/回退。
- 首次使用此功能前须先安装包含更新器的新固件。模拟器只预览操作，不执行网络更新或刷写。

Repository ID survives renames. The retained `Read_Pico_IF` image identity and two-slot layout preserve compatibility. Application-only OTA does not alter the bootloader, partition table or user data. HTTPS, image identity, size, SHA256 and ESP-IDF image validation precede activation. Repository content-addressed downloads are preferred over the Release CDN, with bounded transfer recovery using the release asset as a fallback. SHA256 detects corruption; the release account and HTTPS feed remain the trust boundary. No separate signing key is introduced. The simulator never installs firmware.

## 发布稳定版 / Publishing stable releases

1. 用户验收并授权发布后，再制作固件。版本使用 `0.1.0`、`0.1.1` 等稳定编号；`-test.N` 不入稳定通道。
2. 在同一仓库创建对应稳定Release（例如 `v0.1.1`），提供对应源码、许可和应用`.bin`。在线升级不能使用合并USB镜像、ZIP或分区表。
3. 获取该Release元数据（`gh api repositories/1412628734/releases/tags/v0.1.1`），保存到本地JSON。
4. 将与Release相同的应用`.bin`提交到本站在线刷机目录（沿用现有webflash发布流程）；必须是同一份字节，工具自动生成不可变blob编号。运行 `python3 tools/prepare_online_update.py APP.bin --release-json RELEASE.json --asset-id ID --out ota/stable.json`。工具校验实际镜像、版本与GitHub资产SHA256，仅生成清单。
5. 先确认同版首次安装/TF升级包及源码下载可用，再将新清单提交到main；这是向已安装设备开放更新的最后一步。撤回更新可将`available`恢复为false，已安装设备不会降级。

After approval, release a stable application binary with matching source/licenses, fetch that release's metadata, and generate the manifest with the command above. The helper checks image checksum/hash, version, project and the published asset digest. Commit the feed only after downloads/source are verified. Disabling availability withdraws the offer without downgrading installed devices.

下载最多建立六轮连接，整体限时十分钟；短暂读取超时先等待，持续无数据或断线时自动重连。支持Range时校验续传区间；不支持时重新读取并跳过已写入前缀，不重复写Flash或累计摘要。分段擦除空闲应用槽，避免连接建立后集中擦除整个镜像。取消、写入错误或最终校验失败均不激活新槽；不做无限重试或自动降级。临时联网由UI任务回收，升级期间暂停无操作休眠。普通进度最多每五秒在百分比变化时局部刷新条和数字；阶段变化即时显示。
Downloads use up to six connection attempts within ten minutes. Temporary read timeouts wait briefly; interruptions reconnect. Range responses must match the expected offset/size; servers ignoring Range are reread while skipping the committed prefix without duplicating flash writes or hashing. Incremental erase avoids a full-image erase stall on an open connection. Cancellation, flash errors or failed validation never activate the new slot; retries are finite and never downgrade. The UI owns temporary WiFi cleanup and suspends idle sleep during work. Progress updates only the bar/count at most every five seconds when the percentage changes; stage changes appear immediately.
