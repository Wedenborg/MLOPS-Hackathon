/* ================================================================
 * HiExplorerMain.ino  —  Voice → Gesture → Action (with Serial sync)
 * Target: Arduino Nano 33 BLE Sense (rev2)
 *
 * Flow:
 *   1) LISTENING: continuously listen for a keyword (Edge Impulse).
 *   2) On keyword → switch to MENU (gesture-based): LEFT/RIGHT toggles Option A/B,
 *      UP selects current option.
 *   3) On select:
 *        - Option A: request data from Ardu2 over BLE (or send dummy data if DEBUG_MODE).
 *        - Option B: notify host over Serial (no BLE), then return to LISTENING.
 *
 * Serial protocol (newline-delimited JSON):
 *   {"event":"state","state":"listening"}
 *   {"event":"keyword","label":"<label>","score":0.93}
 *   {"event":"menu","selection":"A"}
 *   {"event":"select","option":"A"}
 *   {"event":"data","option":"A","payload":{"raw":"..."}}
 *   {"event":"error","where":"ble","msg":"..."}
 *
 * Notes:
 *   - This sketch includes a minimal BLE central request to Ardu2 (peripheral).
 *     Update SERVICE_UUID/CHAR_UUID to match Ardu2.
 *   - For the keyword step, this file exposes a thin wrapper (pollKeywordTriggered)
 *     that you can wire to your existing HiExplorer (Edge Impulse) code.
 *     As a fallback for bench testing, you can also type "kw" on the Serial monitor
 *     to simulate the keyword trigger.
 * ================================================================ */

#include <Arduino.h>
#include <ArduinoBLE.h>
#include <Arduino_APDS9960.h>   // Gesture sensor
#include <PDM.h>

// If you compile together with your Edge Impulse sketch (HiExplorer.ino),
// keep the include below. Otherwise, comment it and use the Serial fallback.
#include <a1_inferencing.h>     // <-- from your Edge Impulse project

/* ---------------- Configuration ---------------- */
#define SERIAL_BAUD        115200
#define DEBUG_MODE         1        // 1 = don't contact Ardu2; send dummy data
#define LED_PIN            LED_BUILTIN

// BLE service/characteristic used by Ardu2 (adjust if your peripheral differs)
static const char* SERVICE_UUID = "19b10000-e8f2-537e-4f6c-d104768a1812";
static const char* CHAR_UUID    = "19b10001-e8f2-537e-4f6c-d104768a1812";

// Keyword detection thresholds (Edge Impulse)
#define KEYWORD_POS_LABEL     ""           // e.g. "keyword"
#define KEYWORD_MIN_SCORE     0.80f        // adjust to taste

// Labels to ignore when auto-picking (common for EI projects)
static const char* IGNORE_LABELS[] = { "noise", "unknown", "_silence_", "background" };
static const size_t IGNORE_LABELS_COUNT =
  sizeof(IGNORE_LABELS) / sizeof(IGNORE_LABELS[0]);

static bool isIgnoredLabel(const char* l) {
  if (!l) return true;
  for (size_t i = 0; i < IGNORE_LABELS_COUNT; ++i) {
    if (String(l) == IGNORE_LABELS[i]) return true;
  }
  return false;
}


// Menu behavior
static const uint32_t MENU_TIMEOUT_MS = 10000;  // return to listening if idle
static const uint16_t GESTURE_DEBOUNCE_MS = 350;

/* ---------------- Types & State ---------------- */
enum AppState : uint8_t { STATE_LISTENING = 0, STATE_MENU, STATE_WAITING_BLE };
enum Option   : uint8_t { OPT_A = 0, OPT_B };

static AppState g_state = STATE_LISTENING;
static Option   g_selection = OPT_A;

static uint32_t g_state_enter_ms = 0;
static uint32_t g_last_gesture_ms = 0;

static BLEDevice g_peripheral;
static BLECharacteristic g_char;

/* ---------------- Utility: tiny JSON helpers ---------------- */
static void sendState(const char* st) {
  Serial.print("{\"event\":\"state\",\"state\":\"");
  Serial.print(st);
  Serial.println("\"}");
}

