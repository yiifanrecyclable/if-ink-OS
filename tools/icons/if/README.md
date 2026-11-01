# IF service icons / IF 服务图标

`nutstore.svg` traces the core nut silhouette and diagonal seam from the [Nutstore official logo](https://www.jianguoyun.com/static/images/logo.png), omitting the cloud and wordmark. `weread.svg` uses the classic overlapping chat bubbles and four eyes from the [WeChat app publisher icon](https://play.google.com/store/apps/details?hl=en_US&id=com.tencent.mm), as requested for the WeRead entry. Neither SVG embeds the original raster image; both are monochrome redraws on a 24-unit canvas with 2-unit rounded strokes.

坚果云保留原图斜放的坚果、果帽分界线和短柄，果实沿倾斜轴调整为对称轮廓，去掉云；微信使用原App的左上大泡泡、右下小泡泡、四只眼睛与左右尾巴。两者统一为单色圆角线条，原产品图标、品牌名称及商标归各自权利人所有；重绘和色彩简化不改变原权利归属，不将品牌图形本身声明为Apache开源资产。

`usb.svg` is a plug-shaped monochrome redraw of the user's reference, using the same 24-unit canvas and 2-unit strokes. It replaces the USB branching symbol for the “USB连接” entry. The reference's striped background is omitted.
`usb.svg` 按用户提供的 USB 插头范例重绘，采用同样的24单位画布和2单位描边，用于“USB连接”，省略范例背景斜纹。

`tools/gen_ui_icons.py` generates the same 4bpp masks as for Lucide. It supports filled eye dots only for the custom service icons. WiFi remains unchanged Lucide ISC artwork. Source raster references are kept in ignored `build/brand-reference/` for local design review only.

`folder-image.svg` uses the existing Lucide folder contour as the sole outer frame, with the image's sun and mountain strokes inside, without a nested photo frame. It is an ISC-derived composite, with the Lucide attribution retained in tools/icons/lucide/LICENSE. / 图片文件夹图标仅保留文件夹外框，内部融入太阳和山形，不再嵌套照片方框，保留ISC署名。
