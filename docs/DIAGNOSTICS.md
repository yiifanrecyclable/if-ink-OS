# 操作诊断 / Operation diagnostics

设置 → 系统与设备 → 记录日志 → 开始记录。返回阅读，正常操作并尝试复现失效，等待两分钟。然后回到该页面查看保存路径，从TF卡 `diagnostics` 文件夹取出 `operation-NNNN.csv` 发给维护者。也可通过WiFi文件管理下载。

采集期仅写固定容量PSRAM，结束后才写卡。休眠、关机或USB接管会提前结束；无卡/写失败保留RAM并显示待保存，插卡后可重试。未保存前断电或重启会丢失RAM日志。每次单独编号，不覆盖旧文件；不自动上传，也不无限持续记录。

日志记录相对微秒时间、采样空档、I2C耗时/错误、阅读触点和手势、翻页及显示/预绘制收尾耗时。普通静止采样压缩为50ms心跳；容量满时覆盖最早记录，文件头dropped字段明确标注。此诊断仍使用原触控采样链路；它能揭示采样空窗，不能凭空恢复空窗期间未被硬件读到的触摸。

不记录正文、文件名/书名、SSID、账号或密码；阅读正文外隐藏触控坐标。日志包含固件版本和系统时间，以便对应测试版本。保持原阅读刷新与输入行为，本版用于定位，不能视为触控问题已修复。

Settings → System & device → Record diagnostics → Start recording. Return to reading and reproduce the issue. After two minutes, retrieve diagnostics/operation-NNNN.csv from TF or WiFi file management. Sleep/power-off/USB finishes early. Capture is RAM-only until export; failed export retains RAM for retry, but reset/power loss loses unsaved data. Files are numbered without overwriting. The bounded ring reports dropped entries. Numeric events include timing, read gaps/errors and reader gestures/refresh phases; text, filenames and credentials are excluded, and non-reader coordinates are redacted. No automatic upload. Input sampling and reader refresh remain unchanged for diagnosis.
