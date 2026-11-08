#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# 测试使用临时TF卡和NVS，不触碰实时预览。/ Tests use temporary TF/NVS storage without touching the live preview.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/document-tests
cc -DPICO_HOST_SIMULATOR=1 "-DPICO_SIM_CARD_PATH=\"$(dirname "$PWD")/TF卡\"" \
 -std=gnu11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter \
 -g -fsanitize=address,undefined -fno-omit-frame-pointer -include tools/simulator/fs.h \
 -Itools/simulator/include -Imain -Imain/book -Icomponents/md4c \
 tools/document_host_test.c tools/simulator/fs.c tools/simulator/nvs.c \
 main/book/document_store.c main/book/document_image.c main/book/book_markdown.c main/book/book_title.c \
 main/book/book_progress.c main/book/book_progress_kv.c main/book/record_store.c main/book/markdown_text.c main/book/html_text.c components/md4c/md4c.c \
 -lz -o build/document-tests/documents
ASAN_OPTIONS=detect_leaks=0 PYTHONDONTWRITEBYTECODE=1 \
 "${PICO_TEST_PYTHON:-python3}" tools/test_document_host.py
