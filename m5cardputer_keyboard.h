// Ported from m5stack/M5Cardputer (MIT licensed):
//   src/utility/Keyboard/Keyboard.h
//   src/utility/Keyboard/KeyboardReader/IOMatrix.{h,cpp}
// See repo: https://github.com/m5stack/M5Cardputer
//
// Scans the original (non-ADV) Cardputer's 74HC138-multiplexed 4x14
// keyboard and logs each newly-pressed key to the ESPHome log.
// Free-function API (no Component subclass): call init() once from
// on_boot, then scan() from a periodic interval.

#pragma once

#include <cstdint>
#include "esphome/core/log.h"

extern "C" {
#include "driver/gpio.h"
#include "esp_rom_sys.h"
}

namespace m5kb {

// 74HC138 address bits (A0, A1, A2). 3 outputs -> 8 column states.
static constexpr gpio_num_t output_pins[3] = {GPIO_NUM_8, GPIO_NUM_9, GPIO_NUM_11};

// Matrix row inputs with internal pull-ups. Active-low when key pressed.
static constexpr gpio_num_t input_pins[7] = {
    GPIO_NUM_13, GPIO_NUM_15, GPIO_NUM_3,
    GPIO_NUM_4,  GPIO_NUM_5,  GPIO_NUM_6, GPIO_NUM_7,
};

// Maps the 7 input-bit positions to the pair of physical x-columns
// (even/odd) they cover. From M5Cardputer's IOMatrix.h X_map_chart.
struct ChartEntry { uint8_t x_even; uint8_t x_odd; };
static constexpr ChartEntry X_map[7] = {
    {0, 1}, {2, 3}, {4, 5}, {6, 7}, {8, 9}, {10, 11}, {12, 13},
};

// String labels for each physical key, base and shifted.
struct KeyLabel { const char *base; const char *shifted; };
// Ported verbatim from m5stack/M5Cardputer
// src/utility/Keyboard/Keyboard.h _key_value_map[4][14]:
//   Row 0: ` 1 2 3 4 5 6 7 8 9 0 - = Backspace
//   Row 1: Tab q w e r t y u i o p [ ] backslash
//   Row 2: Fn Shift a s d f g h j k l ; ' Enter
//   Row 3: Ctrl Opt Alt z x c v b n m , . / Space
// Modifier rows log their own name on press; Shift additionally
// feeds the shift_held flag inside scan().
inline const KeyLabel keymap[4][14] = {
  // Row 0
  {{"`", "~"}, {"1", "!"}, {"2", "@"}, {"3", "#"}, {"4", "$"},
   {"5", "%"}, {"6", "^"}, {"7", "&"}, {"8", "*"}, {"9", "("},
   {"0", ")"}, {"-", "_"}, {"=", "+"}, {"Backspace", "Backspace"}},
  // Row 1
  {{"Tab", "Tab"}, {"q", "Q"}, {"w", "W"}, {"e", "E"}, {"r", "R"},
   {"t", "T"}, {"y", "Y"}, {"u", "U"}, {"i", "I"}, {"o", "O"},
   {"p", "P"}, {"[", "{"}, {"]", "}"}, {"\\", "|"}},
  // Row 2
  {{"Fn", "Fn"}, {"Shift", "Shift"}, {"a", "A"}, {"s", "S"}, {"d", "D"},
   {"f", "F"}, {"g", "G"}, {"h", "H"}, {"j", "J"}, {"k", "K"},
   {"l", "L"}, {";", ":"}, {"'", "\""}, {"Enter", "Enter"}},
  // Row 3
  {{"Ctrl", "Ctrl"}, {"Opt", "Opt"}, {"Alt", "Alt"}, {"z", "Z"},
   {"x", "X"}, {"c", "C"}, {"v", "V"}, {"b", "B"}, {"n", "N"},
   {"m", "M"}, {",", "<"}, {".", ">"}, {"/", "?"}, {"Space", "Space"}},
};

// Shift's position in the physical (4 rows x 14 cols) layout.
// Used by scan() to decide whether to apply the shifted label.
// Row 2 col 1 in the upstream keymap.
static constexpr int SHIFT_ROW = 2;
static constexpr int SHIFT_COL = 1;

// 56-bit bitmask of last scan's pressed state.
// Bit index = row * 14 + col.
inline uint64_t &prev_pressed() {
  static uint64_t v = 0;
  return v;
}

inline void init() {
  // Outputs: 74HC138 address bits A0, A1, A2.
  gpio_config_t out_cfg = {};
  out_cfg.intr_type = GPIO_INTR_DISABLE;
  out_cfg.mode = GPIO_MODE_OUTPUT;
  out_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  out_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
  out_cfg.pin_bit_mask = 0;
  for (auto p : output_pins) out_cfg.pin_bit_mask |= (1ULL << p);
  gpio_config(&out_cfg);
  for (auto p : output_pins) gpio_set_level(p, 0);

  // Inputs: 7 matrix rows, pull-ups enabled. Active-low on key press.
  gpio_config_t in_cfg = {};
  in_cfg.intr_type = GPIO_INTR_DISABLE;
  in_cfg.mode = GPIO_MODE_INPUT;
  in_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  in_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  in_cfg.pin_bit_mask = 0;
  for (auto p : input_pins) in_cfg.pin_bit_mask |= (1ULL << p);
  gpio_config(&in_cfg);

  prev_pressed() = 0;

  ESP_LOGI("m5kb", "keyboard init: 3 outputs (8,9,11) + 7 inputs (13,15,3,4,5,6,7) ready");
}

inline void scan() {
  uint64_t new_pressed = 0;

  for (int i = 0; i < 8; i++) {
    // Drive the 3 address bits.
    gpio_set_level(output_pins[0], (i >> 0) & 1);
    gpio_set_level(output_pins[1], (i >> 1) & 1);
    gpio_set_level(output_pins[2], (i >> 2) & 1);
    // 74HC138 + matrix settle. 5us is generous; upstream uses zero.
    esp_rom_delay_us(5);

    for (int j = 0; j < 7; j++) {
      // Active-low: pressed key shorts row to the (low) column.
      if (gpio_get_level(input_pins[j]) == 0) {
        int y_scan = (i > 3) ? (i - 4) : i;
        int y_phys = 3 - y_scan;
        int x_phys = (i > 3) ? X_map[j].x_even : X_map[j].x_odd;
        new_pressed |= ((uint64_t)1 << (y_phys * 14 + x_phys));
      }
    }
  }

  // Shift state is computed from the new mask so a chord like Shift+a
  // (both newly detected in the same scan) decodes correctly.
  const uint64_t shift_bit = ((uint64_t)1 << (SHIFT_ROW * 14 + SHIFT_COL));
  const bool shift_held = (new_pressed & shift_bit) != 0;

  const uint64_t newly = new_pressed & ~prev_pressed();
  if (newly != 0) {
    for (int y = 0; y < 4; y++) {
      for (int x = 0; x < 14; x++) {
        if (newly & ((uint64_t)1 << (y * 14 + x))) {
          const KeyLabel &kl = keymap[y][x];
          ESP_LOGI("m5kb", "Key pressed: %s",
                   shift_held ? kl.shifted : kl.base);
        }
      }
    }
  }

  prev_pressed() = new_pressed;
}

}  // namespace m5kb
