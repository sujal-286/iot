/**
 * Rover Console — Bridge Server
 * Runs on Render (or any Node host).
 * 
 * Two WebSocket paths:
 *   /esp   — the ESP8266 connects here and sends sensor data / receives commands
 *   /ui    — the browser connects here for the live dashboard
 * 
 * HTTP  GET /health  — used by Render health checks
 */

const express = require('express');
const http    = require('http');
const { WebSocketServer, WebSocket } = require('ws');
const path    = require('path');

const app    = express();
const server = http.createServer(app);

// ── Static files (the dashboard) ─────────────────────────────────────────────
app.use(express.static(path.join(__dirname, 'public')));
app.get('/health', (_req, res) => res.send('OK'));

// ── WebSocket servers (share the same HTTP server, different paths) ───────────
const wssESP = new WebSocketServer({ noServer: true });   // ESP8266 connection
const wssUI  = new WebSocketServer({ noServer: true });   // Browser connections

server.on('upgrade', (req, socket, head) => {
  if (req.url === '/esp') {
    wssESP.handleUpgrade(req, socket, head, ws => wssESP.emit('connection', ws, req));
  } else if (req.url === '/ui') {
    wssUI.handleUpgrade(req, socket, head, ws => wssUI.emit('connection', ws, req));
  } else {
    socket.destroy();
  }
});

// ── State ────────────────────────────────────────────────────────────────────
let espSocket = null;          // only one rover at a time
let lastSensorData = null;     // cache for late-joining UIs

function broadcastToUI(payload) {
  const msg = JSON.stringify(payload);
  wssUI.clients.forEach(c => {
    if (c.readyState === WebSocket.OPEN) c.send(msg);
  });
}

// ── ESP8266 handler ──────────────────────────────────────────────────────────
wssESP.on('connection', (ws, req) => {
  console.log('[ESP] Rover connected from', req.socket.remoteAddress);
  espSocket = ws;

  broadcastToUI({ type: 'rover_status', online: true });

  ws.on('message', raw => {
    try {
      const data = JSON.parse(raw);
      // data = { type:"sensors", front:XX, back:XX, left:XX, right:XX }
      lastSensorData = data;
      broadcastToUI(data);
    } catch (e) {
      console.error('[ESP] Bad message:', e.message);
    }
  });

  ws.on('close', () => {
    console.log('[ESP] Rover disconnected');
    espSocket = null;
    broadcastToUI({ type: 'rover_status', online: false });
  });

  ws.on('error', err => console.error('[ESP] Error:', err.message));
});

// ── Browser UI handler ───────────────────────────────────────────────────────
wssUI.on('connection', ws => {
  console.log('[UI ] Browser connected');

  // Tell the new client the current rover state immediately
  ws.send(JSON.stringify({ type: 'rover_status', online: !!espSocket }));
  if (lastSensorData) ws.send(JSON.stringify(lastSensorData));

  ws.on('message', raw => {
    // Commands from browser → forwarded to ESP
    // { type:"drive",  vx:0-255, vy:0-255 }   (joystick)
    // { type:"mode",   mode:"manual"|"auto" }
    // { type:"obstacle", enabled:true|false }
    // { type:"stop" }
    if (!espSocket || espSocket.readyState !== WebSocket.OPEN) return;
    try {
      espSocket.send(raw.toString());
    } catch (e) {
      console.error('[UI ] Forward error:', e.message);
    }
  });

  ws.on('close', () => console.log('[UI ] Browser disconnected'));
  ws.on('error', err => console.error('[UI ] Error:', err.message));
});

// ── Start ────────────────────────────────────────────────────────────────────
const PORT = process.env.PORT || 3000;
server.listen(PORT, () => console.log(`Rover bridge running on :${PORT}`));
