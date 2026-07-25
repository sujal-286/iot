"""
bridge.py
---------------------------------------------------------
Runs on the Raspberry Pi 4B. Sits between:
  - the Arduino Uno (USB serial, running rover_autonomous.ino)
  - the GUI server on Render (Socket.IO, over the internet)

Responsibilities:
  1. Open the Arduino's serial port.
  2. Connect out to the Render server as a Socket.IO client
     (the Pi is almost always behind NAT/a router, so it
     connects OUT to Render rather than Render connecting in).
  3. Forward GUI commands (joystick, toggles) -> Arduino.
  4. Forward Arduino telemetry (sensor JSON) -> GUI, for every
     connected browser to see.

Install:
  pip3 install python-socketio[client] pyserial

Run:
  ROVER_SERVER_URL=https://your-app.onrender.com \
  ROVER_KEY=change-me \
  SERIAL_PORT=/dev/ttyACM0 \
  python3 bridge.py

Set ROVER_KEY to the same value as ROVER_KEY on the Render
service (see gui-server/README.md) -- it's how the server tells
the rover apart from a random browser tab.
---------------------------------------------------------
"""

import os
import sys
import json
import time
import threading

import serial
import socketio

SERIAL_PORT   = os.environ.get("SERIAL_PORT", "/dev/ttyACM0")
BAUD_RATE     = int(os.environ.get("BAUD_RATE", "9600"))
SERVER_URL    = os.environ.get("ROVER_SERVER_URL", "http://localhost:3000")
ROVER_KEY     = os.environ.get("ROVER_KEY", "change-me")
RECONNECT_SEC = 3

sio = socketio.Client(reconnection=True, reconnection_delay=RECONNECT_SEC)
ser = None
ser_lock = threading.Lock()


def open_serial():
    global ser
    while True:
        try:
            ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
            time.sleep(2)  # let the Arduino reset after the port opens
            print(f"[bridge] serial connected on {SERIAL_PORT}")
            return
        except serial.SerialException as e:
            print(f"[bridge] could not open {SERIAL_PORT}: {e} -- retrying in {RECONNECT_SEC}s")
            time.sleep(RECONNECT_SEC)


@sio.event
def connect():
    print("[bridge] connected to GUI server, registering as rover")
    sio.emit("register_rover", {"key": ROVER_KEY})


@sio.event
def disconnect():
    print("[bridge] disconnected from GUI server")


# Commands coming from the GUI (relayed by the server)
@sio.on("rover_command")
def on_rover_command(data):
    # data example: {"type": "joystick", "x": 40, "y": -80}
    #               {"type": "oa", "on": true}
    #               {"type": "auto", "on": false}
    #               {"type": "stop"}
    line = None
    t = data.get("type")

    if t == "joystick":
        x = int(data.get("x", 0))
        y = int(data.get("y", 0))
        line = f"J:{x},{y}"
    elif t == "oa":
        line = f"OA:{1 if data.get('on') else 0}"
    elif t == "auto":
        line = f"AUTO:{1 if data.get('on') else 0}"
    elif t == "stop":
        line = "STOP"

    if line and ser and ser.is_open:
        with ser_lock:
            ser.write((line + "\n").encode())


def serial_reader_loop():
    """Reads telemetry JSON lines from the Arduino and forwards them to the server."""
    global ser
    while True:
        if ser is None or not ser.is_open:
            open_serial()

        try:
            raw = ser.readline().decode(errors="ignore").strip()
        except serial.SerialException:
            print("[bridge] serial read failed, reopening port")
            try:
                ser.close()
            except Exception:
                pass
            ser = None
            continue

        if not raw:
            continue

        try:
            telemetry = json.loads(raw)
        except json.JSONDecodeError:
            continue  # partial/garbled line, skip it

        if sio.connected:
            sio.emit("rover_telemetry", telemetry)


def main():
    open_serial()

    reader_thread = threading.Thread(target=serial_reader_loop, daemon=True)
    reader_thread.start()

    while True:
        try:
            print(f"[bridge] connecting to {SERVER_URL} ...")
            sio.connect(SERVER_URL, transports=["websocket"])
            sio.wait()
        except Exception as e:
            print(f"[bridge] connection error: {e} -- retrying in {RECONNECT_SEC}s")
            time.sleep(RECONNECT_SEC)


if __name__ == "__main__":
    main()
