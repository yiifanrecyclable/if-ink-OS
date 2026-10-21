# Third-party notices / 第三方许可

Modified 2026-10-05 for the independent Pico reader: added the pinned md4c parser attribution; existing notices are retained.
2026-10-05 修改：补充独立 Pico 阅读固件使用的固定版本 md4c 解析器署名；保留原有声明。

2026-10-20：`main/display_profiles.c`、`main/display_waveform_smooth.h` 和 `e0470_update_regions` 适配 kiikoread（wegooo-cell/read-pico-reader）提交 `2087afda444b17fb320562fc31ab9424b661e946` 中标记 Apache-2.0 的刷新代码；保留原厂波形表，未引入透明书封模块。
2026-10-20: The derived display profiles, pulse interleaving helper and simultaneous-region scan adapt Apache-2.0-marked code from kiikoread commit `2087afda444b17fb320562fc31ab9424b661e946`. Vendor tables remain unchanged; the transparent lock-cover module is not included.


IF Edition is based on Pico Reader rc83 (wegooo-cell/read-pico-reader); upstream copyright and component notices are retained.
IF Edition 基于 Pico Reader rc83，保留上游版权及各组件许可。

This is an independent reader firmware for MindReset's Read Pico board. It is
based on the [MindReset demo firmware](https://github.com/MindReset/read_pico_firmware),
which is licensed under Apache-2.0. The original copyright and license notices
remain in the source. This project is not an official MindReset release.

本项目是基于 MindReset Read Pico 官方示例固件开发的独立阅读固件，并非 MindReset
官方发布。原项目的版权声明和 Apache-2.0 许可均予保留。

| Material / 组件 | License / 许可 | Notice / 声明 |
| --- | --- | --- |
| MindReset Read Pico firmware and board components | Apache-2.0 | [LICENSE](LICENSE); source file headers |
| [Lucide](https://lucide.dev/icons/) — the whole UI icon set, vendored as SVG sources in `tools/icons/lucide/` and compiled into `main/assets/ui_icons.h` by `tools/gen_ui_icons.py` | ISC | [Lucide notice](flash/licenses/LUCIDE-ISC.txt) |
| Nutstore nut and WeChat chat-bubble icons, redrawn in monochrome for IF service entries | Original brand graphics and trademarks remain with their respective owners; redrawing does not relicense them | [Sources and redraw provenance](tools/icons/if/README.md) |
| [JPEGDEC](https://github.com/bitbank2/JPEGDEC) — progressive JPEG decoding, vendored and patched in `components/jpegdec/` | Apache-2.0 | [Component license](components/jpegdec/LICENSE), [Fork notes](components/jpegdec/FORK.md) |
| [epdiy](https://github.com/vroland/epdiy), including the locally modified driver | LGPL-3.0-or-later | [Modification notes](components/epdiy/LICENSE), [LGPLv3](licenses/LGPL-3.0.txt), [GPLv3](licenses/GPL-3.0.txt) |
| [md4c](https://github.com/mity/md4c) 0.5.2 parser, including local fixes; Copyright © 2016–2024 Martin Mitáš | MIT | [Original license](components/md4c/LICENSE.md); [provenance and local changes](components/md4c/UPSTREAM.md); upstream `release-0.5.2` commit `729e6b8b320caa96328968ab27d7db2235e4fb47` |
| [cJSON](https://github.com/DaveGamble/cJSON) 1.7.19 / managed 1.7.19~2 | MIT | [Notice](licenses/CJSON-MIT.txt); [host source](tools/simulator/vendor/cjson/README.md) |
| [Expat](https://components.espressif.com/components/espressif/expat/versions/2.8.4/readme) managed 2.8.4 | MIT | [Notice](licenses/EXPAT-MIT.txt); locked component hash in dependencies.lock |
| [pypinyin](https://github.com/mozillazg/python-pinyin) dictionary data used by offline search and input candidates; pinned 0.55.0 phrase pack in `assets/ime/` | MIT | [pypinyin notice](components/read_pico_search/LICENSE.pypinyin), [phrase pack MIT license](assets/ime/pinyin.LICENSE.txt), [provenance](assets/ime/README.md) |
| Noto Sans SC Medium common-character embedded subset and unchanged standard TF-card font | SIL OFL-1.1 | [Font license](main/assets/OFL-Noto.txt); [source, hashes and package](assets/fonts/README.md) |
| [CrossMux](https://github.com/0x1abin/crossmux) WeRead protocol, streaming download and EPUB writer, ported from commit `d6a1727bb27a858ba2ee9a44529ba8e458defbad` | MIT | [CrossMux notice](components/pico_weread/vendor/LICENSE-CrossMux.txt) |
| FreeInk SDK StreamingJsonParser, commit `96de1be6ce08eb732909e6e8149af8f892b9a2c5` | MIT | [FreeInk SDK notice](components/pico_weread/vendor/LICENSE-FreeInk-SDK.txt) |
| [ESP Web Tools](https://github.com/esphome/esp-web-tools) browser flasher | Apache-2.0 | [Bundled web tool license](flash/vendor/LICENSE) |
| Espressif TinyUSB component | Apache-2.0 | [Component license](components/espressif__esp_tinyusb/LICENSE) |

License terms apply to their respective material. The repository's top-level
Apache-2.0 license does not replace those component licenses.

各组件继续适用各自的许可。CrossPoint 的 MIT 署名不表示整个固件改为 MIT 许可。
The firmware embeds a common-character subset. The repository also provides
the standard Noto Sans SC Medium TTF as a separate TF-card resource with its
original OFL notice; it is not embedded in the application binary. Users may
continue to supply their own fonts. See assets/fonts/README.md for provenance.
各组件继续适用各自的许可，不能把整个固件简称为其中某一个组件的许可。
The firmware embeds only the Noto Sans SC subset. The separate TF resource
package includes the unchanged standard Noto Sans SC Medium font with its OFL
license; user-provided fonts are not distributed.
固件只内嵌Noto Sans SC子集；独立TF资源包含原样标准字库及OFL许可，不分发用户自备字体。

JPEGDEC 使用 BitBank Apache-2.0，具体适配与修正见 `components/jpegdec/FORK.md`。

## Offline lunar-year table

`main/ui/ui_lunar_data.h` is generated from MIT-licensed [6tail lunar-python](https://github.com/6tail/lunar-python) 1.4.8. Only 201 year-start/month-length records are linked into firmware, not the Python library. The original license, source digest and generator instructions are in [assets/calendar](assets/calendar/README.md). Gregorian/lunar date anchors were checked against the [Hong Kong Observatory 2026 conversion table](https://www.hko.gov.hk/tc/gts/time/calendar/pdf/files/2026.pdf).
