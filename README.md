# Spa Power SP800 ESPHome COmponent

An ESPHome component for the keypad bus of a Spa Power SP800 spa pack (the keypad is believed to be an SP1200 type).
It listens to the bus and publishes the spa's state to Home Assistant, and it can press the keypad's keys for you,
including setting the target temperature.

Everything here was worked out by capturing the bus with a logic analyser. None of it comes from a manufacturer
document, so treat the protocol notes as observations, not a specification.

## What you get

- **Read-only sensors:** current temperature, set temperature, light level, heater, ozone, pump 1 (off / auto / on),
  pump 2, aux, the two display lines, the big four-digit display, and the last key pressed on the real keypad.
- **Controls (off by default):** pump 1 mode, pump 2 and aux switches, up / down / select buttons, and a **target
  temperature** `number` that drives the keypad menu for you and checks the screen after every key.
- **Diagnostics:** the raw status frame, and a log of every change it sees (status bits that flipped, key presses,
  other frame types) to help map more of the protocol.

## Status

| area | state |
|---|---|
| Frame parser, checksum, escaping | checked on every capture (about 15,000 frames, none rejected) |
| Current temperature, set temperature, light level | checked against real readings |
| Pump 1, pump 2, aux, heater, ozone | mapped from live runs out of demo mode, one run each |
| Sending key frames | one key confirmed working on the live spa |
| Buttons, switches, select | written and simulated; not yet confirmed on the live spa |
| Target temperature `number` | simulated only (a model of the keypad with the captured timing); not yet run on the spa |
| Blower and the other keys | not mapped |

Tested with ESPHome 2026.10.2 (config validation, code generation and a host-side compile check). The ESP-IDF
framework is assumed because flow control uses the IDF RS485 mode; Arduino is untested.

## Hardware

- An ESP32 (developed on an ESP32-S3).
- A **3.3 V RS485 transceiver**. The spa bus is TTL-level UART carried over RS485 transceivers on a 2-wire pair,
  half duplex, **19200 baud, 8N1**. The spa's own transceiver is an SP483ECN (5 V); a 3.3 V part on the ESP side
  sees the same A/B pair.
- Wiring: transceiver RO to `rx_pin`, DI to `tx_pin`, DE and RE̅ tied together to `flow_control_pin`
  (high = transmit, low = receive), A and B to the bus, common ground. For read-only use you can hold DE/RE̅ low and
  skip `tx_pin` entirely.
- The real keypad can stay connected. The component only drives the bus while it is sending a key frame.
- Keep the logger off the UART pins (on an ESP32-S3 with native USB: `logger: hardware_uart: USB_SERIAL_JTAG`).

## Install

Copy the `components/` folder next to your YAML, or pull it from GitHub:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/Bno000/spa_power_esphome
    components: [spa_power]
```

Minimal read-only config:

```yaml
uart:
  id: spa_uart
  rx_pin: GPIO16
  baud_rate: 19200
  data_bits: 8
  parity: NONE
  stop_bits: 1
  rx_buffer_size: 1024

spa_power:
  id: spa
  uart_id: spa_uart

sensor:
  - platform: spa_power
    spa_power_id: spa
    current_temperature: { name: "Spa Current Temperature" }
    set_temperature: { name: "Spa Set Temperature" }

binary_sensor:
  - platform: spa_power
    spa_power_id: spa
    heater: { name: "Spa Heater" }
    pump_running: { name: "Spa Pump Running" }
```

`spa-power.yaml` in this repository is a complete example with every entity.

## Entities

### Sensors and text

| platform | key | what it is |
|---|---|---|
| `sensor` | `current_temperature` | type 25 frame on the normal screens |
| `sensor` | `set_temperature` | `Set temp  NN.NN` text, and the number on the SET TEMP screens |
| `sensor` | `light_level` | status byte 4 as a percentage |
| `binary_sensor` | `pump_running`, `pump2_running`, `aux`, `heater`, `ozone` | status frame bits |
| `text_sensor` | `line1`, `line2` | the two display lines |
| `text_sensor` | `big_digits` | what the big display shows: the temperature, or `----` on the main menu |
| `text_sensor` | `key` | last key frame from the real keypad (`down`, `up`, `menu_set`, `pump1`, `pump2`, `aux`, `light`, `light_mode`, `none` on release) |
| `text_sensor` | `pump_state` | pump 1: `off`, `auto` or `on` |
| `text_sensor` | `light_color` | experimental, byte order unverified |
| `text_sensor` | `status_raw` | hex of the status frame, for reverse engineering |

### Controls (need `transmit: true`)

| platform | key | what it does |
|---|---|---|
| `number` | `set_temperature` | target temperature, 0.25 C steps. `min_value` and `max_value` are required |
| `select` | `pump` | pump 1: `off` / `auto` / `on` |
| `switch` | `pump2`, `aux` | on / off |
| `button` | `up`, `down`, `select` | the keypad's navigation keys; they do what the current screen says |
| `button` | `pump`, `pump2`, `aux` | one raw press of that key |

The state a control shows is always what the controller reports, never what was commanded, and nothing is pressed
at boot.

## Controlling the spa

Sending is off unless you ask for it. In the `spa_power:` block:

```yaml
spa_power:
  id: spa
  uart_id: spa_uart
  transmit: true       # default false
  dry_run: true        # default true: controls only log the key frames they would send
  # key_hold: 150ms          # key-down to release frame
  # key_spacing: 400ms       # minimum time between key presses
  # response_timeout: 800ms  # wait for the status frame to change after a pump or aux press
  # max_reply_latency: 1500us