static void sendKeyword(const char* label, float score) {
  Serial.print("{\"event\":\"keyword\",\"label\":\"");
  Serial.print(label);
  Serial.print("\",\"score\":");
  Serial.print(score, 3);
  Serial.println("}");
}

static void sendMenuSelection() {
  Serial.print("{\"event\":\"menu\",\"selection\":\"");
  Serial.print(g_selection == OPT_A ? "A" : "B");
  Serial.println("\"}");
}

static void sendSelect() {
  Serial.print("{\"event\":\"select\",\"option\":\"");
  Serial.print(g_selection == OPT_A ? "A" : "B");
  Serial.println("\"}");
}

static void sendDataA(const String& payload) {
  Serial.print("{\"event\":\"data\",\"option\":\"A\",\"payload\":{\"raw\":\"");
  // Escape quotes minimally
  for (size_t i = 0; i < payload.length(); ++i) {
    char c = payload[i];
    if (c == '"' || c == '\\') Serial.print('\\');
    if (c >= 32 && c < 127) Serial.print(c);
  }
  Serial.println("\"}}");
}

static void sendError(const char* where, const char* msg) {
  Serial.print("{\"event\":\"error\",\"where\":\"");
  Serial.print(where);
  Serial.print("\",\"msg\":\"");
  Serial.print(msg);
  Serial.println("\"}");
}

/* ================================================================
 *  Keyword detection (Edge Impulse)
 *  - Reuses the standard microphone inferencing pattern from EI examples.
 *  - If you already have HiExplorer.ino working, compile it in the same sketch
 *    folder; the linker will merge them. This wrapper just triggers on the
 *    best match to KEYWORD_LABEL above.
 *  - Fallback: typing "kw" on Serial triggers the keyword (for bench tests).
 * ================================================================ */

// ---------- Edge Impulse audio capture glue (minimal) ----------
typedef struct {
  int16_t *buffer;
  volatile uint32_t buf_count;
  uint32_t n_samples;
  volatile uint8_t buf_ready;
} inference_t;

static inference_t inference;
static bool microphone_inference_start(uint32_t n_samples);
static bool microphone_inference_record();
static void microphone_inference_end();
static int  raw_feature_get_data(size_t offset, size_t length, float *out_ptr);

// PDM callback
static void pdm_data_ready_callback(void) {
  int bytesAvailable = PDM.available();
  if (bytesAvailable <= 0) return;

  static int16_t sampleBuffer[1024];
  int bytesRead = PDM.read(sampleBuffer, bytesAvailable);
  int samplesRead = bytesRead / 2;

  int space = inference.n_samples - inference.buf_count;
  if (samplesRead > space) samplesRead = space;
  memcpy(inference.buffer + inference.buf_count, sampleBuffer, samplesRead * sizeof(int16_t));
  inference.buf_count += samplesRead;
  if (inference.buf_count >= inference.n_samples) {
    inference.buf_ready = 1;
  }
}

static bool microphone_inference_start(uint32_t n_samples) {
  inference.buffer = (int16_t*)malloc(n_samples * sizeof(int16_t));
  if (!inference.buffer) return false;
  inference.buf_count = 0;
  inference.n_samples = n_samples;
  inference.buf_ready = 0;

  PDM.onReceive(pdm_data_ready_callback);
  PDM.setBufferSize(4096);
  if (!PDM.begin(1, EI_CLASSIFIER_FREQUENCY)) return false; // mono @ 16kHz
  PDM.setGain(80);
  delay(50);
  return true;
}

static bool microphone_inference_record() {
  inference.buf_count = 0;
  inference.buf_ready = 0;
  uint32_t start = millis();
  while (!inference.buf_ready) {
    if ((millis() - start) > 2000) return false; // avoid lock
    delay(1);
  }
  return true;
}

static void microphone_inference_end() {
  PDM.end();
  if (inference.buffer) { free(inference.buffer); inference.buffer = nullptr; }
}

static int raw_feature_get_data(size_t offset, size_t length, float *out_ptr) {
  // Clamp if needed, then convert int16 → float
  if (offset + length > inference.n_samples) {
    size_t safe = (offset < inference.n_samples) ? (inference.n_samples - offset) : 0;
    if (safe) numpy::int16_to_float(&inference.buffer[offset], out_ptr, safe);
    for (size_t i = safe; i < length; i++) out_ptr[i] = 0.0f;
    return 0;
  }
  numpy::int16_to_float(&inference.buffer[offset], out_ptr, length);
  return 0;
}



