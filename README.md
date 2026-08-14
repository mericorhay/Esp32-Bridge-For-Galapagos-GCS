# Galapagos Bridge

Turn an ESP32 into a wireless MAVLink bridge for Galapagos GCS.

The bridge lets you use a telemetry radio with an iPhone, iPad, or any other device that cannot directly connect to the radio by USB.

You do not need to understand MAVLink, ESP32 programming, Arduino, or networking to use it.

---

# Privacy Policy

**Effective date: August 2026**

Galapagos GCS ("the app") is built by a small independent developer. This policy explains what information the app accesses and how it is used.

## Data We Collect

**None.**

Galapagos GCS does not collect, store, or transmit personal data.

The app has:

- No user accounts
- No analytics
- No advertising trackers
- No tracking
- No cloud account
- No personal-data collection

## Data Used on Your Device

The following information may be stored or processed locally on your device:

| Data | Why it is used |
|---|---|
| Vehicle telemetry | Displays GPS, battery, attitude, and other flight information |
| Connection settings | Remembers IP addresses, ports, and serial settings |
| Mission plans | Stores waypoints, geofences, and flight plans |
| Offline map data | Allows maps to work without an internet connection |
| App preferences | Saves settings such as theme and interface preferences |
| Purchase status | Allows the app to remember your Pro purchase |

This information is not uploaded to Galapagos servers and is not sold or shared by Galapagos.

## Third-Party Services

Galapagos may communicate with the following third-party services when you use related features:

| Service | Purpose | Information sent |
|---|---|---|
| CARTO | Map tiles | Map coordinates required to display the map |
| OpenFreeMap | 3D map data | Map coordinates required to display the map |
| adsb.lol / OpenSky Network | Nearby aircraft information | Your approximate location may be sent to find nearby aircraft |

These services are used only when their corresponding features are active.

Galapagos does not send your personal identity, account information, or flight logs to these services.

## In-App Purchases

Payments are processed by Apple through the App Store and StoreKit.

Galapagos does not receive or store:

- Credit card information
- Payment information
- Apple ID passwords
- Your full Apple payment details

Purchase status is used locally by the app to provide access to purchased features.

## Children's Privacy

Galapagos GCS does not knowingly collect personal information from children under 13.

## Changes to This Policy

If this privacy policy changes, the updated version will be published with a new effective date.

## Contact

For privacy questions:

**mericorhayy@gmail.com**

---

# What Is Galapagos Bridge?

Galapagos Bridge is a small ESP32 device that connects your **ground telemetry radio** to Galapagos GCS over Wi-Fi.

It is especially useful with iPhone and iPad, where USB telemetry radios cannot normally be connected directly.

The bridge simply forwards the telemetry data between your radio and Galapagos.

It does **not** need to understand or modify MAVLink.

The complete setup looks like this:

```text
DRONE

Flight Controller
       │
       │ UART
       ▼
  Air Telemetry Radio
       ))))))))))))))
       ((((((((((((((
  Ground Telemetry Radio
       │
       │ UART
       ▼
      ESP32
       │
       │ Wi-Fi
       ▼
  iPhone / iPad
       │
       ▼
  Galapagos GCS
```

The ESP32 does not replace your telemetry radio.

It acts as the wireless connection between the **ground radio** and Galapagos.

---

# What You Need

You need only four things:

1. An ESP32, ESP32-S3, or ESP32-C3 board
2. A **ground-side telemetry radio**
3. A USB power source
4. Galapagos GCS

The bridge supports common MAVLink telemetry radios such as:

* SiK radios
* Compatible LoRa telemetry radios
* Other serial MAVLink telemetry radios

Typical baud rates:

* SiK: **57600**
* LoRa: **9600**

---

# Important: Which Radio Goes to the ESP32?

This is the most important part.

You connect the **GROUND radio** to the ESP32.

Do **not** connect the ESP32 to the radio installed on the drone.

Your setup should look like this:

```text
DRONE

Flight Controller
       │
       ▼
   AIR RADIO
       )))))))))))))
       ((((((((((((
   GROUND RADIO
       │
       ▼
      ESP32
       │
       ▼
     Wi-Fi
       │
       ▼
   Galapagos
```

The air radio stays connected to the flight controller.

The ground radio connects to the ESP32.

---

# Quick Start

If you have never used an ESP32 before, follow these steps.

## Step 1 — Flash the ESP32

You do **not** need Arduino IDE.

You do **not** need to compile the firmware.

You do **not** need to use the command line.

You can install the bridge directly from your web browser.

### What is a Web Flasher?

A Web Flasher is simply a webpage that installs the Galapagos Bridge firmware directly onto your ESP32 through USB.

You do not need to know what firmware is.

Think of it like installing an operating system onto the ESP32.

### What you need

Connect the ESP32 to your computer using a USB **data** cable.

