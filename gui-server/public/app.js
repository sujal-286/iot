const socket = io();

// ---------- connection status ----------
const serverDot = document.getElementById("serverDot");
const serverLabel = document.getElementById("serverLabel");
const roverDot = document.getElementById("roverDot");
const roverLabel = document.getElementById("roverLabel");

socket.on("connect", () => {
  serverDot.className = "dot dot--on";
  serverLabel.textContent = "Console linked";
});
socket.on("disconnect", () => {
  serverDot.className = "dot dot--off";
  serverLabel.textContent = "Console offline";
  roverDot.className = "dot dot--off";
  roverLabel.textContent = "Rover offline";
});
socket.on("rover_status", ({ online }) => {
  roverDot.className = online ? "dot dot--on" : "dot dot--off";
  roverLabel.textContent = online ? "Rover online" : "Rover offline";
});

// ---------- radar ----------
const wedgeL = document.getElementById("wedgeL");
const wedgeC = document.getElementById("wedgeC");
const wedgeR = document.getElementById("wedgeR");
const valL = document.getElementById("valL");
const valC = document.getElementById("valC");
const valR = document.getElementById("valR");
const modeTag = document.getElementById("modeTag");

const DANGER_CM = 25;
const CAUTION_CM = 60;

function classifyDistance(d) {
  if (d == null || d < 0) return "wedge--safe"; // no echo = nothing detected = clear
  if (d < DANGER_CM) return "wedge--danger";
  if (d < CAUTION_CM) return "wedge--caution";
  return "wedge--safe";
}

function updateRadar({ left, center, right, mode }) {
  wedgeL.setAttribute("class", `wedge ${classifyDistance(left)}`);
  wedgeC.setAttribute("class", `wedge ${classifyDistance(center)}`);
  wedgeR.setAttribute("class", `wedge ${classifyDistance(right)}`);

  valL.textContent = left != null && left >= 0 ? `${left.toFixed(0)} cm` : "— cm";
  valC.textContent = center != null && center >= 0 ? `${center.toFixed(0)} cm` : "— cm";
  valR.textContent = right != null && right >= 0 ? `${right.toFixed(0)} cm` : "— cm";

  if (mode) modeTag.textContent = mode.toUpperCase();
}

socket.on("telemetry", (data) => {
  updateRadar(data);
  if (typeof data.oa === "number") setToggleState(oaToggle, !!data.oa);
  if (typeof data.auto === "number") setToggleState(autoToggle, !!data.auto);
});

// ---------- toggles ----------
const oaToggle = document.getElementById("oaToggle");
const autoToggle = document.getElementById("autoToggle");

function setToggleState(btn, on) {
  btn.dataset.on = on ? "true" : "false";
}

oaToggle.addEventListener("click", () => {
  const next = oaToggle.dataset.on !== "true";
  setToggleState(oaToggle, next);
  socket.emit("gui_command", { type: "oa", on: next });
});

autoToggle.addEventListener("click", () => {
  const next = autoToggle.dataset.on !== "true";
  setToggleState(autoToggle, next);
  socket.emit("gui_command", { type: "auto", on: next });
});

document.getElementById("stopBtn").addEventListener("click", () => {
  setToggleState(autoToggle, false);
  socket.emit("gui_command", { type: "stop" });
  resetKnob();
});

// ---------- joystick ----------
const joyBase = document.getElementById("joyBase");
const joyKnob = document.getElementById("joyKnob");

const JOY_RADIUS = 95; // matches .joy-base width/2
const KNOB_RADIUS = 31;
const MAX_TRAVEL = JOY_RADIUS - KNOB_RADIUS;

let dragging = false;
let lastSent = 0;
const SEND_INTERVAL_MS = 90;

function resetKnob() {
  joyKnob.style.transform = "translate(-50%, -50%)";
  socket.emit("gui_command", { type: "joystick", x: 0, y: 0 });
}

function handleMove(clientX, clientY) {
  const rect = joyBase.getBoundingClientRect();
  const centerX = rect.left + rect.width / 2;
  const centerY = rect.top + rect.height / 2;

  let dx = clientX - centerX;
  let dy = clientY - centerY;
  const dist = Math.min(Math.hypot(dx, dy), MAX_TRAVEL);
  const angle = Math.atan2(dy, dx);

  dx = Math.cos(angle) * dist;
  dy = Math.sin(angle) * dist;

  joyKnob.style.transform = `translate(calc(-50% + ${dx}px), calc(-50% + ${dy}px))`;

  const x = Math.round((dx / MAX_TRAVEL) * 100);
  const y = Math.round((-dy / MAX_TRAVEL) * 100); // up = positive throttle

  const now = Date.now();
  if (now - lastSent > SEND_INTERVAL_MS) {
    lastSent = now;
    socket.emit("gui_command", { type: "joystick", x, y });
  }
}

joyBase.addEventListener("pointerdown", (e) => {
  dragging = true;
  joyBase.setPointerCapture(e.pointerId);
  handleMove(e.clientX, e.clientY);
});
joyBase.addEventListener("pointermove", (e) => {
  if (dragging) handleMove(e.clientX, e.clientY);
});
["pointerup", "pointercancel", "pointerleave"].forEach((evt) => {
  joyBase.addEventListener(evt, () => {
    if (!dragging) return;
    dragging = false;
    resetKnob();
  });
});
