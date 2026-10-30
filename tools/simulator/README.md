# Pico 宿主模拟器

在仓库根目录启动（macOS，需要 Command Line Tools 的 cc 和 Python 3）：

```sh
python3 tools/simulator/server.py --port 8765
```

在聊天旁的浏览器面板打开 http://127.0.0.1:8765/。

当前重点是 rc83 原有使用界面（含微读与蓝牙设置）：四个主页面、阅读工具、内容管理、应用与存储卡管理、设置子页、配网和传输、图片、系统菜单、启动与锁屏。界面选择器可直接查看各页；屏幕支持点击、长按和滑动，屏幕外三个按钮调用原固件的触控键回调。

基线界面直接编译 main/apps 的绘制和手势逻辑，以及真实 TXT/EPUB/Markdown 书源、分页、字体、阅读进度和设置代码。屏幕使用 684 × 1216、16 级灰度原生帧；底栏为 Lucide 图标的「首页、内容、应用、设置」，无文字、80像素高，选中项白底黑图标，其余灰底白图标。应用页仅有存储卡管理、WiFi/热点、USB连接、坚果云同步、数字拼豆五行。存储卡管理进入完整目录浏览；数字拼豆直接运行真机C状态机、绘图和PNG导出代码，输出仍限制在模拟卡。

图书固定三行三列纯封面，每本下方先显示无数字进度横条，再显示书名；设置页和预览选择器的书架样式入口已移除。

图书长按详情为原书架上的浮动卡片，保留当前分页；卡片新增置顶/取消置顶，置顶优先显示、重启保留并随设置备份。打开卡片时背景控件不响应，点击卡片外关闭。

内容页图书/文档右上角为管理与排序，传入统一在文件页；文档取消星标，仅保留单篇置顶，旧星标筛选不再隐藏内容。图书/文档管理页统一列表、来源/排序/独立搜索和居中分页；图书/文档长按显示共用格式的详情卡，文档置顶仅限单篇长按，批量提供选择、重新扫描、清进度与删除。两页排序界面统一、偏好分别记忆，图书收藏/文档星标入口和筛选取消，仅保留置顶；旧标记和筛选位兼容保留但不影响列表，排序随设置备份恢复。[规则与安全隔离测试](../../docs/CONTENT_LIBRARY.md)。

设置页「无线连接」顺序为 WiFi、坚果云同步；坚果云同步进入独立配置页，可设置文件夹、账号和应用密码。模拟器进入该页时也展开屏幕下方的连接配置表单，沿用Mac钥匙串保存。真机只支持手动填写，本版移除TF配置导入和电脑配置下载。设置页保存与恢复将坚果云配置一起备份；模拟器只在用户保存时通过私有进程管道读取密码并写入TF设置备份，恢复时写回钥匙串，公开快照和普通配置文件不含密码。备份文件含明文WiFi及坚果云密码，请妥善保管。

首页上方为大字日期和固定格言banner，三行分别为公历/星期/离线农历、小字品牌（两侧居中细横线整体略窄于模块分割线）、大字自定义文字（默认「我将宇宙随身携带」）。长按打开「首页文字」设置，上方为与首页同尺寸的实际banner预览，下方依次为修改文字、选择字体、修改字号滑块和四种排版按钮（左对齐、右对齐、居中、两端对齐）；前两行固定居中，格言默认居中，选择排版后更新实际预览并保存。两端对齐每行铺满，英文优先扩词距，单字居中。文字/字体修改后回到预览页，「完成」返回首页。文字、字体、字号与排版偏好随设置备份，留空隐藏，不显示长按提示。日期字号按最大日期长度适配单行；格言默认96px，滑块支持48–120px、2px步进、最多自动折行两行，保存、选择字体或调整字号时按实际字宽/墨迹校验。滑块拖动期间只更新控件，松手后更新预览并保存一次；无效字号保留原值；超出时保留编辑内容提示缩短，前三行锚点不移动。三行视觉间距统一14px，整体居中偏上。阅读记录位于中部，最近阅读贴近下方导航栏。首页「阅读记录」为四行七列的周历热力图：星期标注MON到SUN，最下一行是本周；方块为直角，列间距收紧至12px，包含每月1号的周行左侧标注月份英文缩写；对应月初方块右上四分之一区域显示小数字「1」，无阅读/未来日白底用黑字，填色灰阶0–7用白字、8–15用黑字，抗锯齿与实际底色混合；无外层边框、灰度图例和角落日期，完整日期与时间统一显示在屏幕左上角（YYYY-MM-DD HH:MM）；今天标记短横线，未来日期与已过去且未读的日期统一极浅灰框白底。有阅读秒数即填色并取消描边，按面板离散灰阶递进，120分钟及以上保持深灰。标题右侧显示「本周已读xx小时xx分」，只累计本周一到今天，不计此前三周；真实固件使用持久阅读记录与同一UTC+8日期快照，时钟无效时提示设置时间。预览使用明确标注的30天模拟历史，面板下方仍可调整最近七天数据。

