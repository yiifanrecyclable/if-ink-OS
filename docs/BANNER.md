# Banner 区域设置 / Banner settings

首页长按 Banner，或从设置页「显示与个性化 → 首页Banner」进入原位预览。背景、日期、文字分别设置，可以自由组合；下方所有调整只修改本次草稿，底部“保存”统一生效，“取消”或离开编辑页放弃草稿。

Long-press the Home banner, or use Settings → Display & Personalization → Home Banner for an in-place preview. Background, date and text are independent layers. Changes remain drafts until Save; Cancel or leaving the editor discards them.

- 背景：在同一个选择页选“无背景图”或直接浏览 TF 文件夹选择 PNG/JPG/JPEG，默认从根目录 `banner pic` 开始浏览。图片保持原比例居中铺满 684×300 区域，超出部分裁切；单图最大 2 MiB。选图页底部显示格式、推荐尺寸和缩放裁切说明。系统示例为左侧思考猩猩、右侧留白的白底灰度 PNG，TF路径为 `banner pic/默认背景.png`。
- 日期：独立开启/关闭，通过“高度调节”同行右侧短滑块移动，范围为 Banner 顶部以下 4–260px。
- 文字：独立开启/关闭，开启后显示正文级“编辑内容”“选择字体”“对齐方式”“高度调节”。字号 48–120px 滑块收在字体选择页，文字最多四行，可通过键盘“换行”手动分行；格言左右安全边距为18px，换行与四种对齐共用648px宽度；四种水平对齐图标位于标题同行，高度通过同行右侧短滑块在整个 Banner 内调节，按实际字形高度限制边界。

- Background: choose None or browse TF PNG/JPG/JPEG images on the same selection page, starting in `banner pic`. Images preserve aspect ratio and fill/crop the 684×300 area, with a 2 MiB file limit. The bundled grayscale PNG places a thoughtful ape at left on a white background, at `banner pic/默认背景.png`.
- Date: toggle independently and move vertically using a short slider on the right of the Height row between offsets 4 and 260px.
- Text: toggle independently; when enabled, edit content, choose font or adjust placement. The font page includes the 48–120px size slider. Text supports up to four lines, including manual breaks through the keyboard’s Newline button, with four inline horizontal alignment controls and a short inline height slider spanning the full banner height, constrained by actual ink height.

背景在底层，日期在其上，文字在最上层；日期与文字都带白色描边。关闭文字不影响日期，关闭日期不改变文字位置。PNG 透明区域显示 Banner 的白色底面。

Background sits below date, with text on top. Date and text have white outlines. Their visibility is independent, and hiding the date does not move text. Transparent PNG areas reveal the white banner base.

日期和文字开关采用纯文字，高度滑块均位于标题同行右侧。拖动只更新滑块，松手后更新真实预览；取消手势保留原值。Banner 不再包含英文品牌行及两侧横线；保存/取消使用白底黑框胶囊，适度加宽并增加字间距，英文 Cancel 和 Save 等宽。

Date/text toggles are plain text; both height sliders sit to the right of their labels. Dragging updates the thumb, release updates the native preview, and cancelling preserves the original value. The brand line and side rules are removed; Save/Cancel use wider white pills with black outlines and increased letter spacing; English Cancel and Save have equal widths.

保存使用单个版本化快照，兼容旧的互斥图片/文字配置，纳入设置页的备份恢复。图片仍需随 TF 卡备份；图片丢失时首页使用白底，保留日期、文字的开关与位置，编辑页拒绝保存不可读的背景图。

One versioned snapshot participates in settings backup/restore and migrates older exclusive image/text preferences. Back up artwork separately on TF. Missing artwork falls back to white while retaining date/text visibility and positions; the editor rejects unreadable background selections.

文字设置四行统一行高，以细线分隔；编辑内容右侧以注释字号显示当前内容，超长截断。

The four text settings share equal-height rows and fine dividers. Edit Content shows a caption-sized, width-truncated summary at right.

显示背景图时，Banner与阅读记录之间的分割线拉通屏幕两侧。Banner文字输入框支持四行显示，光标可跨行移动和删除；账号、密码等其他输入仍为单行。最终保存同时校验自动折行后的四行上限与实际可用高度，字号过大时需缩小字号或缩短内容。

With artwork enabled, the banner divider spans the full screen width. The Banner input displays four lines and supports caret navigation/deletion across breaks; other inputs remain single-line. Save checks both wrapped line count and available height, so oversized text requires a smaller font or shorter content.

## 内置可选底图 / Bundled optional artwork

从0.1.4起，固件携带思考大猩猩原图（684×300 PNG），开机或插卡挂载成功时释放到 `banner pic/思考大猩猩.png`。释放采用临时文件、长度及CRC校验；缺卡暂缓，同名已有文件不覆盖。只提供可选文件，不修改Banner的开启状态、背景选择、文字、字体或位置。

The 0.1.4 image is installed after media becomes ready, verified before renaming, and never overwrites existing artwork or alters Banner preferences.
