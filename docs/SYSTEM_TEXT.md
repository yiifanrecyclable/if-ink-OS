# 系统文字 / System text

设置 → 显示 → 系统文字：居中“字号”标题下用 S、M、L、XL 四个正圆按钮选择小、标准、大、特大；居中“字体”标题下选择 TF 卡字体。默认 M，旧百分比值迁移到最近一档，旧设置备份兼容。
Settings → Display → System text: circular S/M/L/XL controls below the centered Size heading; TF font choices below the centered Font heading. M is the default. Map legacy percentages to the nearest preset and retain backup compatibility.

| 字级 / Role | S | M | L | XL |
| --- | --- | --- | --- | --- |
| 标题 / Title | 32px | 38px | 45px | 51px |
| 正文、列表名 / Body and list title | 28px | 34px | 39px | 45px |
| 注释、日期及阅读进度 / Caption, date and progress | 22px | 26px | 31px | 35px |

列表根据当前字体的实际字形高度与字号计算行高、每页数量、分页及点击区域。文档、文件、云端目录、管理列表、设置与字体列表使用同一测量方法；标题按字宽在 UTF-8 边界折行或截断，不为挤进旧行高而缩小标题。首页最近阅读在 XL 档保留三行文档，其余档保留四行，左右标题均单行静默截断；图书分页在 S/M 显示 3×3 封面，L/XL 显示 2×2，封面下标题最多两行且首行位置固定。
Measure actual glyph heights to derive list rows, page sizes, pagination and hit regions. Documents, files, cloud entries, management and settings/font lists share these metrics. Wrap or trim long titles at UTF-8 boundaries without squeezing them into old row heights. Recent home documents reserve three rows at XL and four otherwise; silently truncate home titles to one line. The bookshelf uses a 3×3 grid at S/M and 2×2 at L/XL, with at most two title lines and a fixed first-line anchor.

顶部时间及电量区使用统一固定 28px 字号；首页日期与格言 Banner、内容切换胶囊、底部导航图标保持独立尺寸。首页阅读记录模块坐标固定，月份及星期英文保持标准 34px，七列方块阵列居中。键盘位置与按键格固定，文字在按键可用空间中适配；密集按钮同样按可用空间收敛。阅读正文、目录与阅读工具仍使用独立阅读设置。
All status text uses fixed 28px type. The home date/quote banner, content capsule and navigation icons retain independent metrics. Calendar geometry stays fixed with 34px month/weekday labels and a centered seven-column array. Keyboard position and key cells stay fixed while labels fit available bounds, as do dense controls. Reading content, TOC and tools retain their independent reading settings.

中英文共用一个系统字体，建议使用覆盖中文与拉丁字符的完整字体，不增加两套语言选择。读取已配置的 fonts 目录并兼容 TF 卡 font 目录及 assets/fonts。默认内建界面字库从 TF 标准字库补充动态标题，用户选择优先；重新插卡遵循已有字体恢复机制。
Use one complete CJK/Latin system face. Scan configured fonts, the TF font alias and assets/fonts. The default built-in UI face falls back to the TF standard face for dynamic titles; explicit choices take priority and retain existing remount/reload behavior.

日期与时间 → 状态栏显示：可隐藏日期但始终显示时间，数字日期提供年-月-日、年/月/日、年.月.日、月-日、月/日五种格式；立即应用，备份可保存，旧备份保留当前日期显示偏好。首页日期 Banner 独立。
Date & time → Status display: hide the date independently of time; choose YYYY-MM-DD, YYYY/MM/DD, YYYY.MM.DD, MM-DD or MM/DD. Apply immediately and include preferences in backups; legacy backups retain current status-date preferences. The home date banner remains independent.

## 设置分区 / Settings groups

- 账号与链接：WiFi、坚果云同步。 / Accounts and connections: WiFi and Nutstore.
- 显示与个性化：系统文字、状态栏签名、首页强刷、锁屏壁纸。 / Display and personalization: system text, signature, Home refresh and wallpaper.
- 系统与设备：日期与时间、关机与休眠、电池管理、保存与恢复、固件信息、BOOT刷机。 / System and device: time, sleep/power, battery, backup, firmware information and BOOT flashing.

