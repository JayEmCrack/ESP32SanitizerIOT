"use strict";

// ---- Settings (must match firmware) ----
const BROKER_URL = "wss://broker.hivemq.com:8884/mqtt"; // secure WebSocket (needed on https pages)
const TOPIC_ROOT = "sanitizer-iot";                      // same as firmware topics
const STALE_MS = 30000;                                  // no heartbeat for 30 s => offline

const $ = (id) => document.getElementById(id);

// ---- Device ID: from URL (#id) or saved in this browser. Never stored in the repo. ----
function getDeviceId() {
  let id = decodeURIComponent(location.hash.replace(/^#/, "")).trim();
  if (!id) {
    try { id = localStorage.getItem("deviceId") || ""; } catch (e) { id = ""; }
  }
  if (!id) {
    id = (prompt("Enter your Device ID (the DEVICE_ID from secrets.h):") || "").trim();
  }
  if (!/^[A-Za-z0-9_-]{6,64}$/.test(id)) return "";
  try { localStorage.setItem("deviceId", id); } catch (e) {}
  return id;
}

const deviceId = getDeviceId();
if (!deviceId) {
  $("connStatus").textContent =
    "Invalid or missing Device ID. Open this page as  ...index.html#YOUR_DEVICE_ID";
  throw new Error("No valid device ID");
}
$("deviceIdLabel").textContent = "Device: " + deviceId;

const BASE = TOPIC_ROOT + "/" + deviceId;
const TOPIC_STATE = BASE + "/state";
const TOPIC_ONLINE = BASE + "/online";
const TOPIC_CMD = BASE + "/cmd";

// ---- App state ----
let onlineFlag = false;   // from retained/LWT topic
let lastLive = 0;         // time of last live (non-retained) state message
let lastState = null;
let manualLockUntil = 0;

// ---- MQTT ----
const client = mqtt.connect(BROKER_URL, {
  clientId: "dash-" + Math.random().toString(16).slice(2, 10),
  reconnectPeriod: 3000,
  connectTimeout: 10000,
  clean: true
});

client.on("connect", () => {
  $("connStatus").textContent = "Broker: connected";
  client.subscribe([TOPIC_STATE, TOPIC_ONLINE], (err) => {
    if (err) $("connStatus").textContent = "Subscribe failed: " + err.message;
  });
});
client.on("reconnect", () => { $("connStatus").textContent = "Broker: reconnecting..."; });
client.on("close",     () => { $("connStatus").textContent = "Broker: disconnected"; });
client.on("error",     (e) => { $("connStatus").textContent = "Broker error: " + e.message; });

client.on("message", (topic, payload, packet) => {
  const text = payload.toString();
  if (topic === TOPIC_ONLINE) {
    onlineFlag = (text === "online");
  } else if (topic === TOPIC_STATE) {
    try { lastState = JSON.parse(text); } catch (e) { return; }
    if (!packet.retain) lastLive = Date.now();   // retained = old data from the broker
  }
  render();
});

// ---- Rendering ----
function setBadge(el, text, cls) {
  el.textContent = text;
  el.className = "badge " + cls;
}

function formatLast(epoch, count) {
  if (!epoch) return count > 0 ? "Time unavailable (device clock not synced)" : "No dispense yet";
  const d = new Date(epoch * 1000);
  const date = d.toLocaleDateString("en-US", { year: "numeric", month: "long", day: "numeric" });
  const time = d.toLocaleTimeString("en-US", { hour: "numeric", minute: "2-digit" });
  return date + " - " + time;
}

function isOnline() {
  return onlineFlag && (Date.now() - lastLive) < STALE_MS;
}

function render() {
  const online = isOnline();
  const s = lastState;

  setBadge($("deviceStatus"), online ? "ONLINE" : "OFFLINE", online ? "on" : "off");
  $("lastSeen").textContent = lastLive
    ? "Last update: " + Math.round((Date.now() - lastLive) / 1000) + " s ago"
    : "Waiting for data...";

  document.querySelectorAll(".card").forEach((c, i) => {
    if (i > 0) c.classList.toggle("stale", !online);
  });

  if (s) {
    setBadge($("sensorStatus"),
      s.sensor === "HAND" ? "Hand Detected" : "No Hand",
      s.sensor === "HAND" ? "warn" : "on");

    const san = {
      READY: ["Ready", "on"],
      DISPENSING: ["Dispensing", "busy"],
      COOLDOWN: ["Cooldown", "warn"],
      WAIT_REMOVAL: ["Remove hand", "warn"]
    }[s.sanitizer] || [String(s.sanitizer), "off"];
    setBadge($("sanitizerStatus"), san[0], san[1]);

    setBadge($("servoStatus"), "Servo: " + s.servo, s.servo === "HOME" ? "on" : "busy");
    $("count").textContent = s.count;
    $("lastDispense").textContent = formatLast(s.last, s.count);
  }

  const canUse = online && s && s.sanitizer === "READY" && Date.now() > manualLockUntil;
  $("btnDispense").disabled = !canUse;
  $("btnReset").disabled = !online;
}

setInterval(render, 1000);
render();

// ---- Buttons ----
function sendCommand(cmd) {
  if (!client.connected) { alert("Not connected to the broker."); return; }
  client.publish(TOPIC_CMD, cmd, { qos: 0, retain: false });
}

$("btnDispense").addEventListener("click", () => {
  manualLockUntil = Date.now() + 3000;   // prevent double-clicks
  $("btnDispense").disabled = true;
  sendCommand("dispense");
});

$("btnReset").addEventListener("click", () => {
  if (confirm("Reset the dispense counter to 0?")) sendCommand("reset");
});