// Capture a window, run classifier, return (triggered,label,score)
static bool pollKeywordTriggered(String &outLabel, float &outScore) {
  // Serial fallback for bench testing: type "kw" + Enter to trigger
  if (Serial.available()) {
    String s = Serial.readStringUntil('\n'); s.trim();
    if (s == "kw") { outLabel = "sim"; outScore = 1.0f; return true; }
  }

  // Capture audio and run EI classifier (same as before)
  if (!microphone_inference_start(EI_CLASSIFIER_RAW_SAMPLE_COUNT)) {
    sendError("voice", "mic start failed");
    return false;
  }
  bool ok = microphone_inference_record();
  microphone_inference_end();
  if (!ok) return false;

  signal_t features_signal;
  features_signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
  features_signal.get_data = &raw_feature_get_data;

  ei_impulse_result_t result = { 0 };
  EI_IMPULSE_ERROR r = run_classifier(&features_signal, &result, false);
  if (r != EI_IMPULSE_OK) {
    sendError("voice", "classifier error");
    return false;
  }

  // 1) If a positive label is specified, use its score directly.
  if (strlen(KEYWORD_POS_LABEL) > 0) {
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
      if (String(result.classification[ix].label) == KEYWORD_POS_LABEL) {
        float v = result.classification[ix].value;
        if (v >= KEYWORD_MIN_SCORE) { outLabel = KEYWORD_POS_LABEL; outScore = v; return true; }
        else { return false; }
      }
    }
    // If specified label not found in the impulse, treat as no trigger.
    sendError("voice", "positive label not found");
    return false;
  }

  // 2) Otherwise, auto-pick the best non-noise label by max probability.
  size_t best_i = SIZE_MAX;
  float best_v = 0.0f;

  for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
    const char* lbl = result.classification[ix].label;
    float v = result.classification[ix].value;
    if (isIgnoredLabel(lbl)) continue;
    if (v > best_v) { best_v = v; best_i = ix; }
  }

  if (best_i != SIZE_MAX && best_v >= KEYWORD_MIN_SCORE) {
    outLabel = result.classification[best_i].label;
    outScore = best_v;
    return true;
  }

  return false;
}

/* ================================================================
 *  Gesture (APDS9960): LEFT / RIGHT / UP
 * ================================================================ */
static bool initGesture() {
  if (!APDS.begin()) return false;
  APDS.setGestureSensitivity(80); // more sensitive → easier to trigger
  return true;
}

// Return -1 none, 0 left, 1 right, 2 up
static int readGestureSimple() {
  if (!APDS.gestureAvailable()) return -1;
  int g = APDS.readGesture();
  switch (g) {
    case GESTURE_LEFT:  return 0;
    case GESTURE_RIGHT: return 1;
    case GESTURE_UP:    return 2;
    default:            return -1;
  }
}

/* ================================================================
 *  BLE request to Ardu2 (central role)
 *  - Scans for peripheral advertising SERVICE_UUID
 *  - Connects, finds CHAR_UUID, writes 'R', reads reply string
 * ================================================================ */
static bool bleEnsureConnected() {
  // Already connected?
  if (BLE.connected() && g_peripheral && g_peripheral.connected()) return true;

  // Fresh scan and connect
  BLE.stopScan();
  BLE.scan();                        // generic scan (more compatible than scanForUuid)

  uint32_t t0 = millis();
  bool connected_ok = false;

  while (millis() - t0 < 7000) {     // scan up to 7 seconds
    BLEDevice dev = BLE.available();
    if (!dev) {
      delay(20);
      continue;
    }

    // Try to connect to this device
    if (!dev.connect()) {
      // Not our target or connect failed; keep scanning
      continue;
    }

    // Connected: try to find our service/characteristic
    BLEService svc = dev.service(SERVICE_UUID);
    if (!svc) {
      dev.disconnect();
      continue; // not our device
    }

    BLECharacteristic ch = svc.characteristic(CHAR_UUID);
    if (!ch) {
      dev.disconnect();
      continue; // service present but characteristic missing
    }

    // Success: remember handles
    g_peripheral = dev;
    g_char = ch;
    connected_ok = true;
    break;
  }

  BLE.stopScan();

  if (!connected_ok) {
    sendError("ble", "no suitable peripheral found");
    return false;
  }

  return true;
}

