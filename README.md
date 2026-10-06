# CYD 2432S028 SD Launcher — bare-bones JSON loader + game booter

Bare-bones firmware currently flashed on your CYD (v3). It mounts the SD card
(VSPI MOSI 23 / MISO 19 / SCK 18 / CS 5) and lets you browse files on the
320x240 touch screen and over USB serial (115200).

## What it does
- Lists files on SD (display + serial).
- Loads JSON files: tap a `.json` (or `.conf`/`.js`/`.txt`) to view it.
  JSON is parsed with ArduinoJson and pretty-printed; parse errors are shown.
- Boots `.bin` games from SD: tap a `.bin` → YES → OTA flash + reboot.
  Verified: `flash /invaders.bin` from SD rebooted straight into INVASION 2030.
- Deletes files: open a file, tap DEL → YES. Or over serial: `rm <file>`.
- Uploads files to SD over USB serial: `put <path> <size>` then stream the
  bytes; `crc <file>` verifies with CRC32. This is how the game bins below
  were written to the card without removing it.

## What's on the SD card now (verified with CRC32 over serial)
- `/frank-tunnel-run.bin` (463072 bytes) — FRANK TUNNEL RUN, boots OK
- `/invaders.bin` (375168 bytes) — INVASION 2030, boots OK (also SD-boot tested)
- `/launcher.bin` (this launcher v3 — tap it + YES to come back after a game)

The old Bruce leftovers (12 `.js` apps, `bruce.conf`, `brucePins.conf`,
`BruceAppStore/`) were deleted with `rmbruce`.

`sd-image/` in this repo holds the same three binaries as spares.

## Touch UI (v2+, resistive-friendly)
- 5 big rows; short tap = open, drag/swipe anywhere = scroll (no precision needed).
- Bottom bars are 44–50px tall: `^ UP` / `v DN` / `BACK` in the list;
  `^ UP` / `v DN` / `DEL` / `X` in the viewer.
- Tapping the top header bar refreshes (list) or exits (viewer) — a second way
  out if a button tap ever misses. Every tap/swipe is echoed on serial
  (`tap x,y ...`, `swipe dy=...`) for diagnosis.

Serial commands: `ls [p]` | `cat <f>` | `json <f>` | `flash <bin>` | `put <f> <size>` | `crc <f>` | `rm <f>` | `rmbruce` | `reboot`

## Refreshing the bins on the SD card (no need to remove it)
```
# reset CYD, wait for the launcher banner, then e.g.:
put /invaders.bin 375168     # device replies READY
# ... stream exactly 375168 raw bytes at 115200 baud ...
crc /invaders.bin            # compare with PC-side CRC32
```

## Re-flash via USB
```
./flash.sh                # launcher
pio device monitor -b 115200   # (use pyserial, pio monitor needs a tty)
```
