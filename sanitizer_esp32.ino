/*
  ESP32 Automatic Sanitizer IoT
  Board: ESP32 Dev Module (Arduino IDE)
  Libraries: PubSubClient, ESP32Servo (+ built-in WiFi, Preferences, time)
*/

#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <Preferences.h>
#include <time.h>
#include "secrets.h"   // WIFI_SSID, WIFI_PASSWORD, DEVICE_ID

// =====================================================
//                 CONFIGURATION (edit here)
// =====================================================

// ---- Pins ----
const int SERVO_PIN = 13;   // Servo signal
const int IR_PIN    = 27;   // IR sensor OUT (safe pin, not strapping)

// ---- IR sensor logic ----
// Many modules (FC-51) output LOW when an object is detected -> LOW.
// If yours outputs HIGH when detected, change to HIGH.
const bool OBJECT_DETECTED_STATE = LOW;

// ---- Servo (CALIBRATE THESE on your real pump!) ----
const int SERVO_HOME     = 0;     // resting angle (degrees)
const int SERVO_DISPENSE = 90;    // pressing angle (degrees). Adjust slowly!
const int SERVO_MIN_SAFE = 0;     // never command below this
const int SERVO_MAX_SAFE = 180;   // never command above this (lower it to protect your mechanism)
const int SERVO_MIN_US   = 500;   // pulse width for 0 deg (SG90: 500)
const int SERVO_MAX_US   = 2400;  // pulse width for 180 deg (SG90: 2400)
const bool DETACH_SERVO_WHEN_IDLE = true; // stops buzzing/forcing when idle

// ---- Timing (milliseconds) ----
const unsigned long DISPENSE_TIME      = 500;   // time servo holds the pump pressed
const unsigned long SERVO_SETTLE_TIME  = 600;   // time to let servo reach home
const unsigned long COOLDOWN_TIME      = 2000;  // pause before next dispense allowed
const unsigned long SENSOR_DEBOUNCE_MS = 50;    // sensor must be stable this long
const unsigned long STUCK_SENSOR_WARN_MS = 30000; // warn if "hand" never leaves

// ---- Network timing ----
const unsigned long WIFI_FIRST_TIMEOUT_MS = 15000;
const unsigned long WIFI_RETRY_MS  = 10000;
const unsigned long MQTT_RETRY_MS  = 5000;
const unsigned long HEARTBEAT_MS   = 10000;

// ---- MQTT broker (public, free, no account) ----
const char* MQTT_HOST = "broker.hivemq.com";
const uint16_t MQTT_PORT = 1883;

// =====================================================
//                  INTERNAL VARIABLES
// =====================================================
enum SystemState {
  STATE_IDLE,
  STATE_DISPENSING,
  STATE_RETURNING,
  STATE_COOLDOWN,
  STATE_WAIT_REMOVAL
};

Servo servo;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
Preferences prefs;

char topicState[96];
char topicOnline[96];
char topicCmd[96];
char clientId[32];

volatile SystemState state = STATE_IDLE;
unsigned long stateStart = 0;

volatile bool handDetected = false;   // debounced sensor state
bool lastRawHand = false;
unsigned long lastRawChange = 0;

const char* servoStatus = "HOME";
unsigned long dispenseCount = 0;
unsigned long lastDispenseEpoch = 0;

volatile bool manualRequest = false;
volatile bool resetRequest = false;
volatile bool publishRequested = false;

bool wifiWasConnected = false;
bool ntpStarted = false;
bool stuckWarned = false;
unsigned long lastWifiAttempt = 0;
unsigned long lastMqttAttempt = 0;
unsigned long lastHeartbeat = 0;

// =====================================================
//                     HELPERS
// =====================================================
bool readSensorRaw() {
  return digitalRead(IR_PIN) == OBJECT_DETECTED_STATE;
}

unsigned long getEpoch() {
  time_t now = time(nullptr);
  return (now > 1700000000) ? (unsigned long)now : 0;  // 0 = time not synced yet
}

const char* sanitizerText() {
  switch (state) {
    case STATE_IDLE:         return "READY";
    case STATE_DISPENSING:
    case STATE_RETURNING:    return "DISPENSING";
    case STATE_COOLDOWN:     return "COOLDOWN";
    case STATE_WAIT_REMOVAL: return "WAIT_REMOVAL";
  }
  return "UNKNOWN";
}

void enterState(int s) {
  state = (SystemState)s;
  stateStart = millis();
  publishRequested = true;
}

