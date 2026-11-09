#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# 使用真实字体实现和地址检查，不输出固件。/ Exercise production fonts under sanitizers, without firmware output.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/book-tests
cc -std=gnu11 -DPICO_HOST_SIMULATOR=1 -fsanitize=address,undefined -fno-omit-frame-pointer -g \
  -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable -Wno-sign-compare \
  -Itools/simulator/include -Imain -Imain/font -Imain/book -Imain/ui \
  tools/font_context_host_test.c tools/simulator/pixels.c build/simulator/font.S \
  -lm -o build/book-tests/font-context
build/book-tests/font-context main/assets/builtin.ttf
