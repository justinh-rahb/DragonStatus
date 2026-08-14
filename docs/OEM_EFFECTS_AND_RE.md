# OEM behavior and reverse-engineering notes

## Confirmed factory behavior

The Panda Status documentation describes two factory modes:

| Mode | Behavior |
|---|---|
| Music | Audio-reactive palette: blue → green → yellow → orange → red as microphone level rises. |
| H2D | Unbound: blue flow. Idle/paused: white breathe. Downloading: yellow flow. Preparing: yellow-orange flow. Printing: a solid color filled to print progress. Complete: green hold, then idle. Error: red blink. |

The 1.0.1 release notes extend the completion hold to 15 minutes and add a
120-minute non-Music idle standby. DragonStatus retains both as configurable
policy rather than hard-coding them into the renderer: they are the
`complete_hold_min` and `standby_min` lighting settings, defaulting to the
factory 15 and 120 minutes, and either is disabled by setting it to zero.

## Live OEM portal contract

The stock 1.0.1 Panda Status portal is a self-contained page backed by a
WebSocket at `/ws`. A connection without sending any commands provides the
current Wi-Fi, printer, and settings snapshot. Lighting settings are reported
in `settings.list2`: slot zero is Music and slot one is H2D. The H2D slot
contains an ordered `rgb_rgba` palette of **idle, printing, error**, plus a
per-slot brightness value. This confirms that these three colours are OEM
semantic settings rather than DragonStatus inventions.

The portal's normal firmware uploader POSTs raw application data to `/ota`
with `Content-Type: application/octet-stream` and an `OTA-Type: ota_fw`
header. That is useful evidence for a future stock-compatible network update
path; DragonStatus retains its existing Core OTA mechanism today.

## Stock app evidence

`backups/stock-vent-20260731-002251.bin` is a 4 MiB ESP32-C3 flash image. It
has two valid OTA app images: `app0` at `0x10000` and `app1` at `0x200000`,
each occupying `0x1f0000` bytes. `app0` identifies as `panda_status`, built
from ESP-IDF `v5.3.1-dirty` on 2025-06-28; `app1` is a later 2025-09-20 build.

`app0` embeds the RGB source path `./main/rgb/app_rgb.c`, the RMT API
expressions `rmt_new_tx_channel`, `rmt_new_led_strip_encoder`, and
`rmt_transmit`, and the board configuration path
`components/my_board/my_board_v1_0/board_pins_config.c`. It also embeds
ESP-ADF audio/I2S component paths. The image has been reconstructed as a
segment-mapped ELF for Ghidra, with IROM at `0x42000020`, DROM at
`0x3c0e0020`, IRAM at `0x40380000`, and DRAM at `0x3fc8f400`.

The active known NVS namespaces are `rgb_sundry`, `rgb_data`, `drgb_data`, and
`music_bg_data`. Preserve these until their structures are decoded; do not
overwrite them during an OTA migration.

## Pinout status

The OEM Panda Status `app_rgb.c` initializer has now been decompiled. Its
`rmt_tx_channel_config_t` stack initializer sets `gpio_num = 4`,
`resolution_hz = 10,000,000`, `mem_block_symbols = 64`, and transmit queue
depth 4. The actual RGB transmit wrapper repeatedly sends a `0x4b`-byte GRB
framebuffer while its effect routines iterate `0x19` pixels, confirming a
**25-pixel WS2812 output on GPIO4**. A later PopStatus capture identifies as
`u1_status_2026_05_15` and has a 27-pixel physical bar. The common
DragonStatus production profile therefore emits 27 GRB records on GPIO4:
shorter one-way WS2812 chains discard the final two safely. The earlier
nine-byte call in the initializer is unrelated indicator metadata, not the RGB
framebuffer. The `status` sdkconfig profile selects the recovered map; the normal
development-C3 profile remains GPIO8 with its single on-board pixel.

The recovered map was verified on the live Panda Status unit through the stock
OTA handoff and DragonStatus's subsequent Core OTA path. A solid-red test frame
covered the entire physical bar uniformly; the profile was then restored to
the factory H2D palette. The stock portal's H2D slot reports 25% brightness,
which is DragonStatus's fresh-install default (64/255).

## Confirmed microphone path

The stock `board_pins_config.c` helpers and ESP-IDF's legacy
`i2s_pin_config_t` ordering recover the complete audio map:

| Function | GPIO |
|---|---:|
| ES8311 I2C SDA / SCL | 2 / 3 |
| I2S MCLK / BCLK / WS | 0 / 1 / 4 |
| I2S DOUT / DIN | 7 / 10 |

DragonStatus initializes the ES8311 as an analog microphone codec and opens a
16 kHz full-duplex I2S channel. Opening TX and RX together is important: the
TX side supplies the shared master clocks even though Music mode only consumes
RX. The map was verified on the live Panda Status unit: I2C codec init passed,
I2S reads returned 1024-byte PCM frames, and the captured room audio drove the
Dragon Core audio meter. The production renderer is Dragon Core's shared
`dc_lighting` component; Status contributes only the 25/27-pixel board topology
and microphone bridge. Its canonical Music meter effect is `10`. Earlier
experimental Status builds used local value `7`; firmware migrates that saved
setting to the shared value on boot. The codec/pin layer remains deliberately
product-specific pending a Core capture-source interface suitable for other
Panda products.
