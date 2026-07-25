# Rover control stack

Three pieces, matching your split-compute architecture (Arduino = motors/sensors,
Raspberry Pi = bridge/networking):

```
[ Browser GUI ]  <--WebSocket-->  [ Render server ]  <--WebSocket-->  [ RPi bridge.py ]  <--USB Serial-->  [ Arduino Uno ]
   joystick,           relays both directions,           runs on the rover,          rover_autonomous.ino
   toggles, radar       tracks who's "the rover"          talks to Arduino
```

The GUI is hosted on Render (public internet). The Raspberry Pi on the rover
connects **out** to it (so nothing needs to be port-forwarded on your home
network) and identifies itself with a shared secret, `ROVER_KEY`.

## 1. Arduino

- `arduino/ultrasonic_test/` — wire ONE HC-SR04 at a time to the pins you plan
  to use, run this first, confirm you get sane cm readings in Serial Monitor.
- `arduino/rover_autonomous/` — the real sketch. Requires the **AFMotor**
  library (Library Manager → search "AFMotor" / "Adafruit Motor Shield
  library") since your L293D shield is the classic Adafruit-v1-style clone.

  **Pin assumption**: that shield uses pins 3,4,5,6,7,8,11,12 internally, so
  the three ultrasonic sensors are wired to the pins left over:

  | Sensor | TRIG | ECHO |
  |--------|------|------|
  | Left   | A0   | A1   |
  | Center | 9    | 10   |
  | Right  | 13   | 2    |

  Double check this against your actual shield (silkscreen / datasheet) using
  the test sketch before trusting it — cheap clone shields vary. Also check
  which `AF_DCMotor(n)` number ends up on which physical wheel and fix the
  `motorFL/FR/RL/RR` assignment at the top of the sketch to match your wiring.

- Serial protocol (9600 baud), see comments in `rover_autonomous.ino`:
  - In: `J:<x>,<y>` (joystick, -100..100), `OA:1`/`OA:0`, `AUTO:1`/`AUTO:0`, `STOP`
  - Out (every ~150 ms): `{"left":..,"center":..,"right":..,"oa":0/1,"auto":0/1,"mode":"manual|assist|auto"}`

## 2. Raspberry Pi bridge

```bash
cd rpi-bridge
pip3 install -r requirements.txt
ROVER_SERVER_URL=https://your-app.onrender.com \
ROVER_KEY=some-long-random-string \
SERIAL_PORT=/dev/ttyACM0 \
python3 bridge.py
```

Find your Arduino's port with `ls /dev/ttyACM* /dev/ttyUSB*`. Consider setting
this up as a `systemd` service so it starts on boot and auto-restarts.

## 3. GUI server (deploy to Render)

```bash
cd gui-server
npm install     # to test locally: npm start, then open http://localhost:3000
```

**Deploying to Render:**
1. Push `gui-server/` to a GitHub repo (or the whole `rover-project/` — Render
   lets you set the root directory).
2. Render dashboard → New → Web Service → connect the repo.
3. Root directory: `gui-server` (if you pushed the whole project).
4. Build command: `npm install`. Start command: `npm start`.
5. Environment variable: `ROVER_KEY` = the same string you'll pass to
   `bridge.py` on the Pi.
6. Deploy. Render gives you a URL like `https://your-app.onrender.com` — that's
   both the GUI page (open it in a browser) and the `ROVER_SERVER_URL` for the
   Pi bridge.

Free Render web services sleep after inactivity and take ~30-60s to wake up
on the next request — worth knowing if the rover "goes offline" after sitting
idle; the bridge's auto-reconnect will bring it back once the service wakes.

## Notes on the GUI

- Joystick: `x` = steering, `y` = throttle, both -100..100, sent as the
  rover moves (not just on release).
- **Obstacle Assist**: manual driving stays under your control, but forward
  motion is blocked if a sensor sees something closer than ~30 cm.
- **Autonomous Mode**: rover drives itself; touching the joystick while it's
  on hands control back to you (mirrors what the firmware does — see
  `handleCommand()` in the sketch).
- Radar: green = clear, amber = caution (<60 cm), red = danger (<25 cm),
  per sector. Adjust `DANGER_CM`/`CAUTION_CM` in `public/app.js` to match
  `AUTO_STOP_CM`/`SAFE_DISTANCE_CM` in the sketch if you tune those.
