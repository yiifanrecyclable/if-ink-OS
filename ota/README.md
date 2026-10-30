# if-ink OS 在线升级 / Online updates

稳定通道：`ota/stable.json`；测试通道：`ota/beta.json`。进入在线升级页默认检查稳定版，点击“更新通道”右侧文字切换稳定版/测试版并重新检查，确认后才安装。下载、校验或激活期间不能切换通道。测试版不会混入稳定通道；没有已发布测试版时显示暂无版本。通道选择仅在本次在线升级页内有效，下次进入仍默认稳定版。

Stable feed: `ota/stable.json`; beta feed: `ota/beta.json`. Entering the updater checks stable by default. Tap the channel value to switch and recheck; installation always requires confirmation. Switching is disabled while busy or activated. An empty beta feed offers no installation. Reentering the page defaults to stable; prereleases never enter the stable feed.

- 固定仓库编号 `1412628734`，设备清单地址为 `https://api.github.com/repositories/1412628734/contents/ota/stable.json?ref=main`，原始文件 Accept 为 `application/vnd.github.raw+json`。仓库改名后编号不变。
- 应用身份保留 `Read_Pico_IF`；独立系统名称为 if-ink OS。布局 `if-16m-ota-v1`：两个4MiB应用槽、原有OTA数据分区。只升级应用，不改分区表、bootloader或用户数据，不依赖TF卡。
- 固件优先通过同一仓库的不可变Git blob编号直接下载，避开Release CDN；连接或读取中断时在数字asset ID的Release备用线路与原线路间进行有界恢复。限定HTTPS和GitHub资源重定向；校验芯片、项目、版本、大小及SHA256，ESP-IDF验证完整镜像后才切换启动槽。失败可重试，当前运行槽保持；新版本首次启动按既有逻辑确认/回退。
- 首次使用此功能前须先安装包含更新器的新固件。模拟器只预览操作，不执行网络更新或刷写。

Repository ID survives renames. The retained `Read_Pico_IF` image identity and two-slot layout preserve compatibility. Application-only OTA does not alter the bootloader, partition table or user data. HTTPS, image identity, size, SHA256 and ESP-IDF image validation precede activation. Repository content-addressed downloads are preferred over the Release CDN, with bounded transfer recovery using the release asset as a fallback. SHA256 detects corruption; the release account and HTTPS feed remain the trust boundary. No separate signing key is introduced. The simulator never installs firmware.

稳定版使用严格递增版本。测试版使用 `0.1.8-beta.1`、`0.1.8-beta.2` 等编号；同一基础版本的正式版高于测试版。已安装测试版的用户可以手动选择稳定通道返回较早稳定版，界面明确提示版本号降低并再次确认；已安装正式版时不提供旧稳定版或旧测试版。此功能不会自动降级或清除用户数据；测试版新增的数据结构需保持与当前稳定版兼容，不能承诺任意未来测试版的数据都可无损回退。

Stable versions advance strictly. Betas use `0.1.8-beta.1`, `0.1.8-beta.2`, etc.; the final release supersedes its betas. An installed beta may return to an earlier stable release only through an explicitly confirmed stable-channel installation. Installed final releases cannot move to older stable or beta versions. Updates never auto-downgrade or erase data. Beta data formats must remain compatible with the current stable release; arbitrary future format rollback is not guaranteed.

## 发布稳定版 / Publishing stable releases

1. 用户验收并授权发布后，再制作固件。版本使用 `0.1.0`、`0.1.1` 等稳定编号；`-test.N` 不入稳定通道。
2. 在同一仓库创建对应稳定Release（例如 `v0.1.1`），提供对应源码、许可和应用`.bin`。在线升级不能使用合并USB镜像、ZIP或分区表。
3. 获取该Release元数据（`gh api repositories/1412628734/releases/tags/v0.1.1`），保存到本地JSON。
4. 将与Release相同的应用`.bin`提交到本站在线刷机目录（沿用现有webflash发布流程）；必须是同一份字节，工具自动生成不可变blob编号。运行 `python3 tools/prepare_online_update.py APP.bin --release-json RELEASE.json --asset-id ID --out ota/stable.json`。工具校验实际镜像、版本与GitHub资产SHA256，仅生成清单。
5. 先确认同版首次安装/TF升级包及源码下载可用，再将新清单提交到main；这是向已安装设备开放更新的最后一步。撤回更新可将`available`恢复为false，已安装设备不会降级。

After approval, release a stable application binary with matching source/licenses, fetch that release's metadata, and generate the manifest with the command above. The helper checks image checksum/hash, version, project and the published asset digest. Commit the feed only after downloads/source are verified. Disabling availability withdraws the offer without downgrading installed devices.

下载最多建立六轮连接，整体限时十分钟；短暂读取超时先等待，持续无数据或断线时自动重连。支持Range时校验续传区间；不支持时重新读取并跳过已写入前缀，不重复写Flash或累计摘要。分段擦除空闲应用槽，避免连接建立后集中擦除整个镜像。取消、写入错误或最终校验失败均不激活新槽；不做无限重试或自动降级。临时联网由UI任务回收，升级期间暂停无操作休眠。普通进度最多每五秒在百分比变化时局部刷新条和数字；阶段变化即时显示。
Downloads use up to six connection attempts within ten minutes. Temporary read timeouts wait briefly; interruptions reconnect. Range responses must match the expected offset/size; servers ignoring Range are reread while skipping the committed prefix without duplicating flash writes or hashing. Incremental erase avoids a full-image erase stall on an open connection. Cancellation, flash errors or failed validation never activate the new slot; retries are finite and never auto-downgrade. The UI owns temporary WiFi cleanup and suspends idle sleep during work. Progress updates only the bar/count at most every five seconds when the percentage changes; stage changes appear immediately.

本地待验证修复：升级擦写Flash与显示扫描共用输出互斥锁，仅包住OTA begin/write/end/boot/abort，不包住网络等待；进度仍每五秒最多更新一次，状态变化限制在内容区域。扫描欠载的全屏恢复仍保留，不能把未知物理帧继续当作正常画面。 / Pending hardware validation: serialize OTA flash mutations with display output without locking network waits, retain five-second progress updates and bounded status redraws. Preserve full recovery for genuine scan underruns.

## 发布测试版 / Publishing beta releases

1. 用户授权发布后使用 `0.1.8-beta.1` 等编号构建，应用、源码与许可对应同一次构建。
2. GitHub Release 必须标记为 Pre-release，不设为 Latest；稳定版继续保持 Latest。
3. 验证应用资产与不可变 blob 后，运行 `python3 tools/prepare_online_update.py APP.bin --release-json RELEASE.json --asset-id ID --channel beta --out ota/beta.json`。工具拒绝通道、版本、Release标志或摘要不一致，禁止把测试清单写入 `stable.json`。
4. 仅更新 `ota/beta.json`，保留 `ota/stable.json` 和稳定版网页默认下载不变；正式发布时只更新稳定通道，不必同步生成新的测试版。两个通道可以分别保留不同版本。
5. 暂停测试通道时把 `available` 设为 false；不要为填满两个通道而把同一正式镜像改称测试版。

After approval, publish a matching beta image/source/license release with GitHub Pre-release enabled and Latest disabled. Verify the asset/blob, then generate only `ota/beta.json` using `--channel beta`. Channel/version/release-flag/digest mismatches are rejected. Keep stable.json and default website downloads unchanged. Stable and beta publication are independent: maintain the latest approved build of each, rather than duplicating a final build under a beta name. Disable beta availability to withdraw it.
