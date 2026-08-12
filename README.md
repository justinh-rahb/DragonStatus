# DragonStatus

DragonStatus is open firmware for the BTT Panda Status product family:

- Panda Status
- Panda Lux RGB PX
- PopStatus

These products share an ESP32-C3 board and the same functional role despite
their enclosures and possible LED-strip-length differences. DragonStatus keeps
the familiar factory status-light experience, then adds the Dragon family's
printer integrations, configuration experience, and long-term maintainability.

> **Early firmware:** the project compiles and preserves the factory flash
> contract, but the board-specific RGB and microphone pins are still being
> recovered from stock firmware. It intentionally does **not** drive LEDs on
> hardware yet. Do not treat this repository as a ready-to-flash release.

## Goals

- Retain and improve the factory status, progress, error, and music-reactive
  lighting behaviors.
- Work natively with Bambu Lab LAN/MQTT printers and Klipper/Moonraker.
- Fit cleanly into Home Assistant installations.
- Keep Wi-Fi provisioning, captive AP fallback, OTA installation, and a local
  browser UI available without a separate host.
- Remain compatible with stock units: retain their bootloader, partition table,
  OTA layout, and existing NVS data.
- Contribute reusable hardware-independent lighting pieces back to
  [`dragon-core`](https://github.com/justinh-rahb/dragon-core), so DragonVent
  can use the same renderer rather than accumulating product-specific forks.

## Current state

The bootstrap firmware already includes:

- factory-compatible 4 MiB partition layout with two OTA application slots;
- Dragon Core Wi-Fi setup, captive AP fallback, stock-NVS migration, event log,
  Bambu LAN/MQTT, Moonraker, source selection, OTA, and browser portal;
- a generic RMT/WS2812 renderer in `components/dc_lighting`;
- DragonStatus printer-state-to-light-policy mapping in
  `components/ds_lighting`; and
- a conservative board layer in `components/ds_board` that enables **zero**
  outputs until GPIO recovery is confirmed.

The current policy implements the documented factory H2D intent:

| Printer state | Light behavior |
| --- | --- |
| Not bound / unknown | Blue flow |
| Preparing | Yellow-orange flow |
| Printing | White progress fill |
| Idle or paused | White breathe |
| Complete | Green |
| Error | Red blink |

Music mode remains a distinct audio-reactive policy: blue through red as sound
level rises. The factory behavior and the evidence behind this mapping are
recorded in [OEM effects and RE notes](docs/OEM_EFFECTS_AND_RE.md).

## Architecture

```text
printer source (Bambu MQTT or Moonraker)
                 |
                 v
       dragon-core source selection
                 |
                 v
      ds_lighting state/palette policy
                 |
                 v
      dc_lighting generic WS2812 engine
                 |
                 v
     ds_board verified board GPIO mapping
```

`dc_*` components are deliberately generic. `ds_*` components are
DragonStatus-specific and are the only appropriate home for product policy,
board mapping, and future Status-only UI behavior. This keeps the lighting
engine transferable to DragonVent and other Dragon projects.

## Factory compatibility and safety

DragonStatus uses the stock layout:

| Partition | Offset | Size |
| --- | ---: | ---: |
| NVS | `0x9000` | `0x3000` |
| OTA data | `0xc000` | `0x2000` |
| Application 0 | `0x10000` | `0x1f0000` |
| Application 1 | `0x200000` | `0x1f0000` |
| Core dump | `0x3f0000` | `0x1000` |

The intended installation path is an application-only update using the stock
bootloader and partition table. Existing NVS data is preserved; stock RGB
namespaces are specifically left alone until their binary formats are decoded.
Never erase the full flash or overwrite the bootloader/partition table as part
of normal DragonStatus installation.

## Building

Prerequisites:

- ESP-IDF 5.3 or later, including the ESP32-C3 toolchain;
- Git, with access to the Dragon Core component dependencies.

From the repository root:

```sh
tools/idf-build.sh . esp32c3 build
```

The resulting application image is `build/dragonstatus.bin`. The build is
checked against the stock-sized `0x1f0000` application slots. The current
firmware uses approximately 53% of one slot.

The generated ESP-IDF flash command includes bootloader and partition artifacts
for development convenience. It is **not** the normal stock-device installation
procedure; production flashing instructions will be added only after hardware
validation and an OTA package flow are complete.

## Reverse engineering status

The local stock backup is a full 4 MiB ESP32-C3 image with two valid OTA apps.
Its active app identifies as `panda_status`, embeds ESP-IDF 5.3.1, the RMT
LED-strip APIs, ESP-ADF/I2S audio paths, and a `my_board_v1_0` configuration
path. It has been reconstructed into a segment-mapped ELF for Ghidra analysis.

The remaining hardware work is precise: identify the application callers that
construct the RMT and I2S channels, recover their configuration structs, and
verify the results on hardware. Until then, driving an assumed GPIO would be
unsafe and is explicitly blocked by the firmware.

See [OEM effects and RE notes](docs/OEM_EFFECTS_AND_RE.md) for the evidence
log, factory behavior reference, and safe next steps.

## Related projects

- [DragonBreath](https://github.com/plastikman/DragonBreath) — the reference
  for stock-compatible Dragon firmware behavior and Bambu MQTT integration.
- [DragonVent](https://github.com/justinh-rahb/DragonVent) — the parallel
  Dragon firmware project whose lighting work is being made reusable here.
- [dragon-core](https://github.com/justinh-rahb/dragon-core) — shared Wi-Fi,
  printer-source, portal, event-log, and UI components.
- [OpenVent](https://github.com/justinh-rahb/OpenVent) — the original project
  lineage.

## Contributing

Keep reusable functionality in `dc_*` components and DragonStatus-specific
work in `ds_*` components. When changing board behavior, record the supporting
firmware or hardware evidence in `docs/`; do not turn an unverified pinout into
a default.
