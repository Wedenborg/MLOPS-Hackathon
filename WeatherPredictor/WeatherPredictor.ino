/*
  WeatherPredictor.ino
  - Runs an int8 TFLite Micro model on Arduino Nano 33 BLE Sense Rev2
  - Keeps BLE result publishing (first day's prediction)
  - Robust TFLite Micro setup, quantization-aware I/O, and arena sizing info
  - Autoregressive N-day forecasting with a 60x4 sliding window
*/

#include <Arduino.h>
#include <ArduinoBLE.h>

// Onboard sensors (optional)
#include <Arduino_HS300x.h>    // Temperature/Humidity
#include <Arduino_LPS22HB.h>   // Barometric Pressure
#include <Arduino_BMI270_BMM150.h> // IMU (not used, included for compatibility)

// TFLite Micro
#include <TensorFlowLite.h>
#include "tensorflow/lite/micro/all_ops_resolver.h"          // start broad; trim later if needed
//#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"
// DO NOT include "tensorflow/lite/version.h" (not present in some Arduino builds)

#include "model_settings.h"   // kHistoryDays, kFutureDays, kTotalDays, kNumFeatures
#include "weather_model.h"    // g_model[], g_model_len

// ============================================================================
// Configuration
// ============================================================================
static const bool   kUseOnboardSensors   = false;  // true = read HS300x + LPS22HB; false = feed via BLE/demo
static const size_t kPredictDays         = 7;      // autoregressive forecast horizon
static const uint32_t kSampleMs          = 1000;   // sensor sampling interval
static const bool   kVerbose             = true;   // extra Serial prints

// Start generous. After seeing "Arena used bytes:", trim down.
constexpr int kTensorArenaSize = 60 * 1024;
alignas(16) static uint8_t tensor_arena[kTensorArenaSize];

// BLE
BLEService weatherService("19B10000-E8F2-537E-4F6C-D104768A1214");
// RX: client can send one feature vector (4 floats) per write
BLECharacteristic inputFeatureCharacteristic(
  "19B10001-E8F2-537E-4F6C-D104768A1214", BLEWrite, 4 * sizeof(float), true);
