# WiFi／热点管理网页 / LAN management page

在 Pico 的“WiFi／热点传输”页面启动服务后，电脑连接同一局域网，或连接 Pico 热点，使用浏览器打开设备显示的网址。页面由设备提供，不需要联网访问其他网站。保持设备停留在传输页面；退出即停止网页服务。

Start WiFi/hotspot transfer on Pico and open the displayed address from the same LAN or its hotspot. The device serves the page locally. Leaving the transfer page stops the service.

## 个性化 / Personalization

- 桌面左侧编辑首页 Banner、锁屏壁纸，右侧管理 TF 卡；手机上上下排列。
- Banner 为684×300，锁屏为684×1216。可拖入本地图片、移动裁剪并缩放，也可在右侧目录选择现有图片。浏览器生成适配16阶灰度的图，保留原文件。
- 网页滑块明确标注“背景图缩放”“日期上下位置”“文字上下位置”；位置滑块左端靠上、右端靠下，背景缩放向右放大，背景位置可直接在预览中拖动。背景、日期、文字关闭后只保留各自开关，所有详细设置收起；重新开启保留原选择与数值；滑块松手立即请求预览，连续拖动仍合并请求。文字在底部按实际字高限制位置，以保持完整显示。
- Banner 的背景、文字及动态日期独立保存；文字沿用四行、48–120px、设备字库和现有位置规则。字体列表提供 fonts 中的TTF；当前字体即使未列入扫描结果也会保留其路径，无法读取或缺字时拒绝保存。
- 编辑停止约500ms后自动请求设备实际排版；一次只处理一个请求，过期结果不覆盖新草稿。文字、字号、行距、描边和位置均由设备同一渲染器生成，不刷新墨水屏、不保存设置；等待或失败时明确标记预览未更新，不退回浏览器替代文字。保存也重新校验完整草稿；返回首页后显示新 Banner，壁纸于下次休眠显示。
- 可从电脑选择32MiB以内的TTF字体，先放入隐藏临时区用于真实预览；按文件内容比较已有字体，已有则复用。新字体保存前提示同步至`fonts`，确认后以独立文件名写入、同步落盘并核对副本，再保存配置。取消或更换字体会清理暂存；断开网页遗留的临时字体在停止或重新进入传输服务时清理。一次最多暂存8个字体。
- 新图先以唯一名称上传，通过图片解码及排版验证后才保存配置；不覆盖当前使用的图片。取消或失败可能留下未引用新图，可从文件列表手动删除。
- 多个浏览器同时编辑时，旧快照不能覆盖新设置。若连接超时，保存结果可能尚未确认，须重新读取核实，不自动重复写入。

The browser handles cropping and requests native previews after a 500ms pause. A single in-flight request and generation checks prevent stale pixels from replacing newer drafts. No browser typography substitutes for native rendering; pending and failed previews are labelled. Computer TTFs are staged privately for preview and installed only after an explicit Save confirmation; byte-identical installed fonts are reused. Full snapshots are validated again on Save. Image upload precedes configuration commit and uses new filenames. Failed or cancelled edits may leave unreferenced files. Conflicting snapshots are rejected; after timeouts, reread settings rather than assuming success or retrying writes automatically.

## 文件管理 / Files

支持分页目录、上传、下载、新建文件夹、重命名、复制和删除。根目录中的books、markdown、fonts、pictures、banner pic分别标明默认用途和“不可更改”，目录本身禁止改名、复制、删除或作为修改目标；可正常打开、上传及管理允许操作的子文件。分类上传仍使用books、markdown、fonts、pictures。当前 Banner 使用的图片和明确指定的字体不可覆盖、改名或删除；先更换设置再管理这些资源。

Markdown 正文与附件可下载；上传或同名更新继续通过文档导入事务。受管目录的改名、复制和删除不在网页开放，请在设备内容页管理。隐藏缓存不可下载，普通下载仅作为附件发送。首版不提供在线编辑MD/TXT正文。

Browsing, uploads/downloads, folder creation, rename/copy/delete are available. Active Banner resources are protected. Markdown uploads keep the validated document transaction; managed directories remain write-protected and document removal stays in the device UI. Hidden caches cannot be downloaded. General downloads are non-executable attachments. Inline document editing is not included.

## 维护契约 / Maintenance contract

- `/appearance` GET读取完整快照，POST `op=preview|banner|wallpaper|font-match|font-release` 提交草稿。正文上限3071字节，必须带当前revision；日期和文字不是烘焙进背景的静态图层。
- `/font-stage` PUT只接受32位小写十六进制令牌命名的TTF，写入`.web-fonts`；匹配、释放与正式安装由UI任务串行处理。`font-match`按完整文件字节查重，保存不得直接持久化临时路径。
- `/banner-upload` PUT上传至`banner pic`，图片上限2MiB；应用前另验证可解码与尺寸。`/image-upload?wallpaper=1`旧直接应用入口已禁用。
- `/file?path=...` GET只读TF内公开相对路径；分段检查隐藏/越界路径，流式发送并核对完整长度，以附件、octet-stream、nosniff输出。
- HTTP/UI单请求桥接以双引用持有任务。HTTP超时后，处理任务完成前仍保持存储busy；停止先取消pending再等待HTTP退出。排版和提交只在UI任务执行；预览不改变传输显示状态。
- 字体须核对实际加载路径和文字覆盖，不能只看`ttf_font_open`的返回码。快照保存失败尝试恢复原值，但回退失败仍可能存在，必须保留“重新读取核实”的结果表述。

The UI task owns rendering/commit through a two-reference job handoff. Timeouts retain storage admission until processing completes; stop cancels pending work before joining HTTP. Preview never alters the transfer display state. Font identity/coverage, conflict checks and explicit uncertain-result semantics must be retained.

本机开发预览：`tools/simulator/transfer_preview.py`仅绑定127.0.0.1，使用build中的隔离测试卡和真实原生排版／文档导入代码，不是实机HTTP/WiFi性能模拟。
