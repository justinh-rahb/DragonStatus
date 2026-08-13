# OEM behavior and reverse-engineering notes

## Confirmed factory behavior

The Panda Status documentation describes two factory modes:

| Mode | Behavior |
|---|---|
| Music | Audio-reactive palette: blue → green → yellow → orange → red as microphone level rises. |
| H2D | Unbound: blue flow. Idle/paused: white breathe. Downloading: yellow flow. Preparing: yellow-orange flow. Printing: a solid color filled to print progress. Complete: green hold, then idle. Error: red blink. |

The 1.0.1 release notes extend the completion hold to 15 minutes and add a
120-minute non-Music idle standby. DragonStatus will retain the behavior as
configurable policy rather than hard-code it into the renderer.

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

The OEM `app_rgb.c` initializer has now been decompiled. Its
`rmt_tx_channel_config_t` stack initializer sets `gpio_num = 4`,
`resolution_hz = 10,000,000`, `mem_block_symbols = 64`, and transmit queue
depth 4. It subsequently sends a nine-byte GRB framebuffer, confirming a
three-pixel WS2812 output on **GPIO4**. The `status` sdkconfig profile selects
that recovered map; the normal development-C3 profile remains GPIO8 with its
single on-board pixel.

The microphone/I2S pins are still under RE. The next pass follows the audio
channel setup and extracts those configuration structs.
