/**
 * ═══════════════════════════════════════════════════════════════
 *  ROVER — ESP8266 NodeMCU Sketch
 *  Responsibilities:
 *   1. Connect to WiFi
 *   2. Open a WebSocket to the Render server  /esp  endpoint
 *   3. Forward sensor JSON from Arduino (Hardware Serial) → WebSocket
 *   4. Forward commands from WebSocket → Arduino (Hardware Serial)
 * ═══════════════════════════════════════════════════════════════
 *
 *  WIRING to Arduino:
 *    ESP8266 TX (GPIO1)  →  Arduino Pin 2   (SoftwareSerial RX)
 *    ESP8266 RX (GPIO3)  →  Arduino Pin 3   (SoftwareSerial TX)
 *    ESP8266 GND         →  Arduino GND     (common ground)
 *    ⚠️  Arduino operates at 5V logic; ESP8266 at 3.3V.
 *       Use a voltage divider on Arduino TX → ESP RX:
 *         Arduino Pin3 → 1kΩ → ESP GPIO3
 *                              ↕ 2kΩ → GND
 *       ESP TX → Arduino is fine directly (3.3V is read as HIGH by Uno).
 *
 *  INSTALL LIBRARIES (Arduino IDE → Sketch → Include Library → Manage Libraries):
 *    • ESP8266WiFi          (comes with ESP8266 board package)
 *    • WebSocketsClient     by Links2004  (search "WebSockets")
 *    • ArduinoJson          by Benoit Blanchon (search "ArduinoJson") v6+
 * ═══════════════════════════════════════════════════════════════
 *
 *  BOARD MANAGER URL (File → Preferences → Additional Boards Manager URLs):
 *    http://arduino.esp8266.com/stable/package_esp8266com_index.json
 *  Then: Tools → Board → Boards Manager → search "esp8266" → install
 *  Select: Tools → Board → NodeMCU 1.0 (ESP-12E Module)
 * ═══════════════════════════════════════════════════════════════
 */

#include <ESP8266WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

// ══ ⚙️  CONFIGURE THESE ══════════════════════════════════════════
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASS     = "YOUR_WIFI_PASSWORD";

// Your Render deployment URL (no https://, no trailing slash)
const char* WS_HOST       = "your-app-name.onrender.com";
const int   WS_PORT       = 443;          // Render uses HTTPS/WSS
const char* WS_PATH       = "/esp";
// ═════════════════════════════════════════════════════════════════

WebSocketsClient ws;

bool wsConnected = false;

String arduinoBuffer = "";

// ── WebSocket event handler ───────────────────────────────────────────────
void wsEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {

    case WStype_CONNECTED:
      wsConnected = true;
      Serial.println("[WS] Connected to server");
      break;

    case WStype_DISCONNECTED:
      wsConnected = false;
      Serial.println("[WS] Disconnected");
      break;

    case WStype_TEXT: {
      // Command from server → forward to Arduino
      String cmd = String((char*)payload);
      cmd += "\n";
      // Hardware Serial (pins 1/3) on NodeMCU goes to Arduino SoftwareSerial
      Serial.print(cmd);   // TX goes to Arduino pin2 (SW RX)
      break;
    }

    case WStype_ERROR:
      Serial.println("[WS] Error");
      break;

    default: break;
  }
}

void setup() {
  // NodeMCU hardware serial = GPIO1(TX)/GPIO3(RX) = pins labeled TX/RX
  // Baud must match Arduino SoftwareSerial baud
  Serial.begin(9600);

  // Connect WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\nWiFi OK: " + WiFi.localIP().toString());

  // WebSocket — uses WSS (SSL) for Render
  ws.beginSSL(WS_HOST, WS_PORT, WS_PATH);
  ws.onEvent(wsEvent);
  ws.setReconnectInterval(3000);
  ws.enableHeartbeat(15000, 3000, 2);   // ping every 15s
}

void loop() {
  ws.loop();

  // Read sensor data from Arduino → forward to WebSocket
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      arduinoBuffer.trim();
      if (arduinoBuffer.length() > 2 && wsConnected) {
        ws.sendTXT(arduinoBuffer);
      }
      arduinoBuffer = "";
    } else {
      arduinoBuffer += c;
    }
  }
}
