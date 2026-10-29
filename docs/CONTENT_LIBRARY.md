# 内容页管理与排序

内容页保留图书书架和Markdown文档列表。两页右上角均为「管理」「排序」，只作用于当前子页；传入文件统一使用文件页的三个传输入口。图书沿用原有书库和其他目录读过的TXT/EPUB，Markdown阅读记录不会补入图书书架。

## 图书陈列

图书固定三行三列，每页九本。封面保持比例裁剪，无书轨、亚克力挡板、透明书袋、立体书页或书脊装饰；封面下方先显示极简进度横条，再显示书名。书名最多两行，过长以省略号结束；横条浅灰为未读部分、深色为已读比例，不显示进度数字，读完填满。置顶、排序和长按详情保留；收藏标记与入口移除。

按下封面保持原位置，不产生提起或按下装饰；松手直接打开，长按仍打开详情。EPUB已知尺寸的小注图和Markdown普通插图与前后正文流排在同一页，大图按比例缩小，多张图片可接排。此为块级图文混排，不模拟文字行内上标；未知尺寸图片及MD长图沿用整页显示。

设置中移除「书架样式」，旧NVS样式不再读取或写入。设置备份保留原格式的外观字段以兼容旧备份，但保存固定兼容值，恢复时忽略；任何旧样式都不能重新开启装饰或书脊。

## 排序

可选书名/文档名、阅读进度、加入日期、最近阅读，并可切换正序与倒序。点击「完成」保存并应用，「取消」不修改偏好。图书与文档分别记住模式、方向；重启后保留，并随设置备份的阅读资料保存和恢复。

- 两页排序弹窗使用相同的外框、选项和按钮位置；仅标题与名称标签区分图书/文档。图书收藏及文档星标的入口、标记、优先和筛选全部取消。旧标记记录与备份中的筛选位兼容保留，但不影响显示、排序或隐藏内容。
- 图书和文档置顶始终优先，仍通过单篇长按详情设置。
- 同组再按所选规则排序；相同数值按名称、完整路径稳定排列。英文不区分大小写，数字按自然顺序；中文按字符顺序排列。
- 最近阅读使用设备已有的持久阅读序号，不依赖文件修改日期。未读内容排在已读内容后，再按加入日期排列。
- 文档加入日期为首次入库日期，同名替换保留。图书记录本版首次收录时间，不把文件修改时间当成加入日期；无法还原旧版的历史入库时间。设备时钟无效或存储失败时视为未知日期，排在已知日期之后。
- 排序和优先选项保留当前勾选；切换「仅看」筛选会清除勾选，避免操作隐藏条目。

## 管理

长按图书时仅在当前书架上显示详情浮动卡片；背景沿用图书/文档切换、管理/排序和图标导航，不绘制额外详情标题、来源/搜索栏或翻页按钮。卡片支持置顶/取消置顶、前往文件管理重命名、清进度、删除及关闭；打开期间阻止背景控件响应，点击卡片外关闭（确认期间先取消确认）。关闭保留原页码；置顶操作重排到第一页。图书置顶绑定完整路径，重启保留并随设置备份恢复；置顶始终优先，同名路径重新收录沿用标记。

文档长按使用相同的「文档详情」浮动卡片，显示文档名、TF卡、类型/体积、阅读进度与文件夹；提供置顶/取消置顶、清进度、删除和关闭。清进度及删除在原卡内确认，清进度保留正文/图片，删除保护共享图片。清理失败保持卡片供本次开机重试；重排后卡片仍绑定原文档。阅读页不显示独立收藏入口，书签保留。

图书保留原有管理：本页全选、清除勾选、重新扫描、删除所选、清进度和移出书架，以及来源筛选、搜索。管理页的排序按钮打开同一排序面板。

两种管理页使用相同的标题、来源/排序/搜索栏、六行列表、字体、外框及底部居中分页按钮。文档目前仅存于TF卡，来源可切换「全部来源」「TF卡」，不显示不存在的内置文档来源；搜索与图书独立，支持拼音首字母、完整拼音和英文。应用搜索、切换来源或重新扫描会清除勾选。

文档管理每页六篇，支持本页全选、清除勾选、重新扫描、清进度、删除所选；对应操作与图书使用相同位置和样式。文档不提供移出书架。置顶/取消置顶仅在文档列表单篇长按详情卡中提供，不允许批量操作。可跨页勾选；翻页或更换排序不会丢失勾选。清进度与删除需再次确认；清进度保留文档和图片。删除正文、阅读记录和独占图片走文档服务；其他文档仍引用的图片保留，不会删除云端文件。成功项取消勾选，失败项保留供重试；正文已删除但清理失败时显示「清理待重试」，本次开机期间可继续清理。