```

1. Start with `dry_run: true` and operate a control: the log shows the key frames it would send.
2. Set `dry_run: false` with the real keypad connected. Try one raw `button` first, then the switches and select.
3. Try the target temperature `number` last, with a one-step change.

### How it sends

A key press is two frames, like the real keypad: key-down, then the release frame (`00 00 00`) `key_hold` later.
The real keypad answers 0 to 2 ms after a status frame ends and the controller waits for it, so the component only
sends in that slot, and only if the last loop pass was fast enough (`max_reply_latency`). A late frame is skipped,
not sent on top of the controller's next one. While a key action runs, the main loop runs at high frequency.

- **Pump and aux keys** verify that the status frame changed, retry a bounded number of times, and are refused while
  a menu, edit or adjust screen is showing.
- **Navigation keys** are sent on any screen, once, with no retry. Up to 16 quick taps are queued and spaced out.
- **Target temperature** runs select (main menu), up (to `1. SET TEMP`), select (edit mode), up or down per 0.25 C,
  select (leave). After every key it reads the screen and the number the controller shows and decides the next key
  from that, so a lost press is repeated and a doubled one is corrected with an opposite press. It only starts from
  the normal screen, only for targets inside `min_value` / `max_value` and within 20 C of the current setpoint. If a
  step fails it presses select to leave the menu so the keypad ends on the normal screen, and logs what happened.
  If it gives up in edit mode, the setpoint is wherever the last press left it.

Setting the temperature is done only through the keypad's own keys. The keypad never sends a temperature value, and
the temperature frame the controller sends is display data for the keypad, not a command. Injecting one from the ESP
did not change the setpoint.

## Protocol notes

**Frame:** `02 | type | payload ... | 03 | check`. `check` is the XOR of the type and payload bytes after unescaping.
`10` escapes the next byte, so a literal `02`, `03` or `10` inside a frame is sent as `10 02`, `10 03`, `10 10`.

**Two nodes share the pair.** The controller sends frames about 4.2 ms apart. The keypad answers only with key frames
(type `80`), starting 0 to 2 ms after a controller status frame ends.

### Key frames (type 80, payload byte 0, 1, 2)

| key | payload | wire bytes (key-down) |
|---|---|---|
| down arrow | `01 00 01` | `02 80 01 00 01 03 80` |
| select / main menu | `02 00 01` | `02 80 10 02 00 01 03 83` |
| up arrow | `04 00 01` | `02 80 04 00 01 03 85` |
| pump 1 | `10 00 01` | `02 80 10 10 00 01 03 91` |
| pump 2 | `20 00 01` | `02 80 20 00 01 03 a1` |
| light mode | `40 00 01` | `02 80 40 00 01 03 c1` |
| light brightness | `00 01 01` | `02 80 00 01 01 03 80` |
| aux | `00 04 01` | `02 80 00 04 01 03 85` |
| release (after every key) | `00 00 00` | `02 80 00 00 00 03 80` |

Not identified: `08 00 01`, `80 00 01`, `00 02 01` (one of them is probably the blower). Byte 2 is `01` on every
press seen.

### Status frame (type 30, 11 bytes, sent continuously)

| byte | bit | meaning |
|---|---|---|
| 1 | `08` | heater |
| 2 | `01` / `02` | pump 2 / pump 1 running |
| 2 | `40` / `80` | ozone / aux |
| 3 | `10` / `20` | pump 1 / pump 2 running |
| 3 | `04` | pump 1 in auto |
| 4 | all | light brightness, 0 to 255 |

Pump, heater and ozone bits were mapped from single live runs. In demo mode the bits are simulated.

### Display burst (about once a second, and after every key press)

Four slots: line 1 (type `22`; type `27` on the light adjust screen; type `23` first in edit-mode refreshes),
line 2 (type `21`), the big digits (type `25`, or type `20` on the main menu), and a flags byte (type `28`).

- Type `25`: `byte0 + byte1/100` degrees C, then `01 22`. It is the current temperature on normal screens and the
  setpoint on the SET TEMP screens.
- Type `20`: one byte per digit, `81` is a dash (the main menu shows `----`).
- The setpoint outside the menu is only sent as text, on the `Set temp` screen.

Menu path out of demo mode: select (main menu), up (`1. SET TEMP`), select (edit), arrows, select (back to the main
screen). The new value applies as you press; there is no confirm.

## Mapping more states

At `logger: level: INFO` (or DEBUG) the component logs every change it sees, next to key presses:

```
key pump1
status change: b2 00->02 (+02), b3 00->14 (+14), b9 00->51 (+51)
type 28: c0 -> e0
```

Step the spa through one state at a time (heater on and off, each pump, blower, aux, error messages) and note what
changed.

## Tests

The protocol code is plain C++ with no ESPHome dependency (`components/spa_power/spa_power_parser.h`), so it builds
and runs on a PC:

```
g++ -std=c++17 -o enc tests/enc_test.cpp && ./enc tests/capture_live_pump2_aux.bin     # encoder vs real key frames
g++ -std=c++17 -o sim tests/sim.cpp && ./sim                                           # simulated spa and keypad
g++ -std=c++17 -o t tests/test.cpp && ./t tests/capture_normal_screen.bin              # decoder on a real capture
g++ -std=c++17 -o screen tests/screen_test.cpp && ./screen tests/capture_set_temp_edit.bin
```

The `.bin` files are raw bus captures from the spa the component was developed on.

## Known limits and safety

- The transmit side has been exercised on the live spa only for a single key. The set-temperature control has never
  run on a real keypad.
- If the real keypad is pressed at the same moment as a command, the two frames can collide.
- The controller's tolerance for a reply later than about 2 ms is unknown. Watch the `late_slots` figure in the
  once-a-minute log line.
- Blower and the other keys are unmapped.
- A command is a real key press. Do not run pumps or the heater dry.

## License

Not chosen yet. Add a `LICENSE` file before publishing.
