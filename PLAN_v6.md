# Visitor Counter v6 — Arcade Buttons, LEDs, Buzzer

## Context

Two identical Arduino Nano devices (one at entry, one at exit) count visitors via button presses and exchange their counts over RS485 to compute the number of people currently present. The hardware was rebuilt: the two plain buttons are replaced by illuminated arcade buttons (green/red with built-in LEDs), a passive buzzer was added, and there is now only **one** TM1637 4-digit display (Grove) instead of two. The sketch `sketch_v5_nano_version.ino` must be adapted to the new wiring.

Decisions confirmed:
- Display shows the **total only** (people currently present).
- Button LEDs: **flash ~200 ms on press** as feedback; additionally the **red LED blinks when the RS485 link is lost**.
- Buzzer: **silent in normal use, only sounds when the link is lost** (periodic short warning chirp).
- Green button = **+1** (person passes), red button = **−1** (correction). Same firmware on both devices, as before.

## New file

`sketch_v6_arcade_buttons/sketch_v6_arcade_buttons.ino` (folder name already matches the sketch name, so the Arduino IDE will open it directly). The old v5 sketch stays untouched in its subfolder.

## Pin mapping (from `wiring_counter_with_arcade_buttons_bb.png`)

Keep the v5 structure: all pins as `#define`s at the top so wiring changes are one-line edits.

```cpp
#define BUTTON_DEBOUNCE_INTERVAL 20
#define BUTTON_INCREASE 2   // green arcade button switch (to GND, INPUT_PULLUP)
#define LED_INCREASE    3   // green button LED (through resistor, active HIGH)
#define LED_DECREASE    4   // red button LED (through resistor, active HIGH)
#define BUTTON_DECREASE 5   // red arcade button switch (to GND, INPUT_PULLUP)
#define BUZZER          6   // passive buzzer -> tone()
#define RS485_DI        8
#define RS485_DE        9   // DE and RE tied together on D9 in the new wiring
#define RS485_RE        9   //   (kept as two defines like v5; edit if wired separately)
#define RS485_RO        10
#define DISPLAY_CLK     A5  // Grove TM1637
#define DISPLAY_DIO     A4
```

Timing/behavior constants also as defines: `LED_FLASH_MS 200`, `SYNC_SEND_INTERVAL_MS 1000`, `LINK_TIMEOUT_MS 5000`, `LINK_BLINK_MS 500`, `ALARM_INTERVAL_MS 5000`, `ALARM_TONE_HZ` / duration.

## Libraries (all already in `/libraries`)

- `AcksenButton` — both buttons, `ACKSEN_BUTTON_MODE_NORMAL`, `INPUT_PULLUP` (unchanged from v5).
- `TM1637` (avishorp `TM1637Display.h`) — single display object now.
- `SoftwareSerial` (built-in) — RS485 on RO=10/DI=8.
- Buzzer uses built-in `tone()` (Timer2, coexists with SoftwareSerial), no library.

## Program logic

Keep v5's overall shape: defines → objects → globals → `setup()` → `loop()` → helpers.

- **Counting** (as v5): `local++` on green press, `local--` on red press; `total = abs(local - remote)`; `refresh()` writes `total` to the single display.
- **Press feedback**: on a counted press, switch that button's LED on and record `millis()`; a non-blocking check in `loop()` turns it off after `LED_FLASH_MS`. No `delay()` anywhere.
- **RS485 sync** (improved but wire-compatible with v5's `"<int>|"` format):
  - Replace the v5 busy-loop counter (`synci > 10000`) with a `millis()`-based schedule: send `local` every `SYNC_SEND_INTERVAL_MS`, and additionally send immediately on a local change so the other side updates fast.
  - Send routine = v5's: DE/RE HIGH → print `local` + `"|"` → flush → DE/RE LOW. (Add `rs485.flush()` before releasing the transceiver — v5 relied on SoftwareSerial's blocking write, keep it explicit.)
  - On receive: `parseInt()` + consume the `"|"`, update `remote`, record `lastRemoteMs`, `refresh()`.
- **Link supervision**: link is "up" while `millis() - lastRemoteMs < LINK_TIMEOUT_MS` (also treat startup as "down" until first message).
  - Link down → red LED blinks at `LINK_BLINK_MS` (overrides/coexists with press flash; press flash still counted) and the buzzer plays a short two-chirp warning via `tone()` every `ALARM_INTERVAL_MS`.
  - Link restored → red LED and buzzer return to normal.
- **Serial debug** at 9600 as in v5 (print sent/received values).

## Verification

No `arduino-cli` is installed on this machine. Options:
1. Install `arduino-cli` to the user dir and run `arduino-cli compile --fqbn arduino:avr:nano` against the sketch (with `--libraries` pointing at the workspace `/libraries` folder).
2. Otherwise: compile/upload via Arduino IDE; the pin table above doubles as the wiring checklist.

Functional test on hardware: press green/red → count changes on display + LED flash; disconnect RS485 → after 5 s red LED blinks and buzzer chirps; reconnect → totals re-sync within 1 s and alarm stops.
