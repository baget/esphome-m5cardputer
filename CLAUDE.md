# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository purpose

[ESPHome](https://esphome.io/) YAML configurations for the [M5Stack Cardputer](https://docs.m5stack.com/en/core/Cardputer) (ESP32-S3). There is no application code — only declarative device configs compiled and flashed by the `esphome` CLI.

## Configurations

- [m5cardputer-basic.yaml](m5cardputer-basic.yaml) — working baseline: display, media player (speaker), microphone, GPIO0 button, Wi-Fi, Home Assistant API.
- [m5cardputer-voice-assistant.yaml](m5cardputer-voice-assistant.yaml) — WIP: same hardware plus `voice_assistant:` wired to the Home Assistant Assist pipeline, with the GPIO0 button as push-to-talk.

Both target `esp32-s3-devkitc-1` on the **esp-idf** framework. This is required by the current `media_player.speaker` platform (the old `media_player.i2s_audio` was removed upstream). Switching back to arduino would break the media player and is a deliberate change, not a cleanup.

Audio output uses two components together: a `speaker` (`platform: i2s_audio`, external DAC on GPIO42) and a `media_player` (`platform: speaker`) that references it by id. The speaker block is required — it is the thing driving GPIO42 — not optional.

## Common commands

ESPHome is the only toolchain. Install with `pip install esphome` (or use the Docker image / Home Assistant add-on).

```bash
esphome compile m5cardputer-basic.yaml          # validate + build firmware
esphome run     m5cardputer-basic.yaml          # compile, upload (USB or OTA), then tail logs
esphome logs    m5cardputer-basic.yaml          # tail logs from a flashed device
esphome config  m5cardputer-basic.yaml          # show fully-resolved config (substitutions, secrets)
esphome clean   m5cardputer-basic.yaml          # wipe .esphome/ build cache for this config
```

There are no tests, no linter, no CI. `esphome config` is the closest thing to a lint — it surfaces schema errors without a full build.

## Secrets

Both YAMLs reference `!secret api_key`, `!secret ota_password`, `!secret wifi_ssid`, `!secret wifi_password`, `!secret ap_password`. ESPHome resolves these from a `secrets.yaml` in the same directory. That file is **not** committed and is not currently gitignored — do not add real secrets to a tracked file. If creating `secrets.yaml`, add it to `.gitignore` first.

## Hardware pinout — do not change without a datasheet

The GPIO assignments in these YAMLs are dictated by the Cardputer's onboard wiring and are easy to break:

- I2S audio: `LRCLK=GPIO43`, `BCLK=GPIO41`, mic `DIN=GPIO46` (PDM), speaker `DOUT=GPIO42`
- SPI display (ST7789V, 135×240, rotation 90): `CLK=GPIO36`, `MOSI=GPIO35`, `MISO=GPIO39`, `CS=GPIO37`, `DC=GPIO34`, `RST=GPIO33`, `BL=GPIO38`
- User button: `GPIO0` (inverted, active-low)

Treat these as fixed. GPIO42 is driven by the `speaker` component, which the `media_player` (`platform: speaker`) references — do not add a second component that drives the same pin.

## Known-broken / not-yet-supported

Per the README: the Cardputer's matrix **keyboard is not working** in ESPHome. There is no upstream `m5stack_cardputer_keyboard` component; adding keyboard support requires an external custom component, not just YAML. Don't promise it works.
