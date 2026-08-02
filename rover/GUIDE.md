# 🤖 Rover Build Guide — Complete Step-by-Step

---

## 📦 Your Parts List

| Part | Qty |
|------|-----|
| Kit4Curious 4WD Acrylic Chassis (4 BO motors + wheels) | 1 |
| HC-SR04 Ultrasonic Sensor | 4 |
| Arduino Uno R3 | 1 |
| ESP8266 NodeMCU (ESP-12E) | 1 |
| L293D Motor Driver Shield for Arduino | 1 |
| 18650 Li-ion Batteries | 2 |
| 18650 Battery Case (2-cell, with leads) | 1 |

---

## PART 1 — ASSEMBLY ORDER

Build in this order so you don't have to undo things:

### Step 1 — Assemble the chassis
1. Mount all 4 BO motors into the acrylic chassis slots (2 per side).
2. Press wheels onto motor shafts.
3. The kit includes a sensor/servo holder — mount it on the **front** of the top deck.

### Step 2 — Stack the Motor Shield onto Arduino
- Align the L293D Motor Shield pins directly onto Arduino Uno.
- Press down firmly until all pins seat. No soldering needed for standard shields.
- The shield covers D0–D13 and the power pins. That is fine.
- Motors connect to **M1, M2, M3, M4** terminals on the shield.

### Step 3 — Motor wiring
Wire the 4 BO motors to the shield terminals:

| Terminal | Motor | Position |
|----------|-------|----------|
| M1 | Motor 1 | Front-Left |
| M2 | Motor 2 | Front-Right |
| M3 | Motor 3 | Rear-Left |
| M4 | Motor 4 | Rear-Right |

> **Polarity tip:** The direction (forward/backward) of each motor depends on which wire goes to which screw. If a wheel spins the wrong way after upload, just swap the two wires on that terminal.

---

## PART 2 — ULTRASONIC SENSOR WIRING

You have 4 HC-SR04 sensors. Each has 4 pins: VCC, GND, TRIG, ECHO.

| Sensor | TRIG | ECHO | VCC | GND |
|--------|------|------|-----|-----|
| Front | A0 | A1 | 5V | GND |
| Back | A2 | A3 | 5V | GND |
| Left | D6 | D7 | 5V | GND |
| Right | D8 | D9 | 5V | GND |

> Connect VCC of all sensors to the **5V pin** on the Arduino (the motor shield exposes these headers on its outer edge — look for a row of 5V/GND pins on the shield).

**Mounting positions:**
- Front sensor: front bumper / sensor holder, facing forward
- Back sensor: rear, facing backward  
- Left sensor: left side panel, facing left
- Right sensor: right side panel, facing right

---

## PART 3 — ESP8266 WIRING TO ARDUINO

### ⚠️ CRITICAL — Voltage Levels
- Arduino Uno = **5V logic**  
- ESP8266 = **3.3V logic** (GPIO pins cannot tolerate 5V, they WILL be damaged)

You need a **voltage divider** on the line from Arduino TX → ESP RX.

### Voltage Divider (Arduino Pin 3 → ESP GPIO3/RX)
```
Arduino Pin 3 ──── 1kΩ ──┬──── ESP8266 GPIO3 (RX)
                          │
                         2kΩ
                          │
                         GND
```
Use 1kΩ and 2kΩ resistors (or closest available: 1kΩ + 2.2kΩ is fine).

### Direction is safe (no divider needed):
`ESP8266 TX (GPIO1) → Arduino Pin 2` — 3.3V is read as HIGH by Arduino, no issue.

### Full ESP ↔ Arduino connections:

| ESP8266 Pin | Wire | Arduino Pin |
|-------------|------|-------------|
| GPIO1 (TX) | direct | Pin 2 (SW RX) |
| GPIO3 (RX) | via divider | Pin 3 (SW TX) |
| GND | direct | GND |
| 3V3 | — | Do NOT connect to Arduino 5V! |

> Power the ESP8266 from its own **VIN pin from the battery** or from a separate 3.3V regulator. See Part 5.

---

## PART 4 — POWERING THE SYSTEM

### 4A — USB Only (Bench testing, no batteries)

