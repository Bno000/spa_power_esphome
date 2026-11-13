# spa_power: read-only ESPHome component for the Spa Power SP800/SP1200 keypad bus

Listens to the keypad bus (19200 baud, 8N1, TTL levels carried over RS485) and publishes:

| entity | source |
|---|---|
| `current_temperature` | type 25 frame on the normal screens, `byte0 + byte1/100` |
| `set_temperature` | `Set temp  NN.NN` text (type 22), and type 25 on the SET TEMP menu and edit screens |
| `light_level` | type 30 status frame, byte 4 (0..255) as a percentage |
| `line1`, `line2` | display text (type 22 and 21) |
| `key` | last key frame from the keypad (type 80), `none` on release |
| `pump_state` | EXPERIMENTAL: bytes 2-3 of the type 30 frame, from one pump only |
| `status_raw` | hex of the type 30 payload, for further reverse engineering |

It never transmits. Frame format: `02 | type | payload | 03 | check`, check = XOR of type and payload after
unescaping, `10` escapes the next byte.

## Use
1. Copy `components/` next to your YAML (see `spa-power.yaml`).
2. Wire the RS485 transceiver's RO pin to the ESP RX pin; hold DE and RE̅ low so it only listens. Use a 3.3 V part,
   a common ground, and keep the logger off the UART pins you use.
3. Set `rx_buffer_size: 1024` on the `uart:` block.

## Known limits
- Only tested by replaying captured bus data through the parser, not on a live bus or in a full firmware build.
- `pump_state` is a guess from three states of one pump. Treat the status bytes as unknown beyond byte 4.
- The setpoint outside the menu is only sent as text, on the screen that shows `Set temp`; the value is kept between screens.
- Whether `current_temperature` is the water temperature is supported by one match against a reading, in demo mode.

## Test the parser on a PC
```
g++ -std=c++17 -o t tests/test.cpp && ./t tests/capture_normal_screen.bin
```