固件信息保持一屏；本地升级进入既有TF卡流程，返回仍回信息页。在线升级按钮进入独立页面，按需读取本项目GitHub稳定清单并在确认后安装；稳定版未发布时明确提示尚无在线升级版本。来源名称使用上游当前公开名kiikoread（原Pico Reader）。
Firmware information fits one screen. Local update opens the existing TF workflow and returns to the overview. Online update opens the dedicated GitHub stable-feed workflow and requires confirmation before installation; a disabled feed explicitly reports that no online release is published. Attribution uses the upstream public name kiikoread (formerly Pico Reader).

设置页顶部以64px单行显示系统名、版本和右端口号。对比度调节已退役，旧NVS及备份值不再影响显示，备份槽位保留兼容。
Settings uses a compact 64px single-line identity/version/slogan header. Contrast control is retired; legacy values have no visual effect and the backup field is reserved for compatibility.

页面信息分区使用白底、深色分区线(UI_GRAY_SECTION_LINE)及浅色条目线(UI_GRAY_ITEM_LINE)，不再用圆角矩形包裹。选择行可用平直浅灰背景与标记反馈；按钮、输入框、切换胶囊、详情/确认浮层及进度控件保留各自轮廓。
Use white information sections with dark section rules and pale item rules, without rounded enclosing boxes. Selection rows may use flat gray feedback. Functional controls and detail/confirmation overlays retain their outlines.


## 校时与首页刷新 / Time and Home refresh

S3睡眠时钟采用内部振荡器分频源（INT_8MD256）、8190周期校准；板级120MHz Flash/PSRAM配置不变。后台校时仅复用已连接且可用的WiFi STA，空闲十秒后启动，成功后写入PMU，约一小时后再尝试。每次最多等待十五秒，使用零等待轮询，失败至少间隔五分钟；操作、上传、云同步、在线升级和休眠优先。离线不自动启用WiFi。PMU独立走时以及不同温度下的长期漂移需实机记录验证。

首页强刷一次性对整个目标画面执行完整灰阶GC16，包括未选中导航的灰色底，不再先擦白后重画。保留必要的GC16波形相位，不能保证完全无闪烁；光学残影与纵向条纹应在设备上验证，模拟器仅核对绘图和刷新调用。

English: Use the divided internal RTC source with 8190-cycle calibration, retaining board memory timing. Idle background time sync reuses an existing STA, polls without waiting, writes successful time to the PMU and retries hourly after success or at least five minutes after failure. Foreground work and sleep take priority; offline never starts WiFi. Home refresh drives every pixel in one full-grayscale target pass without an extra white preclear. Hardware clock drift and optical artifacts require physical testing.

## 系统语言 / System language

在设置 → 系统与设备 → 日期与时间下方的“系统语言”一行，点击右端当前语言“中文”或“English”即可切换；默认中文，立即生效，重启后保留，并随“保存与恢复”备份。切换只影响系统文案、日期说明和默认输入模式；书名、文件名、文章正文、格言、签名及TF卡字体名称不翻译。英文界面的输入法仍可切回中文拼音。

In Settings → System & device, tap the current language below Date & time to switch between Chinese and English. The choice applies immediately, persists across restarts and is included in settings backups. User titles, file names, document text, quotes, signatures and TF font names remain unchanged. Chinese Pinyin input stays available in the English interface.

维护约定：只有UI_MSG显式标记的系统文案进入集中目录，布局和绘制前解析；格式串与嵌套标签用ui_snprintf处理，不对任意用户文本做词语替换。备份使用原资料字段的新高位128，退休蓝牙位8继续忽略；旧备份恢复为中文，英文备份需此版本或更新版本恢复。

Maintenance: only explicitly marked system messages enter the catalog. Resolve them before layout/painting and use ui_snprintf for templates and nested labels. Bit128 of the existing profile stores English; retired Bluetooth bit8 remains ignored. Older backups restore Chinese; English backups require this version or newer.
