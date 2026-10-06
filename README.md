# CYD 2432S028 SD Launcher — bare-bones JSON loader + game booter

Bare-bones firmware currently flashed on your CYD. It mounts the SD card
(VSPI MOSI 23 / MISO 19 / SCK 18 / CS 5) and lets you browse files on the
320x240 touch screen and over USB serial (115200).

## What it does
- Lists files on SD (display + serial). Found on your card:
  `/*.js` (12 Bruce apps), `/bruce.conf`, `/brucePins.conf`,
  `/BruceAppStore/installed.json`
- Loads JSON files: tap a `.json` (or `.conf`/`.js`/`.txt`) to view it.
  JSON is parsed with ArduinoJson and pretty-printed; parse errors are shown.
  `^/v` scroll, `X` closes, `BACK` goes up a directory.
- Boots `.bin` games from SD: tap a `.bin` → YES → OTA flash + reboot.

Serial commands: `ls [path]` | `cat <file>` | `json <file>` | `flash <bin>` | `reboot`

## Bootable game bins (in `sd-image/`, also verified via USB flash)
- `frank-tunnel-run.bin` (453 KB) — FRANK TUNNEL RUN, boots OK
- `invaders.bin` (367 KB) — INVASION 2030, boots OK
- `launcher.bin` (422 KB) — this launcher (keep on SD to come back)

All three fit the OTA slot, so SD-boot via `Update` works.

## To store games on the SD card
1. Power off CYD, remove SD card, insert into PC.
2. Copy `sd-image/*.bin` to the SD root (FAT32):
   `launcher.bin`, `frank-tunnel-run.bin`, `invaders.bin`
3. Eject, reinsert into CYD, power on.
4. Tap `frank-tunnel-run.bin` or `invaders.bin` → YES to boot.
   To return, tap `launcher.bin` → YES (or re-flash via USB: `./flash.sh`).

## Re-flash via USB
```
./flash.sh                # launcher
pio device monitor -b 115200   # (use pyserial, pio monitor needs a tty)
```