// TX: publish first day's prediction (float)
BLEFloatCharacteristic predictionResultCharacteristic(
  "19B10002-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify);

// TFLite Micro globals
const tflite::Model*      model       = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;
// Prefer AllOpsResolver at first. Later, trim to MicroMutableOpResolver<> to save flash.
tflite::AllOpsResolver    resolver;

TfLiteTensor* input_tensor  = nullptr;
TfLiteTensor* output_tensor = nullptr;

// Sliding window buffer: [kTotalDays][kNumFeatures]
static float window_buf[kTotalDays][kNumFeatures];

// Timing
static uint32_t lastSampleMs = 0;

// ============================================================================
// Debug helpers
// ============================================================================
static void printTensorInfo(const char* tag, const TfLiteTensor* t) {
  if (!t) return;
  Serial.print(tag); Serial.print(" - type: ");
  switch (t->type) {
    case kTfLiteInt8:    Serial.print("int8"); break;
    case kTfLiteFloat32: Serial.print("float32"); break;
    default:             Serial.print((int)t->type); break;
  }
  Serial.print(", dims: [");
  for (int i = 0; i < t->dims->size; ++i) {
    Serial.print(t->dims->data[i]);
    if (i + 1 < t->dims->size) Serial.print(",");
  }
  Serial.print("], bytes: "); Serial.println(t->bytes);

  Serial.print("  quant scale: "); Serial.print(t->params.scale, 10);
  Serial.print(", zero_point: "); Serial.println(t->params.zero_point);
}

// ============================================================================
// Quantization helpers
// ============================================================================
static inline int8_t quantizeFloatToInt8(float x, float scale, int zero_point) {
  const float q = x / scale + (float)zero_point;
  int32_t r = (int32_t)roundf(q);
  if (r < -128) r = -128;
  if (r >  127) r =  127;
  return (int8_t)r;
}

static inline float dequantizeInt8ToFloat(int8_t q, float scale, int zero_point) {
  return ((float)q - (float)zero_point) * scale;
}

// ============================================================================
// Model I/O
// ============================================================================
static bool fillModelInputFromWindow() {
  if (!input_tensor) return false;

  const int timeSteps = input_tensor->dims->data[1]; // expect 60
  const int nFeatures = input_tensor->dims->data[2]; // expect 4
  if (timeSteps != kTotalDays || nFeatures != kNumFeatures) {
    Serial.println(F("[ERR] Model input shape mismatch vs model_settings.h"));
    Serial.print(F(" Model expects [1,")); Serial.print(timeSteps);
    Serial.print(","); Serial.print(nFeatures); Serial.println("]");
    Serial.print(F(" model_settings say [1,")); Serial.print(kTotalDays);
    Serial.print(","); Serial.print(kNumFeatures); Serial.println("]");
    return false;
  }

  if (input_tensor->type == kTfLiteInt8) {
    const float scale = input_tensor->params.scale;
    const int   zp    = input_tensor->params.zero_point;
    int8_t* dst = input_tensor->data.int8;
    for (int t = 0; t < timeSteps; ++t) {
      for (int f = 0; f < nFeatures; ++f) {
        *dst++ = quantizeFloatToInt8(window_buf[t][f], scale, zp);
      }
    }
    return true;
  } else if (input_tensor->type == kTfLiteFloat32) {
    float* dst = input_tensor->data.f;
    for (int t = 0; t < timeSteps; ++t)
      for (int f = 0; f < nFeatures; ++f)
        *dst++ = window_buf[t][f];
    return true;
  } else {
    Serial.println(F("[ERR] Unsupported input tensor type."));
    return false;
  }
}

static void readModelOutput(float* out, int max_len) {
  const int out_elems = (output_tensor->type == kTfLiteInt8)
                          ? output_tensor->bytes
                          : output_tensor->bytes / sizeof(float);
  const int n = min(out_elems, max_len);

  if (output_tensor->type == kTfLiteInt8) {
    const float scale = output_tensor->params.scale;
    const int   zp    = output_tensor->params.zero_point;
    for (int i = 0; i < n; ++i)
      out[i] = dequantizeInt8ToFloat(output_tensor->data.int8[i], scale, zp);
  } else if (output_tensor->type == kTfLiteFloat32) {
    for (int i = 0; i < n; ++i) out[i] = output_tensor->data.f[i];
  } else {
    for (int i = 0; i < n; ++i) out[i] = 0.0f;
  }
}

// ============================================================================
// Sliding window
// ============================================================================
static void initWindowZeros() {
  for (int t = 0; t < kTotalDays; ++t)
    for (int f = 0; f < kNumFeatures; ++f)
      window_buf[t][f] = 0.0f;
}

static void pushFeatureVector(const float* feat) {
  for (int t = 0; t < kTotalDays - 1; ++t)
    for (int f = 0; f < kNumFeatures; ++f)
      window_buf[t][f] = window_buf[t + 1][f];
  for (int f = 0; f < kNumFeatures; ++f)
    window_buf[kTotalDays - 1][f] = feat[f];
}

// ============================================================================
// Optional sensors (apply SAME scaling as training if you used MinMaxScaler)
// ============================================================================
static bool readOnboardFeatures(float* feat /* 4 values: temp, hum, wind, press */) {
  // Make sure you called HS300x.begin() and BARO.begin() in setup
  float tC = HS300x.readTemperature();
  float hum = HS300x.readHumidity();
  float press_hPa = BARO.readPressure();
  feat[0] = tC;
  feat[1] = hum;
  feat[2] = 0.0f;       // no wind sensor on board
  feat[3] = press_hPa;
  return true;
}

// ============================================================================
// Inference
// ============================================================================
static bool runOneStep(float* out4 /* size 4 */) {
  if (!fillModelInputFromWindow()) return false;
  if (interpreter->Invoke() != kTfLiteOk) {
    Serial.println(F("[ERR] Invoke() failed"));
    return false;
  }
  readModelOutput(out4, 4);
  return true;
}

static void runForecastAndPublish(size_t days) {
  float next4[4];
  float tmp[kTotalDays][kNumFeatures];
  memcpy(tmp, window_buf, sizeof(window_buf));

  Serial.println(F("---------------------------------"));
  Serial.println(F("Forecast:"));
  for (size_t i = 0; i < days; ++i) {
    memcpy(window_buf, tmp, sizeof(window_buf));

    if (!runOneStep(next4)) {
      Serial.println(F("[ERR] Forecast step failed"));
      break;
    }

    Serial.print(F("Day +")); Serial.print(i + 1); Serial.print(F(": "));
    for (int f = 0; f < 4; ++f) {
      Serial.print(next4[f], 6);
      if (f < 3) Serial.print(", ");
    }
    Serial.println();

    if (i == 0) {
      // Preserve your original behavior: send first output as float
      predictionResultCharacteristic.writeValue(next4[0]);
    }

    // Autoregressive input
    for (int t = 0; t < kTotalDays - 1; ++t)
      for (int f = 0; f < kNumFeatures; ++f)
        tmp[t][f] = tmp[t + 1][f];
    for (int f = 0; f < kNumFeatures; ++f)
      tmp[kTotalDays - 1][f] = next4[f];
  }
  Serial.println(F("---------------------------------"));
}

// ============================================================================
// BLE
// ============================================================================
static void setupBLE() {
  if (!BLE.begin()) {
    Serial.println(F("[ERR] BLE.begin() failed"));
    while (1) delay(10);
  }
  BLE.setLocalName("WeatherPredictor");
  BLE.setDeviceName("WeatherPredictor");
  BLE.setAdvertisedService(weatherService);

  weatherService.addCharacteristic(inputFeatureCharacteristic);
  weatherService.addCharacteristic(predictionResultCharacteristic);
  BLE.addService(weatherService);

  predictionResultCharacteristic.writeValue(0.0f);
  BLE.advertise();
  Serial.println(F("BLE advertising started"));
}

static void pollBLEInput() {
  BLEDevice central = BLE.central();
  if (!central) return;

  if (inputFeatureCharacteristic.written()) {
    if (inputFeatureCharacteristic.valueLength() == (4 * sizeof(float))) {
      float feat[4];
      inputFeatureCharacteristic.readValue((void*)feat, 4 * sizeof(float));
      pushFeatureVector(feat);
      if (kVerbose) {
        Serial.print(F("RX feature: "));
        Serial.print(feat[0]); Serial.print(", ");
        Serial.print(feat[1]); Serial.print(", ");
        Serial.print(feat[2]); Serial.print(", ");
        Serial.println(feat[3]);
      }
      runForecastAndPublish(kPredictDays);
    } else {
      Serial.print(F("[WARN] Unexpected RX length: "));
      Serial.println(inputFeatureCharacteristic.valueLength());
    }
  }
}

// ============================================================================
// Setup & Loop
// ============================================================================
void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  Serial.println(F("\nWeatherPredictor starting..."));

  // Sensors
  if (kUseOnboardSensors) {
    if (!HS300x.begin()) Serial.println(F("[WARN] HS300x not found"));
    if (!BARO.begin())   Serial.println(F("[WARN] LPS22HB not found"));
  }

  // BLE
  setupBLE();

  // Load model
  model = tflite::GetModel(g_model);

  // Only check schema version if macro is available in this build
  #ifdef TFLITE_SCHEMA_VERSION
    if (model->version() != TFLITE_SCHEMA_VERSION) {
      Serial.print(F("[ERR] Schema mismatch. Expected "));
      Serial.print(TFLITE_SCHEMA_VERSION);
      Serial.print(F(", got "));
      Serial.println(model->version());
      while (1) delay(10);
    }
  #else
    Serial.print(F("Model schema version: "));
    Serial.println(model->version());
  #endif

  // Interpreter
  static tflite::MicroInterpreter static_interpreter(
      model, resolver, tensor_arena, kTensorArenaSize);
  interpreter = &static_interpreter;

  // Allocate tensors
  if (interpreter->AllocateTensors() != kTfLiteOk) {
    Serial.println(F("[ERR] AllocateTensors() failed"));
    while (1) delay(10);
  }

  input_tensor  = interpreter->input(0);
  output_tensor = interpreter->output(0);

  if (kVerbose) {
    printTensorInfo("INPUT", input_tensor);
    printTensorInfo("OUTPUT", output_tensor);
    Serial.print(F("Arena used bytes: "));
    Serial.println(interpreter->arena_used_bytes());
  }

  initWindowZeros();

  // Optionally prime the window from sensors so first inference is meaningful
  if (kUseOnboardSensors) {
    float feat[4];
    for (int i = 0; i < kTotalDays; ++i) {
      if (readOnboardFeatures(feat)) pushFeatureVector(feat);
      delay(10);
    }
  }

  Serial.println(F("Setup complete."));
}

void loop() {
  BLE.poll();
  pollBLEInput();

  if (kUseOnboardSensors) {
    const uint32_t now = millis();
    if (now - lastSampleMs >= kSampleMs) {
      lastSampleMs = now;
      float feat[4];
      if (readOnboardFeatures(feat)) {
        pushFeatureVector(feat);
        if (kVerbose) {
          Serial.print(F("Sensor feat: "));
          Serial.print(feat[0]); Serial.print(", ");
          Serial.print(feat[1]); Serial.print(", ");
          Serial.print(feat[2]); Serial.print(", ");
          Serial.println(feat[3]);
        }
        runForecastAndPublish(kPredictDays);
      }
    }
  }

  delay(5);
}