> **Important:** Some USB cables are power-only cables and cannot transfer data. If the ESP32 does not appear, try another USB cable.

### Install the firmware

1. Connect the ESP32 to your computer.
2. Open the Galapagos Bridge Web Flasher.
3. Click **Install / Flash**.
4. Your browser will ask which USB device to use.
5. Select your ESP32.
6. Select the ESP32 board type if asked.
7. Let the installer finish.
8. Disconnect the ESP32 when the installation is complete.

That's it.

You do not need Arduino IDE.

### Supported boards

The Web Flasher currently supports:

* ESP32
* ESP32-S3
* ESP32-C3

### Supported browsers

Use a desktop version of:

* Google Chrome
* Microsoft Edge

The Web Flasher uses the browser's Web Serial feature.

> **Firefox cannot flash** — it does not support Web Serial.

> **iPhone and iPad cannot flash the ESP32 directly.**
>
> Flash the ESP32 using a computer first. After flashing, you can use the bridge normally from your iPhone or iPad.

---

# Step 2 — Connect Your Ground Radio

Disconnect the ESP32 from your computer if necessary.

Connect the **ground telemetry radio** to the ESP32.

You only need four connections:

| Radio | ESP32 / ESP32-S3 | ESP32-C3 |
| ----- | ---------------- | -------- |
| GND   | GND              | GND      |
| VCC   | 5V / VIN         | 5V / VIN |
| TX    | GPIO16           | GPIO7    |
| RX    | GPIO17           | GPIO6    |

Remember:

```text
Radio TX → ESP32 RX
Radio RX → ESP32 TX
GND      → GND
VCC      → VCC
```

You do not need to solder anything if your hardware has suitable jumper connectors.

---

# Check Your Radio Voltage

Most modern telemetry radios use **3.3 V logic** even when powered from 5 V.

For example, many SiK radios can be connected directly.

However, if your radio outputs **5 V logic**, do not connect its TX signal directly to an ESP32 GPIO.

Use an appropriate logic-level converter or voltage divider.

A 5 V signal connected directly to an ESP32 GPIO can damage the ESP32.

If you are unsure about your radio's logic voltage, check its hardware documentation before connecting it.

---

# Step 3 — Power the Bridge

The easiest setup is a USB power bank.

Connect:

```text
USB Power Bank
      │
      ├── ESP32
      │
      └── Ground Radio
```

A power source capable of supplying at least **1 A** is recommended.

You do not need to keep the ESP32 connected to your computer while flying.

---

# Step 4 — Turn On the Bridge

After powering the ESP32, wait a few seconds.

The bridge creates its own Wi-Fi network:

```text
Wi-Fi name:
Galapagos-Bridge

Password:
galapagos
```

The bridge normally uses:

```text
192.168.4.1
```

---

# Step 5 — Connect Your Phone or Tablet

On your iPhone or iPad:

1. Open **Settings**.
2. Open **Wi-Fi**.
3. Find **Galapagos-Bridge**.
4. Enter the password:

```text
galapagos
```

5. Wait until your device connects.

You are now connected directly to the ESP32.

You do not need internet access for this connection.

---

# Step 6 — Open Galapagos

Open Galapagos GCS.

Choose the Wi-Fi / UDP MAVLink connection.

Use:

```text
Host: 0.0.0.0
Port: 14550
```

The bridge forwards the MAVLink traffic between your ground radio and Galapagos.

Once the flight controller's heartbeat is detected, Galapagos automatically identifies the vehicle and autopilot.

You do not need to manually select:

* ArduPilot
* PX4
* Copter
* Plane
* Rover

Galapagos detects the vehicle through MAVLink.

### How the host setting works

Galapagos treats the host field two different ways:

| Host | What Galapagos does |
|------|--------------------|
| `0.0.0.0` | **Listens** for the bridge (`udp://`). The bridge broadcasts telemetry to port 14550 and the app picks it up. This is the zero-config phone flow. |
| `192.168.4.1` | **Actively sends** to the bridge (`udpout://`). Use this only if you switch Galapagos to active-send mode (for example on a desktop). |

For a phone or tablet, leave the host at **`0.0.0.0`**. The bridge finds the app — you do not need to tell the app where the bridge is.

---

# Step 7 — Check the Bridge LED

The ESP32 has a status LED.

| LED        | Meaning                                        |
| ---------- | ---------------------------------------------- |
| Solid      | Radio link is active and data is being relayed |
| Slow blink | No radio data detected for more than 5 seconds |
| Fast blink | Wi-Fi is connecting                            |
| Off        | Fatal hardware/software error                  |

If the LED is solid and Galapagos receives telemetry, your bridge is working.

---

# That's It

Your complete setup is:

```text
             DRONE
               │
        Flight Controller
               │
               │ UART
               ▼
          Air Radio
               ))))
               ((((
          Ground Radio
               │
               │ UART
               ▼
             ESP32
               │
               │ Wi-Fi
               ▼
          iPhone / iPad
               │
               ▼
          Galapagos GCS
```