static String bleRequestData() {
  if (!bleEnsureConnected()) return String();

  // (Optional) property check using bitmask for broader compatibility
  // Properties are a bitmask of BLERead, BLEWrite, BLEWriteWithoutResponse, BLENotify, etc.
  unsigned char props = g_char.properties();
  bool canWrite = (props & BLEWrite) || (props & BLEWriteWithoutResponse);

  // Write a simple request token expected by Ardu2 (adjust if your protocol differs)
  const char req[] = "REQ";
  if (canWrite) {
    if (!g_char.writeValue((const uint8_t*)req, sizeof(req) - 1)) {
      sendError("ble", "write failed");
      return String();
    }
  } else {
    // If the peripheral expects a write but the char is read-only, report and bail.
    sendError("ble", "char not writable");
    return String();
  }

  // Poll for a reply up to 3 seconds
  uint8_t buf[128];
  uint32_t t0 = millis();
  while (millis() - t0 < 3000) {
    int n = g_char.readValue(buf, sizeof(buf));
    if (n > 0) {
      String s;
      s.reserve(n);
      for (int i = 0; i < n; i++) s += (char)buf[i];
      return s;
    }
    delay(50);
  }

  sendError("ble", "timeout");
  return String();
}



/* ================================================================
 *  App logic
 * ================================================================ */
static void enterState(AppState s) {
  g_state = s;
  g_state_enter_ms = millis();
  switch (g_state) {
    case STATE_LISTENING:
      digitalWrite(LED_PIN, LOW);
      sendState("listening");
      break;
    case STATE_MENU:
      digitalWrite(LED_PIN, HIGH);
      sendState("menu");
      sendMenuSelection();
      break;
    case STATE_WAITING_BLE:
      sendState("waiting_ble");
      break;
  }
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(SERIAL_BAUD);
  while (!Serial) { /* wait for USB */ }

  // Gesture
  if (!initGesture()) {
    sendError("init", "APDS9960 failed");
  }

  // BLE
  if (!DEBUG_MODE) {
    if (!BLE.begin()) {
      sendError("init", "BLE.begin failed");
    } else {
      BLE.setLocalName("Ardu1");
    }
  }

  enterState(STATE_LISTENING);
}

void loop() {
  switch (g_state) {
    case STATE_LISTENING: {
      String label; float score = 0;
      if (pollKeywordTriggered(label, score)) {
        sendKeyword(label.c_str(), score);
        enterState(STATE_MENU);
      }
      break;
    }

    case STATE_MENU: {
      // Timeout back to listening
      if (millis() - g_state_enter_ms > MENU_TIMEOUT_MS) {
        enterState(STATE_LISTENING);
        break;
      }

      int g = readGestureSimple();
      if (g >= 0 && (millis() - g_last_gesture_ms) > GESTURE_DEBOUNCE_MS) {
        g_last_gesture_ms = millis();
        if (g == 0 /*left*/ || g == 1 /*right*/) {
          // Toggle between two options
          g_selection = (g_selection == OPT_A) ? OPT_B : OPT_A;
          sendMenuSelection();
        } else if (g == 2 /*up*/) {
          sendSelect();
          if (g_selection == OPT_A) {
            if (DEBUG_MODE) {
              // Send dummy data
              String payload = "{\"temp\":23.4,\"hum\":48.7,\"ts\":" + String(millis()) + "}";
              sendDataA(payload);
              enterState(STATE_LISTENING);
            } else {
              enterState(STATE_WAITING_BLE);
            }
          } else {
            // Option B: just inform host, then back to listening
            Serial.println("{\"event\":\"info\",\"msg\":\"Option B chosen\"}");
            enterState(STATE_LISTENING);
          }
        }
      }
      break;
    }

    case STATE_WAITING_BLE: {
      String data = bleRequestData();
      if (data.length() > 0) {
        sendDataA(data);
      }
      if (BLE.connected()) g_peripheral.disconnect();
      enterState(STATE_LISTENING);
      break;
    }
  }
}