| Board | How to power |
|-------|-------------|
| Arduino Uno | USB cable → your laptop |
| L293D Shield | Already powered through Arduino |
| ESP8266 NodeMCU | Second USB cable → laptop OR use a USB hub |

> ⚠️ USB power is **not enough to drive 4 motors**. Use this only for code upload and sensor testing. Motors will stutter or not run.

---

### 4B — Two 18650 Li-ion Batteries (Recommended for driving)

A 2-cell 18650 battery case in **series** gives you **7.4V nominal (8.4V fully charged)**.

#### Step-by-step battery connection:

1. **Battery case**: Make sure the 2 cells are wired **in series** (most 2-cell cases with a single output lead do this automatically — red = positive, black = negative).

2. **Voltage range**: 6.0V (discharged) to 8.4V (full). Arduino Uno accepts 7–12V on its barrel jack. This is within range.

3. **Connect battery to Arduino**:
   - Red wire (positive) → Arduino barrel jack center pin (tip)
   - Black wire (negative) → Arduino barrel jack outer ring  
   
   > You can buy or make a 2.1mm barrel connector adapter, or solder a barrel jack plug onto the battery wires.

4. **Arduino powers the Motor Shield**: The shield gets power from Arduino's VIN once the barrel jack is connected. No separate connection needed.

5. **Power the ESP8266**:  
   The ESP8266's **VIN pin** accepts 4.5V–10V. Connect:
   - Battery positive → ESP8266 VIN
   - Battery GND → ESP8266 GND  
   
   > Alternatively: connect ESP8266 VIN to Arduino's **5V pin** (the shield exposes this). The Arduino's onboard regulator steps down 7.4V to 5V. This works at lower motor loads. At full motor draw, the 5V rail may sag — using VIN directly to ESP is more robust.

6. **Common Ground**: Arduino GND, ESP8266 GND, and battery negative must all share one common GND. Run a wire from Arduino GND to ESP GND if not already connected.

#### Summary wiring diagram:

```
  [18650 x2 Series]
        │
   +7.4V (red)
        ├──────────────────────── Arduino Barrel Jack (+)
        │                         → Arduino powers Motor Shield
        │                         → Arduino 5V rail available
        └──────────────────────── ESP8266 VIN (or Arduino 5V pin)

   GND (black)
        ├──────────────────────── Arduino Barrel Jack (-)
        └──────────────────────── ESP8266 GND
```

#### Adding a power switch (recommended):
Wire a SPST rocker switch in series with the battery positive lead. This gives you a clean on/off without unplugging anything.

```
Battery (+) → Switch → Arduino barrel jack (+)
```

---

## PART 5 — SOFTWARE SETUP

### 5A — Install Arduino IDE
Download from: https://www.arduino.cc/en/software  
Use version 2.x.

### 5B — Install ESP8266 board support
1. Arduino IDE → File → Preferences
2. In "Additional Boards Manager URLs" paste:
   ```
   http://arduino.esp8266.com/stable/package_esp8266com_index.json
   ```
3. Tools → Board → Boards Manager → search "esp8266" → Install (by ESP8266 Community)

### 5C — Install required libraries
Go to: Sketch → Include Library → Manage Libraries

Search and install each:
| Library | Author |
|---------|--------|
| `AFMotor` (Adafruit Motor Shield Library v1) | Adafruit |
| `WebSockets` | Links2004 |
| `ArduinoJson` | Benoit Blanchon |

---

## PART 6 — UPLOADING THE CODE

### 6A — Upload to Arduino Uno FIRST

> **Important:** Disconnect the wire from **Arduino Pin 2** (the SoftwareSerial RX from ESP) **before** uploading. If it's connected, uploads may fail.

1. Open `arduino/arduino_rover.ino` in Arduino IDE
2. Tools → Board → **Arduino Uno**
3. Tools → Port → select your Arduino's COM port
4. Click Upload (→ arrow button)
5. Wait for "Done uploading"
6. Reconnect Pin 2 wire to ESP TX

### 6B — Configure and upload to ESP8266

