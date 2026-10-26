# 阅读打卡 / Record

首页“阅读打卡”标题行右侧点击“更多”，或长按整个打卡区域，进入按日详情。日期从近到远连续列出，无阅读的日期显示零；屏幕左右箭头或实体左右键翻页，左上返回或圆形实体键回到首页。不提供按周切换。

Tap More beside Record on Home, or long-press the record area, to open daily details. Dates run newest first, including zero-reading days. Use the on-screen arrows or physical side keys to paginate; Back or the middle key returns Home. There is no weekly view.

详情右上角“目标设定”（English: Target）可选择每日30、60、90、120、150或180分钟，默认90分钟。选择后立即保存并返回列表；目标随设置备份恢复。首页热力图达到目标时显示最深色并叠加灰色对勾，月初“1”位于格子左上角；详情横条达到目标时填满；超过目标后图形饱和，右侧仍显示真实累计分钟数，不换算小时。改变目标会统一调整历史记录的显示刻度，不改动已记录的阅读时长。

Target offers30,60,90,120,150 or180 minutes per day, default90. Selecting an option saves it immediately and returns to the list; the setting is included in configuration backups. Home's heatmap reaches its darkest shade with a gray check, and daily bars fill at that target. First-of-month labels stay in the upper-left corner. Graphics saturate above target while labels retain actual accumulated minutes. Changing the target rescales historical displays without altering recorded time.

横条两端保持圆角；行高、文字及每页行数适配系统字号。没有可靠日期时不编造每日数据。

Bars retain rounded ends; text, row height and page capacity follow system type size. Invalid clocks do not create dated records.

## 保存 / Storage

- 长期记录在 TF 卡的 `.readpico/reading/YYYY.bin`。不按固定天数淘汰，每个日历年最多约 12 KB 数据；实际容量取决于 TF 卡空间。每页只读取所需日期，同年共用文件句柄，内存占用不随历史增长。
- 沿用原系统近期 30 天 NVS 记录并自动迁入归档；已经被旧系统覆盖的更早记录无法补回。缺卡或写卡失败时仍保留近期记录，后续补存；不应长期在无卡状态下依赖完整归档。
- 每日累计值使用双副本与校验；合并相同累计值不会重复计时。首页热力图读取 TF 归档和 NVS 近期记录的较大值，重置设置不会抹掉卡内历史。
- 备份完整历史时，复制 `.readpico/reading` 文件夹。设置页的配置备份仍只包含原有近期统计，不包含这些 TF 文件。

Long-term records live in `.readpico/reading/YYYY.bin` on TF, without age-based eviction. Each year uses at most about 12 KB of data, subject to card capacity. Reads are paged and memory stays bounded. Existing 30-day NVS totals migrate automatically; already-overwritten dates cannot be reconstructed. NVS provides a recent recovery window while the card is absent or unwritable. Checked dual copies and idempotent cumulative merges reduce interruption damage and prevent duplicate migration. Home merges archive and recent totals, including after settings resets. Copy the history directory to back up the full archive; configuration backups retain only their existing recent statistics.
