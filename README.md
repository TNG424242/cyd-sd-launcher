# CYD 2432S028 SD Launcher — bare-bones JSON loader + game booter

Bare-bones firmware currently flashed on your CYD (v2). It mounts the SD card
(VSPI MOSI 23 / MISO 19 / SCK 18 / CS 5) and lets you browse files on the
320x240 touch screen and over USB serial (115200).

## What it does
- Lists files on SD (display + serial). The Bruce leftovers previously on the
  card (12 `.js` apps, `bruce.conf`, `brucePins.conf`, `BruceAppStore/`) have
  been deleted with `rmbruce` — the card root is now empty, ready for game bins.
- Loads JSON files: tap a `.json` (or `.conf`/`.js`/`.txt`) to view it.
  JSON is parsed with ArduinoJson and pretty-printed; parse errors are shown.
- Boots `.bin` games from SD: tap a `.bin` → YES → OTA flash + reboot.
- Deletes files: open a file, tap DEL → YES. Or over serial: `rm <file>`.

## Touch UI (v2, resistive-friendly)
- 5 big rows; short tap = open, drag/swipe anywhere = scroll (no precision needed).
- Bottom bars are 44–50px tall: `^ UP` / `v DN` / `BACK` in the list;
  `^ UP` / `v DN` / `DEL` / `X` in the viewer.
- Tapping the top header bar refreshes (list) or exits (viewer) — a second way
  out if a button tap ever misses. Every tap/swipe is echoed on serial
  (`tap x,y ...`, `swipe dy=...`) for diagnosis.

Serial commands: `ls [path]` | `cat <file>` | `json <file>` | `flash <bin>` | `rm <file>` | `rmbruce` | `reboot`

## Bootable game binaries (in `sd-image/`, also verified via USB flash)
- `frank-tunnel-run.bin` (453 KB) — FRANK TUNNEL RUN, boots OK
- `invaders.bin` (367 KB) — INVASION 2030, boots OK
- `launcher.bin` (425 KB) — this launcher v2 (keep on SD to come back)

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