1. Open `esp8266/esp8266_rover.ino` in Arduino IDE
2. Edit lines 44-51:
   ```cpp
   const char* WIFI_SSID = "YourNetworkName";
   const char* WIFI_PASS = "YourPassword";
   const char* WS_HOST   = "your-app-name.onrender.com"; // after deploying
   ```
3. Tools → Board → **NodeMCU 1.0 (ESP-12E Module)**
4. Tools → Upload Speed → **115200**
5. Tools → Port → select the ESP8266's COM port
6. Click Upload

> To see debug output: Tools → Serial Monitor → set to 9600 baud.

---

## PART 7 — DEPLOY THE WEB SERVER TO RENDER

### 7A — Push code to GitHub
1. Create a free GitHub account if you don't have one.
2. Create a new repository (e.g., `rover-console`).
3. Upload the contents of the `rover/` folder (server.js, package.json, render.yaml, public/).

### 7B — Deploy on Render
1. Go to https://render.com and sign up (free tier works).
2. Click **New** → **Web Service**
3. Connect your GitHub account and select your `rover-console` repo.
4. Render auto-detects `render.yaml` — settings fill automatically.
5. Click **Create Web Service**.
6. Wait 2–3 minutes for build. You get a URL like:
   `https://rover-console-xxxx.onrender.com`

### 7C — Update ESP8266 with your Render URL
1. Open `esp8266_rover.ino`
2. Change:
   ```cpp
   const char* WS_HOST = "rover-console-xxxx.onrender.com";
   ```
3. Re-upload to ESP8266.

### 7D — Access the dashboard
Open your Render URL in any browser, from anywhere in the world:
```
https://rover-console-xxxx.onrender.com
```

> **Free Render note:** Free tier services spin down after 15 min of inactivity. The ESP8266 WebSocket reconnect loop will wake it back up within ~30 seconds. If you want always-on, upgrade to Render's paid tier ($7/month).

---

## PART 8 — OPERATING THE ROVER

### Controls
| Control | Action |
|---------|--------|
| Drag joystick | Steer (tank-style) |
| Release joystick | Stop |
| W/A/S/D or Arrow keys | Keyboard drive |
| Spacebar | Emergency Stop |
| Obstacle Assist toggle | Prevents driving into walls (manual) |
| Autonomous Mode toggle | Rover drives itself (obstacle avoidance mandatory) |
| EMERGENCY STOP button | Halts everything, returns to manual |

### Radar colors
| Color | Meaning | Distance |
|-------|---------|---------|
| 🟢 Green | Clear | > 40 cm |
| 🟡 Yellow | Caution | 20–40 cm |
| 🔴 Red | Danger | < 20 cm |

### Autonomous mode behavior
- Drives forward while all paths clear
- Stops and turns toward clearer side when obstacle < 20cm ahead
- Gently steers away from side obstacles
- Cannot be used without obstacle avoidance

---

## PART 9 — TROUBLESHOOTING

| Problem | Fix |
|---------|-----|
| Motors spin wrong direction | Swap the two wires on that motor's shield terminal |
| Upload to Arduino fails | Disconnect Pin 2 from ESP before uploading |
| Upload to ESP fails | Hold FLASH button while clicking upload, release when uploading starts |
| ESP won't connect to WiFi | Check SSID/password exactly (case-sensitive). 2.4GHz only — ESP8266 does not support 5GHz |
| "Rover offline" on dashboard | Check ESP Serial Monitor — is it connecting to WiFi and WebSocket? |
| Sensors read 400cm always | Check TRIG/ECHO wiring; confirm 5V and GND connected to sensors |
| Motors don't run on battery | Ensure battery is actually connected to barrel jack; check polarity |
| Joystick doesn't respond | Check that "Autonomous Mode" is not active; check WebSocket status in log |

---

## PART 10 — FILE STRUCTURE

```
rover/
├── server.js           ← Node.js WebSocket bridge (runs on Render)
├── package.json        ← Node dependencies
├── render.yaml         ← Render deployment config
├── public/
│   └── index.html      ← The dashboard UI (served by server.js)
├── arduino/
│   └── arduino_rover.ino   ← Upload to Arduino Uno
└── esp8266/
    └── esp8266_rover.ino   ← Upload to ESP8266 NodeMCU
```
