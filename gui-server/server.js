/*
  server.js
  ---------------------------------------------------------
  - Serves the static GUI (public/) to browsers.
  - Accepts a Socket.IO connection from the RPi bridge, which
    identifies itself with ROVER_KEY.
  - Relays:
      browser  -> "gui_command"      -> rover  "rover_command"
      rover    -> "rover_telemetry"  -> all browsers "telemetry"
  - Broadcasts rover online/offline status to browsers.

  Env vars (set these in the Render dashboard):
    ROVER_KEY  - shared secret the RPi bridge must send to be
                 trusted as "the rover" (anyone else is treated
                 as a viewer/controller browser)
    PORT       - Render sets this automatically
  ---------------------------------------------------------
*/

const path = require("path");
const express = require("express");
const { createServer } = require("http");
const { Server } = require("socket.io");

const ROVER_KEY = process.env.ROVER_KEY || "change-me";
const PORT = process.env.PORT || 3000;

const app = express();
const httpServer = createServer(app);
const io = new Server(httpServer, {
  cors: { origin: "*" },
});

app.use(express.static(path.join(__dirname, "public")));

let roverSocketId = null;

function broadcastRoverStatus() {
  io.emit("rover_status", { online: roverSocketId !== null });
}

io.on("connection", (socket) => {
  // Everyone starts as a "browser" until they prove they're the rover.
  broadcastRoverStatus(); // let the newcomer know current status immediately after they set up listeners

  socket.on("register_rover", ({ key } = {}) => {
    if (key !== ROVER_KEY) {
      socket.emit("register_denied");
      return;
    }
    roverSocketId = socket.id;
    socket.data.isRover = true;
    console.log(`[server] rover registered (${socket.id})`);
    broadcastRoverStatus();
  });

  // Browser -> rover
  socket.on("gui_command", (data) => {
    if (roverSocketId) {
      io.to(roverSocketId).emit("rover_command", data);
    }
  });

  // Rover -> all browsers
  socket.on("rover_telemetry", (data) => {
    if (socket.id === roverSocketId) {
      socket.broadcast.emit("telemetry", data);
    }
  });

  socket.on("disconnect", () => {
    if (socket.id === roverSocketId) {
      roverSocketId = null;
      console.log("[server] rover disconnected");
      broadcastRoverStatus();
    }
  });
});

httpServer.listen(PORT, () => {
  console.log(`[server] listening on ${PORT}`);
});
