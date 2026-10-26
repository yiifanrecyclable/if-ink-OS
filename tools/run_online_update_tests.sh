#!/bin/sh
# 中文：只编译宿主故障注入，不生成或安装固件。/ Build host fault injection only; never generate/install firmware.
set -eu
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
cc -std=gnu11 -Wall -Wextra -Werror -Wno-deprecated-declarations -fsanitize=address,undefined -g \
 -Itools/ota_stubs -Itools/simulator/vendor/cjson -Imain -Imain/cloud -Imain/apps -Imain/book \
 tools/online_update_host_test.c main/ui/ui_i18n.c main/ota_online_policy.c tools/simulator/vendor/cjson/cJSON.c -o "$out/test"
"$out/test"
