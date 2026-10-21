# 坚果云拉取 / Nutstore download

文件页「传输文件 → 坚果云同步」读取同步目录，按创建时间从新到旧排列；勾选需要的内容，再点「同步所选」。标题下直接显示日期和体积，不带“创建/修改”前缀；日期优先使用创建时间，缺失时采用修改时间。体积显示KB，沿用1024字节换算的软件显示约定。一次仅浏览指定文件夹的直接子项，不递归下载整个云盘。目录状态固定显示在刷新按钮左侧，区分读取中、成功项数、账号错误、文件夹不存在和连接失败；刷新失败/取消保留上次完整列表。设备未连接WiFi单独提示，宿主使用电脑网络，连接失败不冒充硬件WiFi状态。离开、锁屏或拔卡会取消并等待当前请求停止；已成功入库的文件保留。

## 配置

1. 在坚果云创建 `PicoBox` 文件夹；也可使用自己的文件夹名。固件「设置 → 坚果云」可以修改文件夹、账号及应用密码。
2. 使用[坚果云第三方应用管理](https://help.jianguoyun.com/?p=2064)生成的应用密码，账号填写坚果云登录账号。该密码独立于登录密码。
3. 预览网页展开「坚果云连接配置」，填写后点「保存到钥匙串并读取目录」。预览通过电脑网络实际访问坚果云；设备 WiFi 在宿主预览中仍是模型。
4. 真机需先完成WiFi配网并插入TF卡。在「设置 → 坚果云同步」分别点击同步文件夹、账号和应用密码，手动填写并确认。确认后自动保存在设备NVS中，重启后无需重新填写。本版不提供TF卡JSON导入或电脑配置文件生成。
5. 「设置 → 保存与恢复 → 保存当前配置到TF卡」将坚果云文件夹、账号、应用密码与设备设置、WiFi和阅读记录一起保存到TF根目录的 `Pico-settings.backup`。恢复新备份会恢复这些连接配置；未配置坚果云的新备份恢复后也会清除当前坚果云配置。旧版备份不含坚果云配置，恢复时保留当前坚果云信息。

恢复配置前先在TF保存整组旧状态，普通写入失败会回退。如果回退也失败，保留 `.Pico-restore.before`，再次选择恢复配置先处理旧状态；未完成前阻止保存覆盖备份。不要删除这个文件；它与设置备份一样包含明文连接密码。突然断电或拔卡的全自动恢复仍待实体专项验收。

备份包含明文WiFi密码及坚果云应用密码，请妥善保管TF卡及备份文件。设备NVS目前未启用加密。模拟器日常密码仍保存在Mac钥匙串，本地配置和界面快照不含密码；手动点击保存配置才将密码写入设置备份，恢复时写回钥匙串。预览配置与设备配置相互独立。

## 扁平目录与第一阶段纯文本

云端收件目录（默认PicoBox）直接放MD、EPUB、TXT、PNG/JPG/JPEG图片及TTF字体。Ob插件0.1.4将`周记1.md`直接写为`PicoBox/周记1.md`；同名以后一次实际写入覆盖，不按Vault区分，不自动同步修改。插件本阶段只发正文，图片/嵌入经确认转可读说明，不读取或上传附件，不创建attachments目录、清单或编号版本目录；代码中的图片示例仍是文字。PUT发出后即使取消或连接异常也回读准确字节及SHA256，结果不明不报告成功。并发发送以最后实际写入为准，不提供防丢更新承诺。 发送窗口仅显示发送中及成功/失败结论，失败附简短原因；关闭窗口继续发送，完成后通知，不提供取消后续发送按钮。

```text
PicoBox/
  周记1.md
  雪崩.epub
  随笔.txt
  壁纸.png
  思源黑体.ttf
  attachments/       # 手动兼容图片或后续插件图文阶段使用，可暂时没有
```

手动放入和插件推送使用相同下载路径。Pico及预览显示指定目录直属的MD/EPUB/TXT、PNG/JPG/JPEG和TTF，读取列表只需一次PROPFIND，不逐篇下载清单。旧`.pico.json`、`_pico`及其子目录原样保留，不显示、不迁移、不删除；旧插件文章需用0.1.3重新发送才进入新列表。已在TF入库的旧文档仍可阅读。

- 按原文件名自动分类：MD → `markdown/`；EPUB/TXT → `books/`；PNG/JPG/JPEG → `pictures/`；TTF → `fonts/`。扩展名不区分大小写，目录不存在时自动创建图片/字体目录。MD≤256KiB，图片≤2MiB，TXT/EPUB/TTF≤64MiB。目录最多96项受支持文件，不能把截断目录当完整目录。
- 图片下载后可从文件页查看，并在壁纸或Banner中手动选择，不自动改变设置。字体下载后重新进入字体选择页即可找到；当前不接收OTF、TTC、WOFF或WebP。图片做类型/尺寸头校验，TTF做TrueType签名与必要字体表边界校验；这不等于验证每个字形。系统、阅读、Markdown或Banner正在使用的同名字体若内容改变，真机提示先切换其他字体后再下载更新，避免替换流式读取中的文件；相同内容可重复下载。
- 标准强ETag采用条件GET；没有可用强标记、弱标记或坚果云裸标记时，二次读取正文核对字节数/SHA256，内容在下载期间变化就不替换本地旧文件。可比较版本标记不一致仍拒绝；GET省略ETag不再单独造成普通文件无法下载。长度、哈希、TLS证书和主机名校验保留。
- 本地Markdown图片阅读保留。手动MD已有本地图片引用时，云图片须在该目录的`attachments/`，采用设备兼容的16灰阶、非交错4-bit PNG、宽≤684，长图保留高度。只下载正文实际引用的图片，不扫描同名前缀或取旧图；每图≤17MiB、每篇≤32MiB、≤64张、每图≤32Mi像素。其他格式或缺图拒绝图文入库。插件重新接入优化图片留待下一阶段，0.1.3不做转换。
- 下载至暂存后统一验证并提交，失败保留旧文件。图书或MD同名真正更新清阅读进度和书签，保持首次入库日期及置顶；同文同图保持进度。EPUB下载先检查ZIP文件头，实际阅读由原书源解析。
- 一次选择存在同名文件（包括ASCII大小写区别）时提示只选一篇，分次同步以后一次下载覆盖。本地删除不删除云端文件。

## 同步中的屏幕刷新

同步状态和进度最多每秒更新一次，只用GL16差分更新发生变化的像素，不计入普通页面周期性清残影次数。手动强刷、锁屏和显示异常恢复仍可能需要GC16全刷；减少同步黑白闪烁不代表停用所有清残影刷新。

English: Cloud status and progress use GL16 pixel differences at most once per second and do not advance ordinary page-cleanup counters. Explicit refreshes, locking and display-error recovery may still require full GC16 updates.

## 中断恢复与访问边界

云端只使用HTTPS的GET/PROPFIND，校验证书链与主机名；拒绝重定向，不写入或删除云盘。目录上限96项可读内容；达到服务目录限制或本地上限时明确提示拆分文件夹，不能把不完整目录当作完整目录。客户端每30分钟最多450次请求；401/403停止整批，429/503最多两次短暂重试，长期限流提示稍后手动操作。

设备HTTPS连接/响应头最多等待15秒，正文单次读取等待5秒；短暂连接或未完成传输最多重试两次，从头下载暂存文件，不拼接残缺数据。证书验证错误、账号错误、版本变化或哈希不一致不通过重试绕过。失败详情留在文章行和底部状态中；TF卡 `.pico-cloud/last-network-error.txt` 仅记录时间、请求类别、HTTP/TLS错误码及字节数，不记录账号、密码、网址、标题或内容。这是最近一次网络错误记录，不代表每次操作都失败。

TF上先暂存，CRC事务日志记录新旧正文SHA/长度与附件，再完成安装、进度及所有权更新。损坏或丢失的日志、唯一备份、正文不完整、NVS错误等会阻止继续阅读/提交，保留恢复材料；禁止手动删除 `.pico-sync` 来绕过提示。宿主文件/目录持久化和固件FatFS同步不能代替实体SD卡的断电测试。

if.6将日志和索引替换改为FatFs兼容操作，不依赖POSIX的重命名覆盖。完整CRC候选日志先提升再恢复，两份有效日志必须属于同一事务；无法判定时保留材料并报错。phase3只核对已安装文件，不重复重写元数据。恢复结束后再次核对实际目标与附件的长度/SHA256，完成回滚不能计入成功。日期/置顶索引替换中断留下的`.before`由读取或再次更新恢复，不手动删除恢复材料。

## 维护与验证

当前 `tools/test_cloud_host.py` 与 `tools/run_document_host_tests.sh` 使用独立临时卡和NVS，并对TF路径启用不允许重命名覆盖的FatFs替身，不备份恢复真实测试卡。旧 `tools/test_cloud_client.py`、`tools/simulator/test_simulator.py` 的存储回放入口已停用，直接运行或导入都会在操作存储前停止；不要恢复其旧测试方式。`tools/test_cloud_etag.py`在独立临时目录检查坚果云版本串、复选条件及二次内容校验，不读取真实账号、不回滚用户TF卡。`tools/test_cloud_host.py`保留旧清单离线兼容检查，并调用生产事务代码，包含中断、校验失败、EIO、NVS失败和日志损坏。`tools/test_cloud_flat_sync.py`使用内存传输替身、真实目录解析/入库及插件文本导出，验证扁平三格式、更新/无变化、无ETag的二次校验、旧文件忽略及图片引用，不访问真实网络。页面回归使用 `tools/test_if_simulator.py`，输入回归使用 `tools/test_text_input.py` 与 `tools/test_input_pages.py`。

```sh
SANITIZE=1 bash tools/build_cloud_host.sh
ASAN_OPTIONS=detect_leaks=0 python3 tools/test_cloud_host.py
python3 tools/test_cloud_flat_sync.py
python3 tools/test_cloud_flat_device.py
python3 tools/test_cloud_transport.py
python3 tools/test_cloud_etag.py  # 仅隔离loopback服务，不使用真实账号
python3 tools/test_if_simulator.py
```

固件按ESP-IDF v6.1及 `sdkconfig.defaults` 编译；cJSON和Expat版本/hash固定在 `dependencies.lock`。新增解析及网络/存储逻辑按体积优化，rc83 IF采用上游双4MiB程序槽及VCOM_EN扫描门控/2ms稳定等待；保留VCOM数值、PMU写保护和板上120MHz时序。内建字库保留横向字形、度量和kerning，移除渲染器不支持的竖排与替代字形数据；卡上完整字体字节不变。

当前默认固件编译和宿主测试通过。首次真机验收发现Banner物理旋转缓存、输入基线和云端连接/版本复核失败；前两项有明确代码修复，云端与插件的新诊断仍需真实账户复测，不能以离线测试代替链路验收。Pico无线/TLS内存、SD断电恢复和灰阶显示仍需验收；程序分区余量较小，新增功能必须继续检查体积。本开发版尚未发布。

English: Configure a dedicated Nutstore application password and an editable folder (default PicoBox). The desktop preview uses real read-only HTTPS and macOS Keychain; firmware uses its own WiFi/NVS configuration. Plugin 0.1.4 writes text-only original MD names directly with last-writer replacement and exact readback. Production C lists direct MD/EPUB/TXT, ignores legacy manifests without deleting them, and validates images/recoverable TF commits. Missing/weak validators require two matching body hashes. Host regressions and an ESP-IDF v6.1 defaults build are available; account and physical-device acceptance remain pending. Preserve matching dependency notices when distributing binaries.


## 共用WiFi启动策略 / Shared WiFi startup

WiFi传输、坚果云同步、在线升级和显式临时校时共用网络层。需要新建连接时先扫描附近网络，只从已保存的网络中选择认证方式兼容的候选，按信号由强到弱连接；同信号优先最近使用的网络。扫描匹配读取完整结果，不受设置页最多显示16个SSID限制。同名多AP合并为一个候选，使用最强信号。连接失败或断开后轮换候选，仍遵守一轮最多五次、连接阶段总计30秒的上限；扫描失败或没有可用候选时结束，不盲连历史第一项。

在设置里点选某个网络（包括新输入密码）时，本次连接严格采用该网络；成功获得IP后，将该网络更新为最近使用。唤醒恢复联网会话时重新匹配环境。已在线的STA供其他功能复用，不因打开另一页面而扫描切网，热点模式不参与自动选择。连接错误后的下一次功能启动可以清理失败会话再重新扫描；上传尚未结束时拒绝清理。没有配置网络和附近没有已保存网络分别提示。

网页模拟器不扫描或控制电脑的真实WiFi；自动选网、射频和跨环境效果以真机验收为准。可用 `python3 tools/test_wifi_autoconnect.py` 在无网络、无设备条件下复跑生产连接代码的边界故障测试。

English: Network features share one startup path. Scan before connecting, match compatible saved profiles, rank by RSSI and recency, deduplicate APs, and rotate failed candidates within the existing five-attempt/30-second connection budget. Explicit choices apply to the requested startup; successful connections update history and sleep recovery scans again. Reuse online STA sessions, preserve AP mode and active uploads, and allow a failed idle session to restart. The browser never controls real WiFi; hardware acceptance is still required.
