#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# 中文：宿主验证，无固件打包。/ English: Host verification without firmware packaging.
set -eu
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Imain/ui tools/ui_i18n_host_test.c main/ui/ui_i18n.c -o "$out/test"
"$out/test"
