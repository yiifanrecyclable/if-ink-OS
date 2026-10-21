# 深睡恢复 / Deep-sleep resume

普通浅睡保持当前页面。浅睡超时、主控进入深睡前，系统可将已成功显示的锁屏壁纸存为 TF 卡 `.readpico/sleep-frame.bin`，并写入小型、一次性的恢复标记。下次正常上电时校验快照及当前固件身份，复用屏幕参考并返回原主页面；正在阅读时，TXT、EPUB 和 Markdown 共用原有进度记录续读。

Light sleep retains the current page. Before timeout-driven deep sleep, the system can checkpoint the successfully displayed lock wallpaper to `.readpico/sleep-frame.bin` and store a compact one-shot marker. Normal power-on validates the snapshot/build identity, reuses the physical-screen reference, and returns to the main root or resumes TXT/EPUB/Markdown using existing progress records.

标记在开书之前消费。异常复位、TF 卡缺失、快照不完整/损坏、源文件尺寸或时间变化、固件变化时走普通开机流程，不自动重试上次开书。缓存失败不阻止休眠，不删除用户文件、书签、配置或进度。缓存是可丢弃文件；没有 TF 卡时沿用普通启动。

Consume the marker before parsing content. Abnormal resets, missing media, invalid snapshots, changed source size/time or a changed firmware build use normal startup without repeatedly reopening content. Cache failure cannot block sleep or delete user content/settings/bookmarks/progress. The cache is disposable; systems without an SD card use ordinary startup.