The ESP32 simply carries the MAVLink data between the ground radio and Galapagos.

---

# Troubleshooting

## I cannot see "Galapagos-Bridge"

1. Make sure the ESP32 is powered.
2. Wait 5–10 seconds.
3. Restart the ESP32.
4. Make sure the board was successfully flashed.
5. Try another USB power source.

---

## My phone connects to the Wi-Fi but Galapagos receives no telemetry

Check the physical radio connections:

```text
Radio TX → ESP32 RX
Radio RX → ESP32 TX
GND      → GND
```

Also check that:

* The ground radio is powered.
* The air radio is powered.
* The air radio is connected to the flight controller.
* The telemetry radio baud rate is correct.

For a standard SiK setup, **57600 baud** is the normal starting point.

---

## The bridge LED is slowly blinking

This means the ESP32 has not received radio data for more than approximately 5 seconds.

The Wi-Fi connection can still be working.

Check the connection between:

```text
Ground Radio ↔ ESP32
```

and then check:

```text
Air Radio ↔ Flight Controller
```

---

## I flashed the wrong board

Run the Web Flasher again and select the correct ESP32 family.

The supported families are:

* ESP32
* ESP32-S3
* ESP32-C3

---

## I am using a LoRa radio

Set the radio baud rate to:

```text
9600
```

The default bridge baud rate is:

```text
57600
```

SiK radios normally use 57600.

---

# Configuration

Most users never need to change the configuration.

The default configuration is:

| Setting        | Default            |
| -------------- | ------------------ |
| Wi-Fi name     | `Galapagos-Bridge` |
| Wi-Fi password | `galapagos`        |
| Wi-Fi channel  | `1`                |
| Radio baud     | `57600`            |
| UDP port       | `14550`            |

The Wi-Fi name, password, channel, and radio baud rate can be stored in the ESP32's non-volatile storage.

This means you do not normally need to recompile the firmware just to change these settings.

---

# Changing the Wi-Fi Name or Password

Advanced users can change the stored configuration using ESP-IDF.

For example:

```sh
idf.py --port /dev/cu.usbserial-0001 nvs-flash --key ssid --value MyBridge
```

Most users should not need this.

The default configuration is ready to use immediately after flashing:

```text
Wi-Fi:     Galapagos-Bridge
Password:  galapagos
```

---

# Firmware Updates

When installing new firmware, the Web Flasher may offer an option to erase the ESP32.

Erasing the device also removes saved configuration such as:

* Wi-Fi name
* Wi-Fi password
* Radio baud rate

If you want to keep your custom settings, do not erase the device unless the installer specifically requires it.

After an erase, the bridge returns to its default configuration.

---

# How the Bridge Works

The bridge does not parse MAVLink.

It simply forwards bytes.

```text
UART RX
   │
   ▼
RX buffer
   │
   ▼
UDP broadcast
   │
   ▼
Galapagos


Galapagos
   │
   ▼
UDP
   │
   ▼
TX buffer
   │
   ▼
UART TX
   │
   ▼
Ground Radio
```

MAVLink parsing, vehicle detection, missions, parameters, commands, geofences, and flight data are handled by Galapagos GCS.

This keeps the bridge simple and allows Galapagos to support new MAVLink features without requiring a new bridge firmware for every protocol change.

---

# Performance and Reliability

The bridge uses lock-free single-producer/single-consumer buffers for the two data paths.

When the link becomes saturated, the bridge drops and counts excess data instead of blocking the UART.

This prevents the radio interface from being stalled by a congested Wi-Fi connection.

The bridge also has a radio activity watchdog.

If no UART data is received for approximately 5 seconds, the bridge reports the radio link as silent and changes the LED state.

The bridge itself does not decide whether the aircraft is connected.

Galapagos uses MAVLink heartbeat monitoring to determine whether the flight controller is actually online.

---

# Hardware Architecture

```text
main/bridge/
├── spsc_ring.hpp
├── state_machine.hpp
├── config.hpp
├── config.cpp
├── uart_radio.hpp
├── uart_radio.cpp
├── wifi_ap.hpp
├── wifi_ap.cpp
├── udp_relay.hpp
├── udp_relay.cpp
├── app_state.hpp
├── app_state.cpp
├── link_watch.cpp
└── state_machine.cpp
```

The bridge uses:

* ESP-IDF
* UART
* Wi-Fi SoftAP
* UDP
* lock-free SPSC buffers
* non-blocking relay architecture
* NVS configuration storage

---

# Supported Hardware

### ESP32

Standard ESP32 development boards are supported.

### ESP32-S3

ESP32-S3 development boards are supported.

### ESP32-C3

ESP32-C3 development boards are supported using its dedicated UART configuration.

---

# License

See [LICENSE](LICENSE).
