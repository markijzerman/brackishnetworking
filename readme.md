# Float and Receiver

Firmware for the energy-harvesting "floats" and the USB-powered receiver that together make up the Brackish Networking installation. Each float is a small floating node that wakes when it has enough juice, sends one tiny LoRa packet to the receiver, draws its own slice of e-ink, and sleeps again. The receiver listens and composes the incoming packets into a vertical feed on its own e-ink.

Part of the [Brackish Networking](https://bit.ly/fiberworkshop) workshop series.

## Hardware

- **Float and receiver boards:** Heltec Vision Master E290 (ESP32-S3 + SX1262 LoRa + 2.9" e-ink)
- **Float power:** BQ25570 energy harvester → supercap bank (0.47 F / 5.5 V typical) → P-FET load switch on `V_OK`
- **Float harvesting source:** salt water battery, or wave motion driving NEMA17 stepper motors used as generators, through rectifier diodes into the BQ25570
- **Receiver power:** USB (always-on)

## Sketches

Each folder is an Arduino sketch. Open the `.ino`, select board **Heltec Vision Master E290**, upload.

| folder | what it does |
|---|---|
| `send_basic_message/` | Simplest float. Sends **one fixed string** you edit at the top **the moment it gets power**, then every 30 s (`SEND_SECONDS`) while powered. No V_OK check, no sleep, so it works the same on USB and on a harvester like a hand-cranked stepper. Energy-saving: CPU at 80 MHz, radio asleep right after sending, and the e-ink is touched **only after a successful send** — just the top band, with a fast partial refresh. |
| `float_generativepoetry/` | Each wake builds a **random short sentence** from three word lists (subject + verb + ending) and sends that. The receiver word-wraps it. |
| `float_tile/` | Each wake sends a small **1-bit drawing** (64×22 blocks, 128×44 px on screen). Make the drawing in `tile_drawer.html` in the same folder — open in a browser, draw, click copy, paste the result over the `TILE[]` block in the sketch. |
| `float_workshop/` | Workshop variant — in-progress / scratch. |
| `MU_float/` | Exhibition float that **talks back**. Each wake it asks the receiver for messages from the other floats, keeps up to 4, sends its own, and redraws its screen: its own message under **SENT BY THIS FLOAT**, the others under **RECEIVED FROM OTHER FLOATS**, each tagged with the sender. Set `MODE` at the top: `MODE_PICTURES` (a list of drawings from `tile_drawer.html`), `MODE_POETRY` (generative poems from word lists) or `MODE_SHAPES` (generative circles and lines). |
| `receiver/` | USB-powered receiver. Decodes incoming packets and shows the last 6 as a feed on its e-ink: newest on top (inverted), older ones below, each in its own box with an **N mins ago** label, updated every minute. Keeps the last 24 messages so `MU_float`s can fetch them. Also prints each message on Serial (115200) as one line: `msg: <text>`, `tile: <hex>` or `shapes: <hex>`. |

### Helper sketches

| folder | what it does |
|---|---|
| `eink_test/` | Writes one line of text to the e-ink on USB power and keeps moving it to random spots. Quick check that the screen and library work. |
| `speaker_test/` | Plays a short melody through a small speaker wired directly to a GPIO. No amp, no DAC — PWM via `tone()`. |
| `wav_player/` | Plays a WAV file off the ESP32's flash through a GPIO. PWM-as-DAC. Upload the WAV via LittleFS. |

### Tools

| path | what it does |
|---|---|
| `tools/feed.html` | Shows what the receiver gets, big: text in big letters, drawings and circles-and-lines as pictures, each with an **x mins ago** label. Plug the receiver in over USB, close the Arduino Serial Monitor, open the file in Chrome or Edge, click connect and pick the port. |
| `tools/tile_drawer.html` | Open in a browser; a 64×22 grid you can draw on. Click copy to get a C byte array ready to paste into `float_tile`. Also copied into `float_tile/` for convenience. |

## Quick start

1. Install the Arduino IDE 2.x and the Heltec ESP32 board package.
2. Install libraries: **RadioLib**, **heltec-eink-modules**, **Preferences** (bundled with ESP32 core).
3. Flash `receiver` to the USB-powered receiver board.
4. Open your choice of float sketch, edit the message / word lists / drawing at the top, flash to a float board.
5. Plug the float into its harvester / supercap circuit; the receiver's screen will update when the float's energy bank first crosses `V_OK+` and it transmits.

The radio parameters (freq, bandwidth, spreading factor, sync word) must match between float and receiver — the headers of the sketches contain them. EU boards use 868 MHz; change to 915 MHz for US hardware.

The receiver has a `FAST_RADIO` switch. `true` (the default) is the short-range, fast setting the `MU_float`s use (868.3 MHz, 500 kHz, SF6, CR 4/5): packets are about 10× shorter on air, which the energy-starved floats need. Set it to `false` to go back to the original setting (868.0 MHz, 125 kHz, SF7, CR 4/7) that the workshop floats (`float_tile`, `float_generativepoetry`, `send_basic_message`) use. A receiver hears only one of the two settings at a time.

## Packet format

8-byte header plus payload, all in one LoRa packet:

```
magic(1)  nodeId(2)  seq(4)  type(1)  payload(...)
0xA5                                   type 0 = tile   (176 bytes)
                                       type 1 = text   (variable)
                                       type 2 = shapes (5 bytes each: op, a, b, c, d)
                                                op 0 line (a,b)-(c,d), 1 circle (a,b) r=c, 2 filled circle
```

The receiver shows every message it hears, repeats included: if a float sends the same packet twice (e.g. `TX_REPEATS` > 1), it appears twice on the receiver.

### Mailbox (MU_float ↔ receiver)

```
type 3 request  float -> receiver   payload: lastSeen(4) want(1)
type 4 relay    receiver -> float   header nodeId/seq = original sender
                                    payload: to(2) mailId(4) ageSeconds(4) kind(1) content
type 5 end      receiver -> float   payload: to(2) count(1)
```

A waking `MU_float` sends a request with the newest mail id it has seen. The receiver answers with up to 4 newer messages, newest first, never the float's own ones, then an `end` packet. Only after that does the float send its own message. The receiver holds off redrawing its e-ink until the radio has been quiet for a second, and keeps answering requests while the screen refreshes. Every float needs its own ID: the sketches take it from the last two bytes of the board's MAC address, which differ per board. The older sketches only send, and work unchanged next to `MU_float`s.

## Workshop

Full build guide, bill of materials, and the context around the installation live at [bit.ly/fiberworkshop](https://bit.ly/fiberworkshop).

## License and attribution

Brackish Networking came out of the Tidal Computing project, funded by Creative Industries Fund NL under their *Grounding the Cloud* grant.
