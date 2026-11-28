# Deploying the TFT43 Grbl screen

This guide takes the 3018 from the stock MKS TS24 screen to the BigTreeTech TFT43 V3.0
running our Grbl firmware, end to end, and back again if needed.

There are two firmwares, and both have to be installed:

| Device | Firmware | How it is loaded |
|---|---|---|
| BTT TFT43 V3.0 (the screen) | `BIGTREE_TFT43_V3.0.grbl-<version>.bin` | microSD card in the TFT43, power on |
| MKS DLC32 V2.2 (the controller) | `full_flash_0x0.bin` from the `mks_dlc32_v2_1_tft43` build | USB cable, MKS Laser Tool (or esptool) |

The DLC32 build is needed because the stock DLC32 firmware drives the TS24 directly and has
no serial port for a screen. The `mks_dlc32_v2_1_tft43` build turns the TS24 code off and
makes two pins of the TS24 connector (EXP1) a Grbl serial port at 115200 baud.

Allow about an hour the first time. Nothing here changes the bootloaders, so every step
can be undone (see [Rolling back](#10-rolling-back)).

---

## 1. What you need

- The TFT43 V3.0 and the 3018 with its DLC32 board
- A microSD card, 32 GB or smaller, formatted **FAT32**
- A PC with a USB cable for the DLC32 (the board's USB-B / CH340 port)
- **MKS Laser Tool** (installers in this repo: `MKS-DLC32-main/firmware/tool/`, e.g. `MKSLaserTool_setupv3.1.6.rar`) and the CH340
  driver, or Python with `pip install esptool`
- A serial terminal to talk to Grbl: LaserGRBL, UGS, or the Arduino serial monitor at
  115200 baud with "Newline" line endings
- 4 female-to-female jumper wires (2.54 mm Dupont), or a 4-wire cable made up for it
- A multimeter (for checking the EXP1 pins before connecting)
- The stock files, for rolling back:
  - DLC32: `MKS-DLC32-main/firmware/TS24/CNC/Normal/Board_V2.0/V2.1.0_H24_20220511_01_N_CNC.bin`
    (this is byte-identical to what is on the board now)
  - TFT43: `BIGTREE_TFT43_V3.0.28.x.bin`, the `TFT43` folder and `config.ini` from
    BIGTREETECH-TouchScreenFirmware, folder "Copy to SD Card root directory to update"

## 2. Check which chip the TFT43 has

Turn the TFT43 over and read the large square chip.

- **STM32F207VCT6** (or similar "STM32F207"): carry on. (Paul's TFT43 is confirmed STM32F207VCT6, from photos 2026-11-28.) This is what the firmware is built for.
- **GD32F205VCT6**: **stop here.** The GD version uses a different bootloader layout and
  needs its own build. Our file would not start on it (it would sit on a blank or white
  screen; putting the stock BTT GD firmware back on the card recovers it). Ask for a GD build.

## 3. Download the firmware

Both files come out of the GitHub Actions builds of PR #2
(`https://github.com/Blackbox-UK/MKS-DLC32-CNC/pull/2`, tab **Checks**). Use the run for
the latest commit on the PR.

1. **Screen:** workflow **"TFT43 screen firmware"** → **Artifacts** →
   `tft43-firmware-<commit>`. Unzip it; the firmware is
   `build/BIGTREE_TFT43_V3.0.grbl-<version>.bin`. The same file is also in the project
   files folder `tft43-firmware/`.
2. **Controller:** workflow **"Firmware build"** → **Artifacts** →
   `firmware-mks_dlc32_v2_1_tft43-<commit>`. Unzip it; the file to flash is
   `full_flash_0x0.bin` (exactly 4 MB). Do not use the `mks_dlc32_v2_1_ts24` or
   `mks_dlc32_v2_1` artifacts: those keep the TS24/TS35 code and have no screen port.

## 4. Back up the DLC32 settings

Flashing a full image at address 0x0 also wipes the stored Grbl settings (steps/mm, max
travel, homing, spindle max...). Save them first.

1. Connect the DLC32 to the PC by USB and open the serial terminal at 115200 baud.
2. Send `$$`. Copy every line that starts with `$` (for example `$100=800.000`) into a text
   file, e.g. `dlc32-settings-backup.txt`.
3. Also send `$#` and note the `G54` line if you rely on a saved work offset.
4. Keep the file. You will paste it back in step 5.

## 5. Flash the DLC32

Power the machine's 12/24 V supply off for this step; USB powers the ESP32. Unplug the
TS24 screen cables from EXP1 and EXP2 now (it will not work with this build).

**With MKS Laser Tool** (Windows, as in `MKS-DLC32-main/doc/DLC32 Firmware Programming Instructions.pdf`):

1. Install the CH340 driver and MKS Laser Tool if they are not installed yet.
2. Plug in the DLC32, find its COM port (Device Manager → Ports).
3. In MKS Laser Tool set: SPI speed **80 MHz**, flash size **8 MB**, baud **115200**,
   the COM port, and choose `full_flash_0x0.bin` (it is written at 0x0).
4. Click to download and wait for the success message, then unplug USB.

**Or with esptool** (any OS):

```sh
esptool --chip esp32 --port COM5 --baud 460800 write_flash 0x0 full_flash_0x0.bin
```

(use `/dev/ttyUSB0` or similar instead of `COM5` on Linux/macOS; drop to `--baud 115200`
if it fails).

**Check it and restore the settings:**

1. Plug USB back in, open the serial terminal at 115200 baud. You should see a
   `Grbl 1.3a ['$' for help]` banner (press the board's reset button if you missed it).
2. Send `?`; you should get a status line like `<Idle|MPos:0.000,0.000,0.000|FS:0,0>`
   (or `<Alarm|...>` if homing is required, which is normal).
3. Paste the lines from your backup file, **one line at a time** (or use the terminal's
   "send file" with a short delay), waiting for `ok` after each.
4. Send `$$` again and compare with the backup.
5. Restore the work offset if needed, e.g. `G10 L2 P1 X.. Y.. Z..`.

## 6. Flash the TFT43

1. Format the microSD card as FAT32.
2. Copy `BIGTREE_TFT43_V3.0.grbl-<version>.bin` to the **root** of the card. Keep the
   name: the TFT's bootloader only takes files whose name starts with
   `BIGTREE_TFT43_V3.0` and ends in `.bin`. Do not put the BTT `TFT43` folder or
   `config.ini` on the card; this firmware does not use them.
3. With the TFT43 unpowered, insert the card into the TFT43's microSD slot.
4. Power the TFT43 (any 5 V source works for this step, including the DLC32 once wired, see
   step 7). The bootloader shows a progress bar for a few seconds, then starts the new
   firmware.
5. On the PC, check the card: the file is now called `...bin.CUR`. That means it was
   flashed. If it still ends in `.bin`, it was not taken (see Troubleshooting).
6. Remove the card (it is not needed for running).

**First start, touch calibration:** a splash screen shows "TFT43 Grbl". If the screen was
calibrated under the stock BTT firmware, that calibration is reused and it goes straight
to the DRO screen. Otherwise it asks you to tap three crosses and then one more to check.
To recalibrate at any time: hold a finger on the screen while the splash is showing, or use
**Setup → Calibrate touch**.

## 7. Wire the TFT43 to the DLC32

With **everything powered off**:

**Do not use a ribbon cable from EXP1 to the TFT43's EXP1.** The TFT43's EXP1/EXP2 headers
are for BTT's Marlin "12864 mode" (LCD and encoder signals), not serial, and this firmware
does not use them. A straight ribbon would tie the DLC32's IO5/IO25/IO26/IO27/IO33 and
beeper pins to unrelated TFT pins, and nothing would talk. Make up a 4-wire cable from
DLC32 EXP1 to the TFT43's 5-pin **RS232** header instead, as below.

DLC32 **EXP1** is the upper of the two 2×5 headers at the board's edge, nearer the ESP32
module (where the TS24's EXP1 cable went). The lower one, next to the RESET button, is EXP2.
MKS's pin drawing (`MKS-DLC32-main/hardware/MKS DLC32 V2.1_003/... PIN.pdf`) shows EXP1 like
this, viewed from above with the board's edge on the right and EXP2 below:

```
                 board centre  <-------------------------->  board edge
 ESP32 side row:    GND     IO25    IO26    IO5     BEEPER
 EXP2 side row :    5V      IO33    NC      IO27    NC
```

So **GND and 5V are the pair at the end towards the board centre**, IO25 sits next to GND,
and IO26 next to IO25. (In standard EXP1 numbering those are pins 9, 10, 7 and 5; MKS doesn't
print numbers on the board, so go by position and the meter, not the numbers.)

| DLC32 EXP1 | Signal | → | TFT43 "RS232" header |
|---|---|---|---|
| 5V (centre end, EXP2-side row) | 5V | → | + |
| GND (centre end, ESP32-side row) | GND | → | − |
| IO25 (next to GND) | DLC32 TX | → | **RX2** |
| IO26 (next to IO25) | DLC32 RX | → | **TX2** |

**Using a 10-wire IDC ribbon on the EXP1 end:** the colours only tell you the wire order,
not which pin each wire lands on. That depends on which way round the plug sits. Pick the
two wires at the board-centre end of the plug (in a rainbow ribbon plugged with brown at that
end, brown and red). One is GND, the other 5V: measure to find which. Then:

| Wire on GND is... | IO25 (DLC32 TX) is | IO26 (DLC32 RX) is |
|---|---|---|
| the outermost wire (e.g. brown) | the 3rd wire (orange) | the 5th wire (green) |
| the 2nd wire (e.g. red) | the 4th wire (yellow) | the 6th wire (blue) |

The other six wires stay unconnected and should be insulated.

The TFT43 V3.0's **RS232** header is the 5-pin white connector at the top right, next to
EXP1. Its silkscreen reads, left to right with the header at the top: **RST, TX2, RX2, −
(GND), +** (5V). TX2/RX2 is the USART2 this firmware uses. Do not use the separate TX3/RX3
or TX4/RX4 connectors on the right-hand edge.

| TFT43 RS232 pin (left to right) | Connect to |
|---|---|
| RST | nothing |
| TX2 | DLC32 EXP1 IO26 (DLC32 RX) |
| RX2 | DLC32 EXP1 IO25 (DLC32 TX) |
| − (GND) | DLC32 EXP1 GND |
| + (5V) | DLC32 EXP1 5V |

The RS232 header is a 2.5 mm JST-XH socket. The 5-pin XH lead that came with the TFT43 fits
it; cut or re-pin the other end with female Dupont crimps for the DLC32's EXP1 pins. Single
Dupont jumpers also push onto the XH pins if you have no lead.

Leave the TFT43's RST pin unconnected. Both sides use 3.3 V logic, so no level shifter is
needed.

**Check before connecting the screen:** power the DLC32 (TS24 unplugged), with your cable
plugged into EXP1 and the screen end loose. Put the meter's black probe on a known GND (for
example the − of the 12/24 V input) and touch the red probe to each of the two board-centre
wires: the one reading about 5 V is 5V, the one reading 0 V is GND. Power off and confirm the
GND wire beeps to GND on continuity. Only then fit them into + and − of the TFT43's RS232
plug. Getting 5V and GND wrong is the one mistake here that can damage a board; swapping
TX and RX cannot.

## 8. Verify it works

Power the machine. Within about 2 seconds of the DLC32 booting:

1. **Link:** the dot at the top right of the TFT43 turns green and the state shows `Idle`
   (or `Alarm`). If it says "No link", see Troubleshooting.
2. **DRO:** the X/Y/Z numbers match the machine position (`?` on USB shows the same).
3. **Jog:** select step 1 and feed 1000, tap X+ once; X moves 1.000 mm and the readout
   follows. Try Y and Z.
4. **Home:** on Jog, tap Home All (or X / Y / Z for one axis). The machine homes.
5. **Zero:** DRO → zero X, Y and Z at a known point; the readout shows 0.000.
6. **Code tab:** the log shows the commands you just sent (`> $J=...`, `> $H`) and the replies.
7. **Files:** the list shows the G-code files on the **DLC32's** SD card (not the TFT's card).
8. **Test job:** raise Z well above the work (or remove the tool), pick a small file, tap
   Run → OK. The DRO tab shows the file name, progress bar and live X/Y/Z, and the Code tab
   shows the current line. Try Pause, Resume, then Stop (asks first; it holds, then resets
   once the machine has stopped).
9. **Tool tab:** spindle 25 % starts the spindle, Off stops it.

LaserGRBL / UGS over USB keep working at the same time, and so does the DLC32 web UI.

## 9. Troubleshooting

| Symptom | Likely cause and fix |
|---|---|
| File on the card still ends in `.bin`, old firmware starts | Card not FAT32, file not in the root, or name changed. Reformat, copy again, keep the exact name. |
| White or blank TFT43 after flashing | GD32 chip, or a damaged file. Put the stock BTT firmware on the card and power on (section 10). |
| "No link" on the TFT43 | TX/RX swapped (swap the IO25 and IO26 wires), the DLC32 is still on the stock or `ts24` firmware, or GND not connected. On USB, `?` must answer for the screen to work too. |
| Touch is offset | Hold the screen during the splash and recalibrate. |
| Files list empty | No SD card in the DLC32, or it has no files in the root; tap Refresh. |
| Code tab never shows "Ln ..." during a job | The DLC32 is not running the `mks_dlc32_v2_1_tft43` build. |
| Machine settings look wrong after flashing | Restore the `$$` backup (section 4/5). |

## 10. Rolling back

**DLC32 back to stock (TS24):**

1. Back up `$$` as in section 4.
2. Flash `MKS-DLC32-main/firmware/TS24/CNC/Normal/Board_V2.0/V2.1.0_H24_20220511_01_N_CNC.bin`
   at 0x0 with MKS Laser Tool or esptool, exactly as in section 5.
3. Restore the `$$` lines, power off, reconnect the TS24's EXP1 and EXP2 cables.

**TFT43 back to stock BTT (Marlin) firmware:**

1. Copy `BIGTREE_TFT43_V3.0.28.x.bin`, the `TFT43` folder and `config.ini` from the BTT
   release ("Copy to SD Card root directory to update") to the root of a FAT32 card.
2. Insert it and power the TFT43. It flashes the firmware and reloads its fonts and icons.
3. Our firmware stores the touch calibration where BTT keeps its settings, so the BTT
   firmware may start with default settings and ask for a touch calibration once.

The bootloaders of both boards are never changed, so both roll-backs always work.
