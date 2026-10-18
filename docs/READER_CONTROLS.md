# 阅读操作

阅读正文默认左、中、右三个等宽点击区域：左侧上一页，右侧下一页，中间打开阅读控制面板。更多 → 基础操作可选择上、中、下分区（高度30%、20%、50%），分别为上一页、面板、下一页；两种模式以缩略图选择，图书与文档共用并持久保存，操作示例同步显示实际分区。圆键短按打开或关闭面板，长按返回首页；左右实体键在正文中翻页，在多页控制列表中翻列表页。电源键使用锁屏操作。Markdown图片仍可点开查看，长图按住上下拖动。

阅读面板展开后正文变淡，面板顶边增加与主导航同宽的细渐变阴影；关闭恢复原色，拖动控件不会累加淡化。中间第二个按钮依次切换常规布局、普通全屏、沉浸全屏，显示当前状态并保持面板展开。普通全屏保留状态栏，沉浸全屏隐藏状态栏，两者保留底部细进度线。三种布局跨图书与文档保存，更多中不再单独提供沉浸开关。 / Opening controls fades the prose and adds a narrow shadow without compounding during drags. Cycle three persistent layouts while keeping controls open. Fullscreen retains the progress strip; immersive mode also hides the status bar.

Markdown文字设置一页展示阅读字体、一级标题、二级标题、三级标题、正文和正文例句。标题标签直接使用文档字体及对应字号，拖动时实时更新；正文放在最后。文档字体与图书字体分别保存，旧配置首次打开文档时沿用当时的图书字体，此后互不影响。底部注明“本页设置仅对文档阅读生效”。 / Markdown typography fits one page: font, H1–H3, body size and body sample. Labels preview the document face and heading size live. Document fonts persist independently, initially inheriting the existing book preference.

主面板顶部以横线分隔正文，显示阅读进度、本次阅读时长、翻页次数和章节页数。拖动圆形进度滑块时预览目标百分比，松手后按整本书进度跳转，取消拖动不改变阅读位置。下方横排退出书籍、阅读布局、添加书签／取消书签、立刻刷新四个两行文字按钮；第三个按钮对当前页操作，按书签状态切换名称，操作后保持面板展开。全屏状态为全局偏好，更换图书／文档和重启后继续使用，随配置备份恢复。底部固定目录、书签、文字、版式、更多五个入口，以图标上方短粗线指示当前页，其中底部书签按钮打开书签列表，不添加或移除当前页书签。进度条下方的章节页数左对齐；目录、书签或进度跳转后，同一行右端出现小字“返回跳转前”，点击返回原位置，不另增面板高度。关闭目录或控制面板不改变阅读位置。

目录、字体、书签和设置使用系统文字及白底分割列表，行高和每页数量随字号调整。阅读字体、正文及Markdown标题字号独立于系统字体。调整字体、字号和版式保留当前文字位置；分页变化时回到包含原文字偏移的页。页边距按8～60像素逐像素调整，不凑整字行宽，也不与字间距联动；段距按0～75%逐1%调整。所有滑块均在松手提交，短拖、长按后拖动同样有效；加减按钮仅调整一档。

阅读线采用可直接点击的无、虚线、点线预览列表；位置减号、当前数值、加号横向分开，点数值不会改变位置。

拖动进度、字号和版式滑块时只绘制变化的控件区域，保留现有正文；松手后再执行跳转或重排。目录百分比滑块也使用区域更新，短拖及长按后拖动均可用，取消恢复拖动前数值，确认后才切换目录页。系统和阅读字体保留两套独立缓存，避免来回切换时反复读卡；字形位图总预算仍为1.5MiB，系统字体不额外整表映射。拔卡或进入USB存储时释放两套卡上字体资源。 / Slider previews repaint only dynamic controls and retain prose until release. TOC dragging also uses local updates, supports holds and restores its original value on cancellation; confirmation changes the TOC page. Two bounded font contexts avoid repeated card loads while retaining a combined 1.5MiB glyph budget, without whole-table mapping for the UI context. Card removal and USB storage purge both faces.

