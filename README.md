# DragonStatus

DragonStatus is open firmware for the BTT Panda Status product family:

- Panda Status
- Panda Lux RGB PX
- PopStatus

These products share an ESP32-C3 board and the same functional role despite
their enclosures and possible LED-strip-length differences. DragonStatus keeps
the familiar factory status-light experience, then adds the Dragon family's
printer integrations, configuration experience, and long-term maintainability.

> **Early firmware:** the project preserves the factory flash contract. The
> production RGB output and ES8311 microphone path have been recovered and
> validated on Panda Status hardware; keep a private stock backup before
> installing experimental builds.

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
- the generic RMT/WS2812 renderer from Dragon Core's `dc_lighting`
  component (currently pinned to Core `v0.19.0`);
- DragonStatus printer-state-to-light-policy mapping in
  `components/ds_lighting`; and
- a Status ES8311/I2S audio adapter in `components/ds_audio`, which supplies a
  normalized microphone level to the reusable Core audio-meter renderer; and
- a board layer in `components/ds_board` with an explicit development-C3
  preview output plus the verified production Status RGB map.

Status-only lighting settings are stored in their own `dragonstatus` NVS
namespace. Existing installations are migrated once from the legacy shared
`app_nvs` key, so changing a printer source cannot disturb the selected effect
or palette.

The current policy implements the documented factory H2D intent, and every part
of it is configurable. The factory colours below are the defaults; each printer
state carries its own palette entry, and the animation per state is the factory
policy until an effect override replaces it everywhere.

| Printer state | Light behavior (default) |
| --- | --- |
| Not bound / unknown | Blue flow |
| Downloading | Yellow flow |
| Preparing | Yellow-orange flow |
| Printing | White progress fill |
| Idle | White breathe |
| Paused | White breathe |
| Complete | Green, held for the completion time, then idle |
| Error | Red blink |

Two knobs shape the palette, matching DragonVent's lighting controls. *Colour
follows* selects either the per-state palette or one fixed colour — an error
keeps its own colour either way, so a fault stays recognisable. *Effect* stays
on **Factory H2D** to keep the per-state animations above, or forces a single
animation over every state. Brightness, effect speed, and strip direction apply
as soon as they are saved.

The two documented OEM 1.0.1 timers are settings rather than constants: the
completion hold (15 minutes by default) and the non-Music idle standby that
blanks the bar after a long idle (120 minutes by default). Zero disables either.
Note that the standby is a behaviour change on upgrade rather than a preserved
default: earlier DragonStatus builds documented it but never implemented it, so
a unit left idle now goes dark where it previously kept breathing. A blanked
bar wakes when the printer changes state, and when lighting settings are saved,
so a change is always visible immediately.

Music mode remains a distinct audio-reactive policy: blue through red as sound
level rises. In the shared renderer it is the canonical **Music meter** effect;
Status exposes that renderer through its stable **Music meter** choice. The
factory behavior and the evidence behind this mapping are recorded in [OEM
effects and RE notes](docs/OEM_EFFECTS_AND_RE.md).

## Architecture

```text
printer source (Bambu MQTT or Moonraker)     Status ES8311 microphone
                 |                                      |
                 v                                      v
       dragon-core source selection                  ds_audio adapter
                 |                                      |
                 v                                      |
      ds_lighting state/palette policy <---------------+
                 |
                 v
   dragon-core dc_lighting WS2812/audio-meter engine
                 |
                 v
     ds_board verified board GPIO mapping
```

`dc_*` components are deliberately generic and are consumed from
[`dragon-core`](https://github.com/justinh-rahb/dragon-core). `ds_*`
components are DragonStatus-specific and are the only appropriate home for
product policy, board mapping, microphone wiring, and future Status-only UI
behavior. This keeps the lighting engine transferable to DragonVent and other
Dragon projects while allowing their strip layouts to differ.

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

## Backing up a PopStatus safely

Before anyone installs experimental firmware, make a stock backup. On a Linux
or Raspberry Pi host with the device connected over USB:

```sh
git clone https://github.com/justinh-rahb/DragonStatus.git
cd DragonStatus
./scripts/backup-pop-status.sh /dev/ttyUSB0
```

Use `/dev/ttyACM0` instead when that is the device's serial port. The script
only reads the ESP32-C3: it makes a verified private 4 MiB recovery image and a
second `-nvs-redacted.bin` copy for sharing with us. The redacted copy has the
whole NVS partition cleared, removing Wi-Fi/Bambu credentials and any other
stored NVS values. Never send the private full image.

The script finds an existing `esptool` installation or creates an isolated
tool environment under `~/.cache` and installs both `esptool` and `pyserial`.
On a minimal Raspberry Pi OS/Debian install, it will tell the user to install
the one prerequisite first:

```sh
sudo apt update && sudo apt install -y python3 python3-venv python3-pip git
```

If Linux reports serial-port permission errors, add the current user to
`dialout` (`sudo usermod -aG dialout "$USER"`) and sign out and back in. The
script does not write, erase, or reset flash on the device. The NVS-redacted
copy is intentionally not an exact restore image: keep the private full backup
somewhere safe.

## Building

Prerequisites:

- ESP-IDF 5.3 or later, including the ESP32-C3 toolchain;
- Git, with access to the Dragon Core component dependencies.

From the repository root:

```sh
tools/idf-build.sh . esp32c3 build
```

The resulting application image is `build/dragonstatus.bin`. The build is
checked against the stock-sized `0x1f0000` application slots. The shared-
topology firmware uses approximately 56% of one slot.

The generated ESP-IDF flash command includes bootloader and partition artifacts
for development convenience. It is **not** the normal stock-device installation
procedure; production flashing instructions will be added only after hardware
validation and an OTA package flow are complete.

## Reverse engineering status

The local stock backup is a full 4 MiB ESP32-C3 image with two valid OTA apps.
Its active app identifies as `panda_status`, embeds ESP-IDF 5.3.1, the RMT
LED-strip APIs, ESP-ADF/I2S audio paths, and a `my_board_v1_0` configuration
path. It has been reconstructed into a segment-mapped ELF for Ghidra analysis.

The recovered production map has been validated on Status hardware. The RGB
RMT configuration is GPIO4 with a GRB/WS2812 chain. Panda Status uses 25 pixels; the newly
captured PopStatus has 27. The production renderer emits the safe 27-pixel
superset—WS2812 chains have no feedback path and a 25-pixel bar simply ignores
the last two records. Normal builds continue to target the development C3's
GPIO8 preview LED; a production image is explicitly selected with
`bash tools/idf-build.sh . esp32c3 build-status status`. The microphone/I2S
path is the OEM ES8311 at I2C SDA=GPIO2/SCL=GPIO3 and the recovered legacy I2S
map MCLK=GPIO0, BCLK=GPIO1, WS=GPIO4, DOUT=GPIO7, DIN=GPIO10. It captures a
16 kHz stereo stream and sends its filtered 0..1 level to Core.

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