首次日期、置顶、图片所有权和同步索引属于TF卡文档数据，换卡时仍需保留文档目录及隐藏索引。设置备份保存两页的排序偏好与图书首次收录时间、置顶标记，不复制图书、正文或图片。

## WiFi文档上传

WiFi/热点网页的「上传文档」固定将非空`.md`存入`markdown/`，每篇最多256 KiB，不选择其他目录。浏览文档目录的通用上传也会转到专用文档接口。同名替换需确认，正文完整接收和验证后才提交；首次日期与置顶保留，无变化保留阅读进度，正文变化清理旧进度。中断或验证失败保留原正文。

含图片的MD仅接受已在`markdown/attachments/`中的兼容灰度PNG引用，缺图拒绝入库；不会从网络下载或将普通图片自动转换。含插图的笔记建议使用Ob插件。普通文件上传、复制、移动、重命名仍保护文档与事务目录；删除由内容页统一清理附件与索引。

## 验证

`PYTHONDONTWRITEBYTECODE=1 python3 tools/test_content_library.py` 使用独立临时卡验证实际C页面手势、两页隔离、名称/进度/日期/最近排序、单篇置顶与旧星标兼容、独立搜索、浮动卡片背景一致性、图书置顶持久保存、居中分页与跨页勾选、确认取消、进度清除、共享图片保护及删除失败重试。`tools/book_history_backup_host_test.c`验证排序偏好与首次时间随阅读资料备份恢复。测试不使用真实账号、密码或项目TF卡。

English: Books and documents have independent Manage/Sort controls. Four sort modes and direction persist with settings history backup; both tabs share the same sort layout, with favorite/star UI and filters retired. Both tabs share floating detail cards. Both tabs retain single-item pins; legacy favorite/star records and filter bits remain backup-compatible without decorating, reordering or hiding content. Both management pages share layout and independent search. Batch document deletion uses ownership-aware cleanup, retains shared assets, and leaves failed items available for retry during the current boot. Book dates start at first collection by this build; historical import dates cannot be reconstructed.

## Markdown 表格 / Markdown tables

if.8支持普通Markdown管道表格（表头、分隔行和内容行）。例如：

```markdown
| 项目 | 说明 | 状态 |
| :--- | :---: | ---: |
| 阅读 | **粗体**、*斜体*和==高亮== | 已支持 |
```

能容纳的表格以细线网格显示，表头浅灰加粗，各列支持左/中/右对齐，长文字自动换行。沿用文档正文的字体、字号与行距；不另设表格字号。长表分页后重复表头，单行超过一页时继续显示剩余内容。列宽不足以清晰阅读或超过8列时，自动纵向显示列名和对应内容。超长列名完整显示一次，重复的列名最多两行。表内图片显示说明文字；表格外的图片阅读保持原有方式。不解释HTML表格、嵌套表格或合并单元格。

Use standard Markdown pipe tables. Readable tables use thin grids, gray bold headers, column alignment and wrapping at body typography. Long tables repeat headers across pages, including continuation of oversized rows. Too-narrow or more-than-eight-column tables stack labels and values. Oversized labels appear once in full; repeated labels use at most two lines. Images inside cells show descriptions; images outside tables retain their normal behavior. Raw HTML tables, nested tables and merged cells are outside this subset.

翻页、书签和续读仍使用原文字节位置；更换字体或字号会重新排版。普通图书不走此表格绘制路径。维护回归命令：

Pagination, bookmarks and resume retain normalized text offsets; changing typography rebuilds the layout. Ordinary books do not use this table renderer. Maintenance checks:

```sh
bash tools/run_markdown_host_tests.sh
bash tools/run_markdown_table_tests.sh
SANITIZE=1 bash tools/simulator/build.sh
ASAN_OPTIONS=detect_leaks=0 python3 tools/test_markdown_tables.py
```

## 固定存储目录

图书根目录固定为TF卡的`books`，字体目录固定为`fonts`，Markdown文档目录固定为`markdown`（附件在`markdown/attachments`）。删除“设为书籍目录／字体目录”，旧自定义路径和旧备份不会改变标准根目录；原目录与原文件保留，需用户自行移入标准目录。标准根目录不可在设备上改名、移动或删除，其内部文件仍按既有规则管理。文件页只保留WiFi/热点、坚果云同步和USB连接三个入口。

English: Fixed TF roots are books, fonts and markdown. Legacy root preferences/backups cannot reroute them or move files. Protect the root folders from rename/move/delete; retain existing management rules for their contents. Files exposes WiFi/hotspot, Nutstore and USB.