void servoMoveTo(int angle) {
  angle = constrain(angle, SERVO_MIN_SAFE, SERVO_MAX_SAFE);
  if (!servo.attached()) {
    servo.attach(SERVO_PIN, SERVO_MIN_US, SERVO_MAX_US);
  }
  servo.write(angle);
}

void servoRelease() {
  if (DETACH_SERVO_WHEN_IDLE && servo.attached()) {
    servo.detach();
  }
}

// =====================================================
//                 NETWORK (runs in its own task)
// =====================================================
void publishStateNow() {
  char buf[256];
  snprintf(buf, sizeof(buf),
           "{\"sensor\":\"%s\",\"sanitizer\":\"%s\",\"servo\":\"%s\","
           "\"count\":%lu,\"last\":%lu,\"rssi\":%d,\"uptime\":%lu}",
           handDetected ? "HAND" : "NO_HAND",
           sanitizerText(),
           servoStatus,
           dispenseCount,
           lastDispenseEpoch,
           (int)WiFi.RSSI(),
           millis() / 1000UL);
  mqtt.publish(topicState, buf, true);   // retained
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char msg[16];
  unsigned int n = (length < sizeof(msg) - 1) ? length : sizeof(msg) - 1;
  memcpy(msg, payload, n);
  msg[n] = '\0';

  if (strcmp(msg, "dispense") == 0) {
    if (state == STATE_IDLE) {
      manualRequest = true;
    } else {
      Serial.println("Manual dispense ignored: system busy.");
    }
  } else if (strcmp(msg, "reset") == 0) {
    resetRequest = true;
  } else {
    Serial.print("Unknown command: ");
    Serial.println(msg);
  }
}

void connectMqtt() {
  Serial.print("Connecting to MQTT broker... ");
  // Last Will: broker publishes "offline" if we vanish
  bool ok = mqtt.connect(clientId, NULL, NULL, topicOnline, 1, true, "offline");
  if (ok) {
    Serial.println("connected.");
    mqtt.publish(topicOnline, "online", true);
    mqtt.subscribe(topicCmd);
    publishRequested = true;
  } else {
    Serial.print("failed, rc=");
    Serial.println(mqtt.state());
  }
}

void onWifiConnected() {
  wifiWasConnected = true;
  Serial.println("WiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  if (!ntpStarted) {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");  // UTC; dashboard converts to local time
    ntpStarted = true;
  }
}

