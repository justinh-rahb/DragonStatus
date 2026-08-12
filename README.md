# DragonStatus

Open firmware for the BTT Panda Status family (Panda Status, Panda Lux RGB PX,
and PopStatus), built on Dragon core networking and printer integrations.

## Current bootstrap

The ESP32-C3 project uses the factory 4 MiB partition contract and is intended
for stock-web-UI OTA installation: it never replaces the bootloader or table.
It reuses `dragon-core` for Wi-Fi/AP fallback, stock NVS migration, Bambu LAN,
Moonraker, source selection, event logging, captive provisioning, OTA and the
shared Dragon browser surface.

`components/dc_lighting` is a generic WS2812 renderer staged here for its first
consumer. It will move unchanged to `dragon-core` once DragonVent consumes it.
`ds_lighting` owns Status palette/state policy. RGB GPIOs are deliberately not
enabled until confirmed on actual Status hardware or through stronger RE evidence.

The initial Status policy reproduces the documented factory H2D behavior: blue
flow while unbound, breathing white at idle/paused, orange flow while preparing,
progress fill while printing, green completion, and blinking red on failure.
See [OEM effects and RE notes](docs/OEM_EFFECTS_AND_RE.md) for the current
evidence and remaining pinout work.

Build with `tools/idf-build.sh . esp32c3 build` after installing ESP-IDF 5.3+.
