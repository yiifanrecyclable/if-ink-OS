#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# 只编译宿主模拟器，不生成可烧录固件。/ Build host simulator, never flashable firmware.
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/simulator
output=$(mktemp build/simulator/render.XXXXXX)
trap 'rm -f "$output"' EXIT
font="$PWD/main/assets/builtin.ttf"
image="$PWD/main/assets/display-test.png"
card="$(dirname "$PWD")/TF卡"
if [[ "$(uname -s)" == Darwin ]]; then
    section='.section __TEXT,__const'
else
    section='.section .rodata'
fi
cat > build/simulator/font.S <<EOF
$section
.balign 16
.global _binary_builtin_ttf_start
.global _binary_builtin_ttf_end
_binary_builtin_ttf_start:
.incbin "$font"
_binary_builtin_ttf_end:
.balign 16
.global _binary_display_test_png_start
.global _binary_display_test_png_end
_binary_display_test_png_start:
.incbin "$image"
_binary_display_test_png_end:
.global _binary_lock_4bpp_bin_start
_binary_lock_4bpp_bin_start:
.incbin "$PWD/main/assets/lock_4bpp.bin"
.global _binary_loading_4bpp_bin_start
_binary_loading_4bpp_bin_start:
.incbin "$PWD/main/assets/loading_4bpp.bin"
EOF
flags=(-DPICO_HOST_SIMULATOR=1 -DCJSON_NESTING_LIMIT=16)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags=(-DPICO_HOST_SIMULATOR=1 -DCJSON_NESTING_LIMIT=16 -fsanitize=address,undefined -fno-omit-frame-pointer -g); fi
cc "${flags[@]}" -std=gnu11 -Wall -Wextra -Werror -Wno-deprecated-declarations -c tools/simulator/vendor/cjson/cJSON.c -o build/simulator/cJSON-render.o
cc "${flags[@]}" "-DPICO_SIM_CARD_PATH=\"$card\"" -std=gnu11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Wno-unused-parameter \
    -Wno-unused-function -Wno-unused-variable -Wno-sign-compare -ffunction-sections -fdata-sections \
    -include tools/simulator/fs.h -Itools/simulator/include -Itools/zip_host_stubs -Imain -Imain/app -Imain/apps -Imain/factory -Imain/ui -Imain/font -Imain/book -Imain/cloud -Imain/power -Itools/simulator/vendor/cjson -Icomponents/md4c \
    -Icomponents/sc7a20h/include -Icomponents/pico_weread/include \
    -Icomponents/read_pico_pmu/include -Icomponents/read_pico_transfer/include -Icomponents/read_pico_search/include \
    tools/simulator/render.c tools/simulator/firmware_cloud.c tools/simulator/rc83_hardware.c build/simulator/cJSON-render.o tools/simulator/pixels.c main/font/ttf_font.c \
    tools/simulator/fs.c tools/simulator/image_decode.c tools/simulator/hardware.c tools/simulator/nvs.c tools/simulator/firmware_settings.c tools/simulator/firmware_dashboard.c \
    components/read_pico_search/read_pico_search.c \
    main/book/book_progress.c main/book/book_history_backup.c main/book/content_library.c main/book/book_title.c main/book/book_source.c main/book/book_markdown.c main/book/document_image.c main/book/document_store.c main/book/book_txt.c main/book/book_epub.c \
    main/book/book_index_cache.c main/book/zip_reader.c main/book/gbk.c main/book/book_cover_auto.c main/book/book_cover_store.c main/book/book_ticket.c main/book/reading_history_store.c main/apps/file_tree.c \
    tools/simulator/firmware_book.c tools/simulator/firmware_files.c tools/simulator/firmware_transfer.c \
    tools/simulator/firmware_image.c main/apps/book_toc.c main/ui/ui_menu.c main/ui/ui_gesture.c main/ui/ui_power_dialog.c \
    main/ui/ui_wifi_qr.c tools/simulator/vendor/qrcodegen.c \
    main/settings.c main/sleep_resume.c main/power/battery_history.c main/font/app_font_context.c main/font/ui_font.c main/ui/ui_kit.c main/ui/ui_i18n.c main/ui/ui_text_input.c main/ui/ui_pinyin.c main/ui/ui_nav.c main/ui/ui_lunar.c main/ui/ui_banner.c main/ui/ui_nav_icons.c main/ui/datamatrix.c \
    main/ui/ui_image_dither.c main/ui/ui_wallpaper.c main/ui/ui_wallpaper_rotation.c \
    main/book/book_layout.c main/book/reader_input.c main/book/markdown_table.c main/book/html_text.c main/book/markdown_text.c components/md4c/md4c.c main/cloud/cloud_protocol.c \
    build/simulator/font.S -lm -lz -lexpat -framework CoreFoundation -framework CoreGraphics -framework ImageIO -Wl,-dead_strip -o "$output"
mv "$output" build/simulator/render
