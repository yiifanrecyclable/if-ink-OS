#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/simulator
flags=(-DPICO_HOST_SIMULATOR=1 -DCJSON_NESTING_LIMIT=16)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer -g); fi
cc "${flags[@]}" -std=gnu11 -Wall -Wextra -Werror -Wno-deprecated-declarations -c tools/simulator/vendor/cjson/cJSON.c -o build/simulator/cJSON.o
cc "${flags[@]}" "-DPICO_SIM_CARD_PATH=\"$(dirname "$PWD")/TF卡\"" -std=gnu11 -D_DEFAULT_SOURCE \
 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter -Wno-sign-compare \
 -include tools/simulator/fs.h -Itools/simulator/include -Itools/simulator/vendor/cjson -Imain/cloud -Imain/book -Icomponents/md4c \
 tools/cloud_host.c build/simulator/cJSON.o main/cloud/cloud_protocol.c main/cloud/cloud_dav.c \
 tools/simulator/fs.c tools/simulator/nvs.c main/book/document_store.c main/book/document_image.c main/book/book_title.c \
 main/book/book_progress.c main/book/book_progress_kv.c main/book/record_store.c main/book/book_markdown.c main/book/markdown_text.c main/book/html_text.c components/md4c/md4c.c \
 -lz -lm -lexpat -o build/simulator/cloud-host