void maintainConnections() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiWasConnected) onWifiConnected();
    if (!mqtt.connected()) {
      if (millis() - lastMqttAttempt >= MQTT_RETRY_MS) {
        lastMqttAttempt = millis();
        connectMqtt();
      }
    } else {
      mqtt.loop();
    }
  } else {
    if (wifiWasConnected) {
      wifiWasConnected = false;
      Serial.println("WiFi connection lost.");
    }
    if (millis() - lastWifiAttempt >= WIFI_RETRY_MS) {
      lastWifiAttempt = millis();
      Serial.println("Retrying WiFi...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  }
}

void networkTask(void* param) {
  for (;;) {
    maintainConnections();
    if (publishRequested && mqtt.connected()) {
      publishRequested = false;
      publishStateNow();
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// =====================================================
//                  DISPENSER LOGIC
// =====================================================
void updateSensor() {
  bool raw = readSensorRaw();
  unsigned long now = millis();
  if (raw != lastRawHand) {
    lastRawHand = raw;
    lastRawChange = now;
  }
  if (raw != handDetected && (now - lastRawChange) >= SENSOR_DEBOUNCE_MS) {
    handDetected = raw;
    Serial.println(handDetected ? "IR Sensor: HAND DETECTED" : "IR Sensor: NO HAND");
    publishRequested = true;
  }
}

void startDispense(const char* source) {
  Serial.print("Dispense trigger: ");
  Serial.println(source);
  Serial.println("Dispensing sanitizer...");
  servoStatus = "DISPENSING";
  servoMoveTo(SERVO_DISPENSE);
  Serial.println("Servo: DISPENSING");
  enterState(STATE_DISPENSING);
}

void runStateMachine() {
  unsigned long now = millis();

  if (resetRequest) {
    resetRequest = false;
    dispenseCount = 0;
    lastDispenseEpoch = 0;
    prefs.putULong("count", 0);
    prefs.putULong("last", 0);
    Serial.println("Counter reset from dashboard.");
    publishRequested = true;
  }

  if (state != STATE_IDLE) manualRequest = false;  // drop stale requests

  switch (state) {
    case STATE_IDLE:
      if (handDetected) {
        startDispense("IR SENSOR");
      } else if (manualRequest) {
        manualRequest = false;
        startDispense("DASHBOARD (manual)");
      }
      break;

    case STATE_DISPENSING:
      if (now - stateStart >= DISPENSE_TIME) {
        servoMoveTo(SERVO_HOME);
        servoStatus = "RETURNING";
        dispenseCount++;
        lastDispenseEpoch = getEpoch();
        prefs.putULong("count", dispenseCount);
        prefs.putULong("last", lastDispenseEpoch);
        Serial.print("Dispensing complete. Total dispensed: ");
        Serial.println(dispenseCount);
        Serial.println("Servo: RETURNING");
        enterState(STATE_RETURNING);
      }
      break;

    case STATE_RETURNING:
      if (now - stateStart >= SERVO_SETTLE_TIME) {
        servoRelease();
        servoStatus = "HOME";
        Serial.println("Servo: HOME");
        enterState(STATE_COOLDOWN);
      }
      break;

    case STATE_COOLDOWN:
      if (now - stateStart >= COOLDOWN_TIME) {
        if (handDetected) {
          Serial.println("Waiting for hand removal...");
          stuckWarned = false;
          enterState(STATE_WAIT_REMOVAL);
        } else {
          Serial.println("System Ready.");
          enterState(STATE_IDLE);
        }
      }
      break;

    case STATE_WAIT_REMOVAL:
      if (!handDetected) {
        Serial.println("Hand removed. System Ready.");
        enterState(STATE_IDLE);
      } else if (!stuckWarned && (now - stateStart >= STUCK_SENSOR_WARN_MS)) {
        stuckWarned = true;
        Serial.println("WARNING: sensor reports a hand for 30s. Check IR sensor / OBJECT_DETECTED_STATE.");
      }
      break;
  }
}

// =====================================================
//                       SETUP / LOOP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("================================");
  Serial.println(" AUTOMATIC SANITIZER IoT");
  Serial.println("================================");
  Serial.println();

  // IR pin: pull the line toward the "no object" level if the wire is loose
  pinMode(IR_PIN, (OBJECT_DETECTED_STATE == LOW) ? INPUT_PULLUP : INPUT_PULLDOWN);

  // Servo
  ESP32PWM::allocateTimer(0);
  servo.setPeriodHertz(50);
  servoMoveTo(SERVO_HOME);
  delay(SERVO_SETTLE_TIME);
  servoRelease();
  servoStatus = "HOME";

  // Saved counter
  prefs.begin("sanitizer", false);
  dispenseCount = prefs.getULong("count", 0);
  lastDispenseEpoch = prefs.getULong("last", 0);

  // MQTT topics and client id
  snprintf(topicState,  sizeof(topicState),  "sanitizer-iot/%s/state",  DEVICE_ID);
  snprintf(topicOnline, sizeof(topicOnline), "sanitizer-iot/%s/online", DEVICE_ID);
  snprintf(topicCmd,    sizeof(topicCmd),    "sanitizer-iot/%s/cmd",    DEVICE_ID);
  snprintf(clientId, sizeof(clientId), "sanitizer-%04X", (unsigned int)(ESP.getEfuseMac() & 0xFFFF));
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(15);
  mqtt.setSocketTimeout(3);

  // Initial sensor reading
  handDetected = readSensorRaw();
  lastRawHand = handDetected;
  lastRawChange = millis();

  // Wi-Fi (give it a limited time; dispenser works offline too)
  Serial.println("Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_FIRST_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    onWifiConnected();
  } else {
    Serial.println("WiFi FAILED (timeout). Running in OFFLINE mode - dispenser still works.");
    Serial.println("Will keep retrying in the background.");
    lastWifiAttempt = millis();
  }

  Serial.println();
  Serial.println("System Ready.");
  Serial.println(handDetected ? "IR Sensor: HAND DETECTED" : "IR Sensor: NO HAND");
  Serial.println("Servo: HOME");
  Serial.print("Total dispensed (saved): ");
  Serial.println(dispenseCount);
  Serial.println();

  if (handDetected) {
    Serial.println("Hand present at startup. Waiting for hand removal...");
    enterState(STATE_WAIT_REMOVAL);
  } else {
    enterState(STATE_IDLE);
  }

  xTaskCreatePinnedToCore(networkTask, "network", 8192, NULL, 1, NULL, 0);
}

void loop() {
  updateSensor();
  runStateMachine();

  if (millis() - lastHeartbeat >= HEARTBEAT_MS) {
    lastHeartbeat = millis();
    publishRequested = true;
  }
  delay(5);
}