## 测试 TF 卡

项目根目录的「TF卡」是真实文件夹，与 read-pico-reader 仓库并列；它映射为模拟设备的 /sdcard。默认放入原创 TXT/EPUB/Markdown、5000像素长图及上游灰阶测试图及上游字体子集，已有同名文件不会被初始化覆盖。

```text
PICO Firmware/
├── TF卡/
│   ├── books/       TXT、EPUB
│   ├── pictures/    PNG、JPG
│   ├── fonts/       阅读器可选择的 TrueType TTF 字体
│   ├── markdown/   MD 文档（首次入库日期保存在隐藏 .index）
│   │   └── attachments/ 16级灰度4-bit PNG 插图
│   └── notes/       测试移动的目标目录
└── read-pico-reader/
```

放入测试文件后点击「重新载入 TF卡」。模拟器里的删除、复制、重命名和移动会修改这个测试目录，请使用测试副本。宿主文件适配器限制写入测试卡和模拟器缓存，并拒绝路径穿越与符号链接。

模拟屏幕支持左键短点、按住左键500毫秒长按、右键直接长按，以及拖动滑动。长按在按住时触发，松手不会重复点击；移动超过真机容差会取消长按。右键在模拟屏幕内打开固件的长按操作，不弹出浏览器菜单。长图放大状态的按住拖动保留，移动完松手不会误退出；失去指针捕获或窗口焦点会取消等待中的长按。

界面操作从相同的本地状态重放，避免重复渲染导致重复翻页、复制或切换设置。若目录被外部修改，重放会暂停并提示重新载入，不覆盖外部更改。设置、进度与内部存储在被忽略的 build/simulator 下；测试卡不在构建缓存中，普通重编译不会删除它。

## 当前验证范围

- 原版页面与典型连续操作有宿主回归检查，命令见下方。
- 电量、容量、网络扫描、热点和 USB 使用硬件模型；地址与 SSID 是演示数据，配网不连接实际网络。
- 图书/图片应用的 PNG/JPG 用 macOS ImageIO 解码；Markdown 插图用与固件一致的有界 C/zlib 灰阶 PNG 解码器。
- 界面目录底部保留三个新功能原型：自定义首页、坚果云示例目录和同步 MD 列表。原型目录仍只复制本地样例；文件页「坚果云同步」、工具栏「坚果云」和界面选择器「坚果云同步」使用实际只读HTTPS客户端。配置/存储/协议边界见[坚果云拉取](../../docs/CLOUD_SYNC.md)。
- 「内容 → 文档」读取 TF卡/markdown，列表显示文件名、首次入库日期和进度圆环。长按/右键打开「文档详情」浮动卡片，支持置顶/取消置顶、清进度、删除和关闭；置顶优先，其余按所选规则排序。星标入口与筛选已取消。标记存本机.index中的独立.prefs，保持首次日期与阅读进度，同名更新仍保留，删除时清理。删除仍须第二次确认。标题、列表、引用、图片进入共用原生阅读器；大纲箭头展开、标题跳转。书签、字体、统计与刷新工具沿用图书实现。
- 插图为非交错4-bit灰阶PNG；普通图等比例适配，长图显示最宽64像素的窄条，点按满宽放大、按住上下拖动、再点按退出。解码逐行写隐藏 .cache，视口RAM不随源图高度增长。
- MD 限UTF-8、256KiB、64张图片。文件名是文档身份；同名更新保留首次日期，重置进度和书签，按旧明确清单清理独占附件与缓存；同文同图 SHA256 相同保持进度。缺附件/格式或CRC不合法的网页导入保留旧正文。文件页删除与内容页长按确认删除会清理关联附件；外部删除在进入文档页扫描时核对。
- 「打开MD」导入TF卡的markdown；关联图须预先放入attachments。浏览器只导入本地文件，坚果云v1/v2下载共用生产C校验与TF事务。
- 内建Noto Sans SC Medium为覆盖静态界面的1688字符子集/约347KB，程序分区预留给rc83新增功能。标准黑体资源覆盖30890个码位/约10.53MB，放在TF卡/fonts/PicoSans-Medium.ttf，自动补全系统标题缺字，并作为未自选字体时的正文默认字体。[来源、OFL与校验](../../assets/fonts/README.md)。模拟器首次准备标准字体后不会反复恢复用户删除/替换的同名文件，已有Hei.ttf与自定义字体也不会被覆盖。任意Unicode/emoji并非此字体全部覆盖；CFF轮廓OTF不是当前加载器支持的格式。
- 系统字体在「设置 → 系统字体」选择；正文另在「阅读工具 → 字体设置 → 选择字体」选择，分别保存。宿主适配同时映射 stdio 与 POSIX open，确保列表中能看到的卡上TTF能被实际加载；诊断状态的 font_path/font_builtin 表示实际加载结果，而不是仅表示选中的名称。

该工具验证界面与交互。墨水屏波形、残影、传感器、功耗、无线/TLS内存峰值和 USB 磁盘独占仍需真机验证。

