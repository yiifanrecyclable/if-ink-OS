# 阅读记录 / Reading history

首页长按“阅读记录”区域进入，顶部切换“按日”和“按周”。日期从近到远连续列出，无阅读的日期显示零；周以周一至周日汇总，当前周只计截至今天的时间。屏幕左右箭头或实体左右键翻页，左上返回或圆形实体键回到首页。

Long-press Home's reading history to open daily or weekly rows, newest first. Dates are continuous, including zero-reading days. Weeks run Monday–Sunday, with the current week counting only through today. Use on-screen arrows or physical side keys to paginate; Back or the middle key returns Home.

横条两端为圆角，按当前页最长时长比较；右端日视图用累计分钟，周视图用小时与分钟，不显示不足一分钟的秒数。行高、文字及每页行数适配系统字号。没有可靠日期时不编造每日数据。

Round-ended bars compare durations against the longest row on the current page. Daily totals use minutes; weekly totals use hours and minutes, omitting remaining seconds. Typography, row height and pagination adapt to system type size. Invalid clocks do not create dated records.

## 保存 / Storage

- 长期记录在 TF 卡的 `.readpico/reading/YYYY.bin`。不按固定天数淘汰，每个日历年最多约 12 KB 数据；实际容量取决于 TF 卡空间。每页只读取所需日期，同年共用文件句柄，内存占用不随历史增长。
- 沿用原系统近期 30 天 NVS 记录并自动迁入归档；已经被旧系统覆盖的更早记录无法补回。缺卡或写卡失败时仍保留近期记录，后续补存；不应长期在无卡状态下依赖完整归档。
- 每日累计值使用双副本与校验；合并相同累计值不会重复计时。首页热力图读取 TF 归档和 NVS 近期记录的较大值，重置设置不会抹掉卡内历史。
- 备份完整历史时，复制 `.readpico/reading` 文件夹。设置页的配置备份仍只包含原有近期统计，不包含这些 TF 文件。

Long-term records live in `.readpico/reading/YYYY.bin` on TF, without age-based eviction. Each year uses at most about 12 KB of data, subject to card capacity. Reads are paged and memory stays bounded. Existing 30-day NVS totals migrate automatically; already-overwritten dates cannot be reconstructed. NVS provides a recent recovery window while the card is absent or unwritable. Checked dual copies and idempotent cumulative merges reduce interruption damage and prevent duplicate migration. Home merges archive and recent totals, including after settings resets. Copy the history directory to back up the full archive; configuration backups retain only their existing recent statistics.