在更多 → 快捷手势中，可分别绑定单指四向滑动、长按正文、双指轻点、双指捏合/张开，共8种手势。四个双指方向滑动已移除。手势列表和候选操作各占一页，手势及已设功能同一行左右对齐；旧配置中的双指方向绑定在载入时清空。所有快捷操作默认不启用。每个非空功能只允许一个手势，选择功能时右侧注明已绑定的手势；先解除原绑定才能更换，旧配置重复项只保留第一项。旧版上下点击翻页、圆键长按强刷、电源键翻页设置不改变新的固定操作。可选动作包括上一页、下一页、目录、书签、阅读面板、全屏、增减字号、添加/移除当前位置书签、清残影；这些操作另有点击或实体键入口。

双指操作等全部手指离开后只执行一次。捏合/张开绑定字号后，每次改变一档，Markdown正文和标题按比例缩放并限制到各自有效范围。取消、锁屏或切换面板会丢弃本次手势，不在剩余单指抬起时补触发点击。书签删除需单独确认。晃动翻页及阅读页的传感器检测已移除。

首次打开尚未提示过的图书或文档时显示操作示例。关闭后同一内容不再提示；“在每本书开始时显示”默认选中，取消后停止在新内容首次打开时弹出；可从更多中重新打开并勾选恢复。更多 → 操作示例始终可以重新打开，示例以朱自清《荷塘月色》开篇为淡色底图，分区与基础操作中所选横向或纵向区域一致，中央提示对应实际正文点击区；去掉顶部中央标题和示例书名，从原文开始；分区线距屏底保留36像素，按键说明和箭头上移约40像素，避开设备装配遮挡。说明文字仍用白描边。顶部显示勾选项和关闭按钮，示意区点击不会触发翻页。快捷设置和全局显示偏好随配置备份保存；逐内容已提示标记留在设备上。旧版配置备份可恢复，缺少快捷设置的备份保留当前快捷设置。

模拟器可直接检查控制界面及单指操作。网页下方的模拟阅读快捷手势工具按已保存的绑定执行动作，不代替真机双点触控识别测试。双指动作及触控误触表现仍需实体设备验收。

English: Default columns or optional 30/20/50 rows tap previous/controls/next; the circle toggles controls, a hold goes Home, and power locks. Controls combine statistics with TOC/bookmarks/type/layout/more and preserve the reading anchor through reflow. The minimum margin is 8 px. Eight optional one/two-contact shortcuts start unbound; directional two-finger swipes are retired. Full screen persists globally, margins use independent pixels and paragraph spacing uses one-percent steps. Shortcuts and action choices fit single pages. The guide keeps a bezel inset. Shortcuts commit once on release; pinch size changes scale Markdown headings proportionally. Remove shake detection, retain physical/page-button access, confirm bookmark deletion and offer per-content help with a global opt-out. Back up shortcuts and opt-out with legacy compatibility. Native preview checks control actions; hardware multitouch still needs device acceptance.

示例文字：朱自清《荷塘月色》（1927）开篇，简体转写。原作来源：[维基文库](https://zh.wikisource.org/wiki/荷塘月色)，页面标为公有领域。齿轮使用 [Lucide settings](https://github.com/lucide-icons/lucide/blob/main/icons/settings.svg)，沿用项目 Lucide 许可说明。

底层预绘以文字行或Markdown表格块为可停止单元；推屏结束后交还字体锁，未完成目标页可继续绘制，排版代次变化后游标失效。空闲工作使用软预算，不承诺复杂表格在固定毫秒内完成。EPUB目录标题和锚点使用稳定块池，索引采用不含指针的独立固定宽度格式，旧索引自动重建，正文与进度不变。图片分配不足时优先回收可重建字形/下一页缓存；保留系统和阅读字体上下文。刷新后释放像素驱动并对短暂控制通信失败重试，保留现有IF波形、清残影周期和首页强刷，睡眠前同步关闭。

English: Prefetch yields at row/table-block boundaries and validates layout generations. Idle work has a soft budget. EPUB labels use stable pools and pointer-free versioned cache records; old indexes rebuild without changing prose/progress. Images can reclaim rebuildable glyph/page caches. Release pixel drive after output and retry transient failures while preserving IF waveforms and cleanup policy; finalize before sleep.
