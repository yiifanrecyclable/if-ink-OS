/* SPDX-License-Identifier: Apache-2.0
 * 中文：从原始标定表派生自然页面与纯文字序列，不修改原表、电压和扫描时序。
 * English: Derive natural-page and text sequences without editing vendor tables, voltage or scan timing.
 */
#pragma once
#include "epdiy.h"
const EpdWaveform *display_natural_waveform(void);
const EpdWaveform *display_art_waveform(void);
const EpdWaveform *display_text_waveform(void);
