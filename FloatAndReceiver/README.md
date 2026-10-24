# Float and Receiver

Firmware for the energy-harvesting "floats" and the USB-powered receiver that together make up the Brackish Networking installation. Each float is a small floating node that wakes when it has enough juice, sends one tiny LoRa packet to the receiver, draws its own slice of e-ink, and sleeps again. The receiver listens, dedups, and composes the incoming packets into a vertical feed on its own e-ink.

Part of the [Brackish Networking](https://bit.ly/fiberworkshop) workshop series.

## Hardware

- **Float and receiver boards:** Heltec Vision Master E290 (ESP32-S3 + SX1262 LoRa + 2.9" e-ink)
- **Float power:** BQ25570 energy harvester → supercap bank (0.47 F / 5.5 V typical) → P-FET load switch on `V_OK`
- **Float harvesting source:** salt water battery or wave/vibration piezo
- **Receiver power:** USB (always-on)

## Sketches

Each folder is an Arduino sketch. Open the `.ino`, select board **Heltec Vision Master E290**, upload.

| folder | what it does |
|---|---|
| `float_basic/` | Simplest float. Sends and shows **one fixed string** you edit at the top. |
| `float_generativepoetry/` | Each wake builds a **random short sentence** from three word lists (subject + verb + ending) and sends that. The receiver word-wraps it. |
| `float_tile/` | Each wake sends a small **1-bit drawing** (64×22 blocks, 128×44 px on screen). Make the drawing in `tile_drawer.html` in the same folder — open in a browser, draw, click copy, paste the result over the `TILE[]` block in the sketch. |
| `float_workshop/` | Workshop variant — in-progress / scratch. |
| `receiver/` | USB-powered receiver. Decodes incoming packets, cycles through 6 bands on its e-ink to form a vertical feed. |

### Helper sketches

| folder | what it does |
|---|---|
| `speaker_test/` | Plays a short melody through a small speaker wired directly to a GPIO. No amp, no DAC — PWM via `tone()`. |
| `wav_player/` | Plays a WAV file off the ESP32's flash through a GPIO. PWM-as-DAC. Upload the WAV via LittleFS. |

### Tools

| path | what it does |
|---|---|
| `tools/tile_drawer.html` | Open in a browser; a 64×22 grid you can draw on. Click copy to get a C byte array ready to paste into `float_tile`. Also copied into `float_tile/` for convenience. |

## Quick start

1. Install the Arduino IDE 2.x and the Heltec ESP32 board package.
2. Install libraries: **RadioLib**, **heltec-eink-modules**, **Preferences** (bundled with ESP32 core).
3. Flash `receiver` to the USB-powered receiver board.
4. Open your choice of float sketch, edit the message / word lists / drawing at the top, flash to a float board.
5. Plug the float into its harvester / supercap circuit; the receiver's screen will update when the float's energy bank first crosses `V_OK+` and it transmits.

The radio parameters (freq, bandwidth, spreading factor, sync word) must match between float and receiver — the headers of the sketches contain them. EU boards use 868 MHz; change to 915 MHz for US hardware.

## Packet format

7-byte header plus payload, all in one LoRa packet:

```
magic(1)  nodeId(2)  seq(4)  type(1)  payload(...)
0xA5                                   type 0 = tile (176 bytes)
                                       type 1 = text (variable)
```

The receiver dedupes on `(nodeId, seq)` so a float can resend the same packet multiple times without the receiver refreshing its screen more than once.

## Workshop

Full build guide, bill of materials, and the context around the installation live at [bit.ly/fiberworkshop](https://bit.ly/fiberworkshop).

## License and attribution

Brackish Networking came out of the Tidal Computing project, funded by Creative Industries Fund NL under their *Grounding the Cloud* grant.