## 回归检查

rc83 IF 推荐使用独立临时TF/NVS回归，不读取真实账号，也不回滚用户TF卡：

```sh
bash tools/simulator/build.sh
python3 tools/test_if_simulator.py
python3 tools/test_banner_preview.py
bash tools/run_markdown_host_tests.sh
bash tools/run_document_host_tests.sh
bash tools/build_cloud_host.sh
python3 tools/test_cloud_host.py
bash tools/run_book_host_tests.sh
python3 tools/test_weread_port.py
```

`test_if_simulator.py` 检查68个原生页面、MD/TXT阅读、完整字体覆盖、无字80px导航、坚果云设置和无卡字体回退；仍由人手动核对屏幕美观。旧rc58 `test_simulator.py` 使用备份恢复存储且断言旧布局，不作为当前IF验收入口。

微读二维码与书架为明确的宿主演示；模拟器不登录微信、不下载微读图书。字体和卡文件读取沿用生产代码。普通JPG采用ImageIO，真机的渐进JPEG采用上游缩略解码，效果需真机比较。

`node --test tools/simulator/test_pointer.cjs` 单独检查网页鼠标处理器：短点、500毫秒按住触发与不重复、移动/取消/失焦、右键与触屏菜单去重、指针隔离、长图最终拖动位移。9项检查采用确定性时钟，无需停止预览或修改TF卡。


宿主 QR 适配使用 Nayuki QR-Code-generator 的 MIT 实现，保留原始授权头，不替代固件端 QR 组件。

`PYTHONDONTWRITEBYTECODE=1 python3 tools/test_cloud_settings_backup.py` 使用临时TF卡和假钥匙串验证实际原生备份生成、恢复与损坏拒绝，不触碰真实账号和测试卡。

统一输入界面固定贴底，输入时隐藏主导航；本机TF卡已放入可选词组资源，完整操作及可复跑的私有TF测试见[输入说明](../../docs/TEXT_INPUT.md)。

Input screens share the bottom-anchored keyboard and hide main navigation. The local TF card has the optional phrase pack; see [behavior and isolated tests](../../docs/TEXT_INPUT.md).

宿主ImageIO解码已移除多余上下翻转，PNG/JPEG封面和内页插图与真机保持同一行序；验证使用合成四象限图，未改动真机解码器。

`test_banner_preview.py` 使用临时TF/NVS，逐像素比较设置预览与首页banner，并验证默认/独立/缺失字体、字号拖动与松手保存、取消、两行超限拒绝、四种排版位置/持久化、中文扩距与英文词距、单字居中及编辑返回。成功保存后的模拟事件清除重放起点，避免重绘重复提交。

本版彻底移除蓝牙翻页器设置/扫描页、状态栏图标、输入映射及蓝牙编译依赖；旧原生页面17/18回到设置主页，旧备份蓝牙位忽略，其他设置仍恢复。无线连接区仅WiFi/坚果云，其余分组向上收紧一行。

文档阅读的字体面板新增「标题与正文字号 · 效果预览」，四个独立滑块共用固件真实排版与字体，拖动仅预览、松手保存、取消恢复。`tools/test_markdown_styles.py` 使用隔离TF/NVS验证三级标题、正文独立设置、重启保留、图书不受影响和粗体/斜体/灰底高亮的真实像素。 / The Markdown font panel adds four production size sliders with draft/commit/cancel preview; the isolated native test checks persistence, independent book sizes and actual emphasis/highlight pixels.

模拟器跨页面打开最近阅读内容会按已保存的文字位置续读；每帧独立进程会在退出前保存当前进度，真机保存节奏不变。`tools/test_home_resume.py` 在私有TF/NVS上测试完整Python路由和C阅读器，包括文档/图书、两个入口重复续读及未读内容从头开始。 / Cross-app recent reads resume saved text offsets, persisting host progress between frame processes without changing hardware save cadence; the isolated integration test covers real routing, books/documents, repeat entry and unread defaults.

Markdown 表格沿用正文字体/字号：窄表按列、宽表纵向展开，长表续页重复表头。`tools/test_markdown_tables.py` 使用私有TF/NVS检查真实字体、逐页显示、字号调整及续读；示例与检查图输出在 `build/markdown-table-preview/`。 / Markdown tables use body typography, grids for readable widths, stacked wide fields and continuation headers. The isolated native test covers fonts, pages, reflow and resume; sample/screenshots live in `build/markdown-table-preview/`.

if-ink OS v0.1.0开发预览：settings-5为合并系统文字，settings-24为电池管理；移除字号独立页和阅读票根锁屏，旧6/8页分别兼容跳到文字/壁纸。电量曲线仅在宿主明确启用PICO_SIM_BATTERY_DEMO时展示标注的模拟数据，不写入历史文件；网页设置中的电池页启用该演示。 / Development preview: combined System text and Battery management; legacy pages 6/8 redirect to text/wallpaper. Explicit host battery demo data is labeled and never persisted; the web battery preview enables it.
