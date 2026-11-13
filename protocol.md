# Spa Power SP800 / SP1200 touchpad bus: observed protocol

Everything below was worked out by capturing the bus with a logic analyser and an ESP32. It is not from a
manufacturer document, so treat it as observations, not a specification. Items are marked:

- **confirmed**: seen repeatedly and checked against a real reading or a real effect
- **likely**: consistent with every capture, but one or two samples
- **guess**: a hypothesis to test

Captures were taken on an SP800 main unit with an SP800/SP1200 type touchpad, mostly out of demo mode.

## 1. Physical layer

| item | value |
|---|---|
| Controller ports | two touchpad jacks (RJ11), labelled "TOUCH PADS X2" inside the box |
| Signalling | RS485, half duplex, 19200 baud, 8N1, TTL levels at the transceiver |
| Both jacks | on the same pair (a tap on jack 2 sees the keypad on jack 1) |
| Idle | RO high (A above B) |

RJ11 pinout (from the owner's notes):

| pin | wire | signal |
|---|---|---|
| 1 | yellow | RS485 B |
| 2 | green | RS485 A |
| 3 | red | +12 V |
| 4 | black | GND |

- The jack supplies 12 V, so an ESP32 can be powered from it through a regulator.
- A MAX485 is a 5 V part. Its RO output into a 3.3 V ESP32 pin is not safe without level shifting. Use a 3.3 V
  transceiver (MAX3485, SN65HVD72 or similar).
- The controller's own transceiver is an SP483ECN (5 V).
- DIP switch A (touchpad count: ON = one touchpad, OFF = two) is documented in the Davey service manual. What it
  changes on the bus is **not known**.

## 2. Frame format (confirmed)

```
02 | type | payload ... | 03 | check
```

- `check` = XOR of the type byte and all payload bytes, taken **after** unescaping.
- `10` (DLE) escapes the next byte: a literal `02`, `03` or `10` inside a frame is sent as `10 02`, `10 03`,
  `10 10`. The check byte has never needed escaping.
- Example, select key down: `02 80 10 02 00 01 03 83` (payload `02 00 01`, check = `80^02^00^01` = `83`).

## 3. Bus timing (confirmed)

- The controller sends frames back to back, about 4.2 ms apart. Status frames (type `30`) repeat every ~12.5 ms.
- The touchpad answers only in a reply slot: its frame starts 0 to 2 ms after a controller frame ends (median
  1.5 ms). The controller then waits for it and resumes about 1.7 ms after it ends.
- A key press is two frames: key-down, then a release frame (`00 00 00`) 140 to 180 ms later. Both are sent in
  reply slots.
- After a key-down, the controller's display burst starts about 18 ms later, and the number the key changed
  (type `25`) is in it about 46 ms after the key.
- Display bursts are also sent about once a second without any key.

## 4. Controller to touchpad frames

| type | payload | meaning |
|---|---|---|
| `30` | 11 bytes | status: outputs and light level (section 6), sent continuously |
| `31` | 3 bytes | likely the light colour used for the touchpad backlight (section 7) |
| `22` | 15 ASCII | display line 1 |
| `21` | 15 ASCII | display line 2 |
| `25` | `int hund 01 22` | the big digits: temperature, degrees C = `int + hund/100` (confirmed) |
| `20` | 4 digit bytes + `00` | the big digits on the main menu: `81` = dash, `80` = blank |
| `28` | 1 byte | flags: `c0` normal, `e0` in the periodic edit-mode burst |
| `23` | `0f 00 00 00 00 00 09 09` | starts the periodic burst while in setpoint edit mode |
| `27` | `pp 7e 42 80` | replaces line 1 on the LIGHT ADJUST screen; `pp` = brightness percent |
| `32` | 8 bytes | seen twice at light mode changes (`00 aa 55 55 55 55 00 fa`, `00 ff 00 ff 00 00 00 25`); unknown |
| `38` | empty, or `0b a0` | only in demo mode: follows key frames; not seen out of demo mode |

### Display bursts

A burst is four slots in order: line 1 (`22`, or `27` / `23` variants), line 2 (`21`), the digits (`25` or `20`),
flags (`28`). Text is plain ASCII; byte `1e` is an up-arrow glyph and `1f` a down-arrow glyph.

| screen | line 1 | line 2 | digits |
|---|---|---|---|
| normal | `Set temp  NN.NN` (or `PWR OFF TO EXIT` in demo mode) | clock, e.g. ` Mon,   1:27pm ` | `25`: current temperature |
| main menu | `(^)  (ESC)  (v)` | `   MAIN MENU   ` | `20`: `81 81 81 81 00` (`----`) |
| SET TEMP item | `(^)  (SET)  (v)` | `  1. SET TEMP   ` | `25`: setpoint |
| setpoint edit | `(^)  (SET)  (v)` | blank | `25`: setpoint |
| light adjust | `27` frame | `LIGHT ADJUST:` | `25`: current temperature |
| pump / aux | `PUMP AUTO`, `PUMP ON`, `PUMP OFF`, `AUX ON`, `AUX OFF` | blank | `25` |

- The setpoint outside the menu is only sent as text (`Set temp  NN.NN`).
- Boot: digits `80 80 80 80` (blank), then line 1 `SP800` with `----`, then the normal screen after about 4 s.

## 5. Touchpad to controller frames

### Key frames: type `80`, payload = byte 0, byte 1, byte 2

Byte 2 is `01` on every key-down (a "key active" flag, or possibly a touchpad number: **guess**). It is `00` on the
release frame.

| key | payload | wire bytes (key-down) | what it does |
|---|---|---|---|
| down arrow | `01 00 01` | `02 80 01 00 01 03 80` | menus; setpoint -0.25 C in edit mode |
| select (the manual calls it SCROLL) | `02 00 01` | `02 80 10 02 00 01 03 83` | opens / closes the main menu, enters edit, leaves edit |
| up arrow | `04 00 01` | `02 80 04 00 01 03 85` | menus; setpoint +0.25 C in edit mode |
| pump 1 | `10 00 01` | `02 80 10 10 00 01 03 91` | OFF -> AUTO -> ON -> OFF |
| second pump | `20 00 01` | `02 80 20 00 01 03 a1` | `PUMP ON` / `PUMP OFF` |
| light mode | `40 00 01` | `02 80 40 00 01 03 c1` | cycles the light effect modes |
| light level | `00 01 01` | `02 80 00 01 01 03 80` | wakes the light screen, then cycles brightness |
| aux | `00 04 01` | `02 80 00 04 01 03 85` | `AUX ON` / `AUX OFF` |
| release | `00 00 00` | `02 80 00 00 00 03 80` | after every key |

Not identified: `08 00 01`, `80 00 01`, `00 02 01` (the blower and pump 2 are candidates; the Davey manual says an
SP800 has no pump 2 outputs, so the working second pump is probably Pump 1B).

### Settings transfer: types `c0`, `c1`, `50`, `51` (confirmed for reads)

Seen when the touchpad's COPY SETUP mode did a read and a verify. The controller kept running normally.

| frame | direction | payload |
|---|---|---|
| `c1` | touchpad to controller | `bank offset 00 length`: read request |
| `51` | controller to touchpad | the four request bytes, then the data |
| `c0` | touchpad to controller | `01`: info request |
| `50` | controller to touchpad | `01 a5 00`: bank 1 is 0xa5 = 165 bytes |

Sequence: `c1 00 00 00 09` (header), `c0 01`, then `c1 01 NN 00 10` for NN = `00`, `10`, ... `90`, then
`c1 01 a0 00 05`. Requests are about 34 ms apart. Wire bytes: `02 c1 00 00 00 09 03 c8`, `02 c0 01 03 c1`,
`02 c1 01 00 00 10 10 03 d0` (the `10 10` is the escaped length byte).

Header (9 bytes): `08 00 90 13 00 5f 00 00 01` (meaning unknown).

Settings block (165 bytes), read at setpoint 24.50:

```
00: 01 01 64 32 32 32 32 32 05 62 1e 00 01 00 01 0a
10: 00 00 01 32 01 00 00 00 00 00 00 00 00 00 00 00
40: 00 00 00 00 00 00 00 00 00 00 00 01 76 02 5a 00
70: 00 00 00 00 00 00 00 00 01 00 06 06 06 01 06 01
80: 01 01 01 01 01 01 01 01 01 00 50 09 05 00 00 02
90: 01 01 02 09 0b 01 02 05 0b 01 02 01 02 09 05 0b
a0: 0c 02 05 0a c7        (lines 20, 30, 50 and 60 are all zeros)
```

- **Offset `0x09` = setpoint in 0.25 C steps** (confirmed: `62` = 24.50, `80` = 32.00).
- **Last byte (`0xa4`) = `0xAA` XOR the XOR of the 164 bytes before it** (confirmed on two reads: `c7`, `25`).
- Between the 24.50 and 32.00 reads, only those two bytes differed.
- Write commands have not been captured.

## 6. Status frame (type `30`, 11 bytes)

| byte | bit / value | meaning |
|---|---|---|
| 1 | `08` | heater |
| 2 | `01` | second pump running |
| 2 | `02` | pump 1 running |
| 2 | `40` | ozone |
| 2 | `80` | aux |
| 3 | `10` | pump 1 running (with byte 2 `02`) |
| 3 | `20` | second pump running (with byte 2 `01`) |
| 3 | `04` | pump 1 in AUTO |
| 4 | 0 to 255 | light brightness (`ff` 100%, `aa` 67%, `55` 33%) |
| 5, 6 | `ff ff` | `48 20` after a light effect mode change (unknown) |
| 9, 10 | `51 00` pump 1, `a2 10` second pump, `04 22` aux | per-output pattern (unknown meaning) |

- Pump, heater and ozone bits were mapped from single live runs. In demo mode the bits are simulated.
- The heater comes on about 6 s after the pump, ozone a few seconds later. The Davey manual says ozone follows the
  circulation pump and is switched off for 30 minutes after a button press if DIP switch I is OFF.
- The heater bit stays off while the pump is off, even with the setpoint above the water temperature.

## 7. Light colour (type `31`) (likely)

`55 55 55` normally; `ff 00 00`, `00 00 ff`, `80 00 80`, `00 55 aa` seen while a colour-changing light mode ran. The
Davey manual says the touchpad backlight can follow the pool light colour, and that the controller produces the effects
even with no light fitted, so this is probably the backlight colour. Byte order is unverified.

## 8. Menu navigation (confirmed out of demo mode)

1. select: main menu (`----` on the digits)
2. up: `1. SET TEMP`
3. select: edit mode
4. up / down: setpoint +/- 0.25 C per press, applied immediately
5. select: back to the normal screen. There is no confirm step.

## 9. What the keypad never sends (confirmed)

Across about 15,500 frames in 16 captures, the only frames the touchpad sent in the reply slot were key frames and,
in copy mode, the settings requests. A controller-style temperature frame (`25`) injected from the ESP did not change
the setpoint: type `25` is display data for the touchpad, not a command.

## 10. Not known

- Write commands for the settings transfer, and whether the controller accepts transfers outside copy mode.
- What DIP switch A changes on the bus, and whether the touchpads have separate slots or addresses.
- Blower and light protocol: no light or variable speed blower is fitted to the test unit.
- Meaning of types `32` and `38`, status bytes 5, 6, 9, 10, and most of the settings block.
- Keys `08 00 01`, `80 00 01`, `00 02 01`.

## 11. From the Davey SP800 & SP1200 Installation & Service Manual (TEC224)

- The touchpad has a four-digit display, two text lines, a beeper, and its own firmware version (shown with the
  controller version on a diagnostic screen).
- COPY SETUP mode: power the controller up while holding Light Level. The touchpad can read the controller's settings
  (including set temp, sleep and filtration times, timers, options) into its own memory, write them to another controller,
  or verify them.
- Diagnostic screens (hold SCROLL about 5 s on the main menu): software versions, DIP switches, in-heater and in-pool
  temperature readings, logged errors.
- Demo mode: entered with the up button from the PRIME FAILED error; power-cycle to leave it. Pumps do not run in it.
- Key lock: Down, Scroll, Down, Up, Scroll.
- Latching errors 3 to 8 are reset by pressing the top four buttons together.
- Default setpoint 37 C; the manual recommends not exceeding 40 C. Over-temperature error at 45 C.
- Main-board microcontroller is an AVR, reprogrammable through a 6-pin ISP header; the software is proprietary.
- An SP800 has no pump 2 outputs (SP1200 only); the touchpads are shared between both models.
