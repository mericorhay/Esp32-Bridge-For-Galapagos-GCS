# Galapagos Bridge

A zero-config WiFi bridge that turns your ESP32 into the ground link for
your telemetry radio, feeding [Galapagos GCS](https://github.com/mericorhay/Galapagos-GCS) over UDP.

```
[Flight controller] --UART-- [SiK/LoRa air] ~~~~~ [SiK/LoRa ground] --UART-- [ESP32] ~~WiFi~~ [Galapagos app]
```

The bridge relays **raw bytes**. It does not parse MAVLink — that stays in
Galapagos, where the message set changes faster than field firmware does.
The ESP32's only job is to move bytes from its UART to a UDP socket and
back, without losing any, and to tell you (with one LED) that it's still
alive.

## Why this exists

Apple does not let iOS apps open USB serial devices (the hardware would
need an MFi chip; telemetry radios don't have one). So an iOS GCS needs a
WiFi hop. The ESP32 is the $4 version of that hop. It is also the best
version: no cable to snap mid-flight, phone stays in your hand, and the
power bank runs both the radio and the bridge.

## What you need

| Part | Notes |
|---|---|
| ESP32, ESP32-S3, or ESP32-C3 board | DevKitC pinout assumed |
| Telemetry radio **ground** module | SiK (57600) or LoRa (9600) |
| USB power bank | ≥ 1 A; both radios and the bridge draw from it |

## Wiring

Four wires, no soldering (jumper wires are fine):

| Radio pin | ESP32 / ESP32-S3 | ESP32-C3 |
|---|---|---|
| GND | GND | GND |
| VCC | 5V / VIN | 5V / VIN |
| TX  | GPIO16 (UART2 RX) | GPIO7 (UART1 RX) |
| RX  | GPIO17 (UART2 TX) | GPIO6 (UART1 TX) |

> **5V radios.** Modern radios (3DR SiK V2, Holybro SiK V3, most LoRa
> modules) run 3.3 V logic and connect directly. If your radio's pins are
> labeled 5 V, put a logic-level converter (or a 1.8 kΩ / 3.3 kΩ divider)
> between the radio TX and ESP32 RX — a 5 V signal on a 3.3 V GPIO will
> eventually kill the pin.

## Flash it

**Easiest — browser:** serve `web/` (`cd web && npx serve .`) and open it,
or use the hosted page. Plug in the ESP32, click the button, done. No
Arduino IDE, no drivers, no toolchain.

**Or ESP-IDF:**
```sh
source $IDF_PATH/export.sh
idf.py flash monitor
```

After boot the bridge advertises WiFi **`Galapagos-Bridge`**
(password `galapagos`) on 192.168.4.1. Join it from your phone, open
Galapagos, leave host `0.0.0.0` / port `14550`, tap Connect.

## Configuration

Build-time defaults live in `main/bridge/config.hpp`; the values a pilot
actually changes are read from NVS and override them, so reflashing isn't
needed:

| Key | Default | Meaning |
|---|---|---|
| `ssid` | `Galapagos-Bridge` | AP name |
| `pass` | `galapagos` | AP password |
| `channel` | 1 | AP channel |
| `baud` | 57600 | radio baud (LoRa: set to 9600) |

Write them once with `idf.py` (NVS partition already built in):
```sh
idf.py --port /dev/cu.usbserial-0001 nvs-flash --key ssid --value MyBridge
```

Pins and LED are compile-time only (`kUartTxPin`/`kUartRxPin`/`kLedPin`).

## Architecture

```
main/bridge/
├── spsc_ring.hpp      SPSC lock-free byte ring (two of these)
├── state_machine.hpp  compile-time transition table, duplicate edges = build error
├── config.hpp/.cpp    constexpr defaults + NVS overrides
├── uart_radio.hpp/.cpp   ESP-IDF UART2 driver wrapper
├── wifi_ap.hpp/.cpp      SoftAP bring-up
├── udp_relay.hpp/.cpp    single UDP socket, broadcast out / command in
├── app_state.hpp/.cpp    shared rings + the four relay tasks
├── link_watch.cpp        radio-silence watchdog (drives the state machine)
└── state_machine.cpp     transition logging
```

The data path is two lock-free hops, both single-producer/single-consumer:

```
UART RX ──▶ rx_ring ──▶ UDP broadcast ──▶ Galapagos
UDP RX ──▶ tx_ring ──▶ UART TX ──▶ radio
```

- **`spsc_ring.hpp`** — no mutex, no CAS: one writer and one reader each
  touching a separate cache line, publish/consume via acquire/release.
  When the link saturates the bridge drops-and-counts instead of blocking
  the UART (the ISR must never stall). `dropped()` tells you it happened.
- **`state_machine.hpp`** — the link lifecycle is a `constexpr` table.
  Transitions you never declared are impossible at runtime; two handlers
  claiming the same edge fail the build.
- **Watchdog** — the bridge can't parse MAVLink, but it *can* count bytes.
  5 s without any UART traffic flips the state machine to `link-silent`
  and the LED starts blinking — mirroring the heartbeat watchdog Galapagos
  runs on its side.
- **The LED** — solid = link up, slow blink = radio silent, fast blink =
  WiFi connecting, off = error. The pilot reads the bridge without the app.

## Status LED

| Pattern | Meaning |
|---|---|
| solid | link up, relaying |
| slow blink (500 ms) | radio silent > 5 s |
| fast blink (200 ms) | WiFi still connecting |
| off | fatal error |

## License

See [LICENSE](LICENSE).
