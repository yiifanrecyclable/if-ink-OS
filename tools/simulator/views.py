# SPDX-License-Identifier: Apache-2.0
"""IF Edition / rc83 view inventory. Native entries invoke production C drawing."""
VIEWS = {}
def add(key, label, group, native, aux=0, mask=0):
    VIEWS[key] = dict(id=key, label=label, group=group, native=native, aux=aux, mask=mask)

add('baseline-home','首页','主界面','baseline-home')
add('documents','内容 · 文档','主界面','book-8')
add('book-0','内容 · 图书','主界面','book-0')
add('files-home','文件','主界面','files-0',aux=-1)
add('settings-0','设置','主界面','settings-0')
add('cloud-sync','坚果云同步','同步','cloud-0')
for n,name in enumerate(('WiFi','日期与时间','手动设置时间',None,'系统文字',None,None,None,'锁屏壁纸','预览壁纸','保存与恢复','本地升级','BOOT刷机','关机与休眠',None,None,None,None,'文字编辑','坚果云同步'),1):
    if name is None:continue
    add(f'settings-{n}',name,'设置子页',f'settings-{n}')
for key,name,native,mask in (
 ('reader','正文','book-1',0),('reader-fullscreen','全屏正文','book-1',100),
 ('reader-tools','阅读控制面板','book-1',1),('reader-font','字体与字号','book-1',2),
 ('reader-layout','排版设置','book-1',3),('reader-rule','阅读线','book-1',4),
 ('reader-md-sizes','文档标题与正文','book-1',11),('reader-font-picker','选择字体','book-1',5),
 ('reader-bookmarks','书签','book-1',7),
 ('reader-refresh','更多阅读设置','book-1',9),('reader-toc','目录','book-2',0),
 ('reader-toc-jump','目录 · 跳转','book-2',1)):
    add(key,name,'阅读',native,mask=mask)
for key,name,view,mask in (
 ('book-details','图书详情',3,0),('book-bulk','批量管理',4,0),
 ('book-import','导入方式',5,0),('book-search','搜索',6,0),('book-edit','编辑书名',7,0)):
    add(key,name,'内容管理',f'book-{view}',mask=mask)
add('documents-manage','文档 · 批量管理','内容管理','book-9')
for key,name,view,folder in (
 ('files-books','书籍目录',0,0),('files-pictures','图片目录',0,1),
 ('files-fonts','字体目录',0,2),('files-card','TF 卡目录',0,3),
 ('files-actions','文件操作',1,0),('files-rename','重命名',2,0),
 ('files-move','移动文件',3,3)):
    add(key,name,'文件管理',f'files-{view}',aux=folder)
for key,name,view,mode in (
 ('transfer-methods','选择传输方式',0,0),('transfer-wifi','WiFi 传书',1,1),
 ('transfer-hotspot','热点传书',1,0),('transfer-networks','选择无线网络',2,1),
 ('transfer-password','输入网络密码',3,1)):
    add(key,name,'传输',f'transfer-{view}',aux=mode)
add('transfer-usb','USB 读卡','传输','transfer-1',aux=2)
add('image-list','图片列表','图片','image-0')
add('image-view','查看图片','图片','image-1')
for key,name in (('menu','系统菜单'),('power','电源菜单'),('shutdown','关机画面'),('restart','重启画面')):
    add(key,name,'系统',key)
for key,name in (('loading','启动画面'),('lock-default','默认锁屏'),('lock-wallpaper','壁纸锁屏')):
    add(key,name,'系统',key)
add('home','自定义首页原型','新功能原型','home')
add('cloud','坚果云目录示例','新功能原型','cloud')
add('shelf','同步 MD 列表','新功能原型','shelf')

# 首页格言复用生产输入界面。/ Home quotes reuse the production input editor.
add("home-quote","首页格言","设置子页","settings-21")

add("settings-22","首页文字字体","设置子页","settings-22")
add("settings-23","首页文字编辑","设置子页","settings-23")

add("settings-24","电池管理","设置子页","settings-24")

add("settings-25","状态栏日期格式","设置子页","settings-25")

add("settings-26","自动休眠","设置子页","settings-26")

add("reader-shortcuts","阅读快捷操作","阅读","book-1",mask=12)
add("reader-binding","选择快捷操作","阅读","book-1",mask=13)
add("reader-guide","阅读操作示例","阅读","book-1",mask=14)
add("reader-basic","基础操作","阅读","book-1",mask=15)

add("settings-27","固件信息","设置子页","settings-27")

add("settings-28","在线升级","设置子页","settings-28")

# 保留设置来源，避免返回路径被传书页覆盖。/ Preserve the settings origin independently from upload provisioning.
add("wifi-settings","无线网络设置","设置子页","transfer-2",aux=3)
add("wifi-settings-password","无线网络密码","设置子页","transfer-3",aux=3)
