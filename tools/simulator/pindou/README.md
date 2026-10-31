# 数字拼豆 / Cyber Beads

主模拟器“应用 → 数字拼豆”直接运行与真机共用的C代码。下面的独立网页原型仍可单独预览，但不模拟实体墨水屏刷新速度。
The main simulator runs production C via Apps → Cyber Beads. The standalone web prototype below remains available, without physical e-paper refresh emulation.

```sh
python3 tools/simulator/transfer_preview.py --port 8768 --session build/os016-preview/session
```

打开 `http://127.0.0.1:8768/pindou`；WiFi管理预览首页也有独立入口。会话目录必须已有隔离模拟卡，不能指定真实TF卡。
Open that URL; the WiFi preview also links to it. The session must contain an isolated simulator card, never the real TF card.

- 17×27点阵、六阶圆环、相邻豆间距2px；同色再点移除、异色替换，滑动不连续涂画。
- 保存与最后成功保存的阵列一致时不新增文件；选色不算修改。
- 工具栏按退出、撤回、六阶候选豆、清空、保存排列，均用图标。最多撤回32步；普通编辑只记录格子编号和旧颜色，清空确认后作为一步操作，可以撤回恢复。
- 退出、模拟返回/Home键和Escape共用未保存确认；浏览器离页使用原生未保存提示。
- 保存为684×1216、4位灰度PNG，编辑工具不进入图片，底部署名为 `if-ink OS @ Pico`。
- 输出在会话模拟卡 `pictures/pindou/pindou-NNNN.png`，先写临时文件再提交，不覆盖旧作品；失败保留当前编辑状态。

17×27 cells with 2px bead gaps, six shades, toggle/recolor by tap, no drag painting. The compact icon toolbar orders Exit, Undo, six shades, Clear and Save. Undo keeps at most 32 records, with index/old-color pairs for normal edits; confirmed clearing is one recoverable step. Saving compares against the last saved model; selecting a shade is not an edit. Exit/Back/Home/Escape share the unsaved prompt, while browser navigation uses its native warning. Exports are full-resolution 4-bit grayscale PNGs with a mid-gray footer signature instead of the editing toolbar, written transactionally to the isolated session card without overwriting artwork.

核心规则和导出检查 / Core and export checks:

```sh
node tools/test_pindou.mjs
python3 tools/test_pindou_export.py
python3 tools/test_transfer_preview.py
```

真机版使用PSRAM保存固定大小画布和32步撤回；普通编辑只刷新39×39格子，工具状态变化时另刷新底栏。保存逐行压缩为4-bit灰度PNG，目录为 `/sdcard/pictures/pindou`。实体键退出也受未保存提示保护。
The native app keeps its bounded board and 32-step history in PSRAM, refreshes changed 39×39 cells and updates tools only when needed. PNG export streams rows to `/sdcard/pictures/pindou`; physical-key exit also checks unsaved edits.

验证：`cc -Imain/apps tools/cyber_beads_host_test.c main/apps/cyber_beads.c -o /tmp/beads-test && /tmp/beads-test`，`bash tools/simulator/build.sh && python3 tools/test_apps_beads.py`。后者使用临时隔离TF卡。
Validation commands above exercise the production model and isolated native UI/PNG export.

真机版保存时同时保留可编辑画布，下次进入恢复最近保存的残局及选色（撤回历史不跨会话）。选择“不保存退出”只放弃未保存的修改。隐藏双槽状态文件放在作品目录内，损坏时尝试另一槽；不从PNG反推编辑状态。
Native saves also retain the editable board and selected shade for the next session; undo history is session-only. Discarding edits preserves the previously saved board. Hidden alternating records sit beside artworks and provide a corruption fallback; PNGs are not decoded back into editable state.
