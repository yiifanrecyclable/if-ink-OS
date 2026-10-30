#!/bin/sh
# 中文：在主机上验证真实触控驱动及识别器。/ English: Exercise the actual touch driver and recognizer on the host.
set -eu
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -Itools/cst836u_stubs -Icomponents/cst836u/include -Imain/ui \
  -Itools/ui_gesture_stubs -Itools/simulator/include -Itools/water_turn_stubs \
  tools/cst836u_host_test.c components/cst836u/cst836u.c main/ui/ui_gesture.c -o "$out/touch"
"$out/touch"
