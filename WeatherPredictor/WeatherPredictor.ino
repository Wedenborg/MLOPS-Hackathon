#include <ArduinoBLE.h>
#include <Arduino_HS300x.h>
#include <Arduino_LPS22HB.h>
#include <Arduino_BMI270_BMM150.h>
#include <TensorFlowLite.h>
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "model_settings.h"
#include "weather_model.h"

// =================================================================
// ## Debugging Configuration ##
// =================================================================
const bool DEBUG_MODE = true; // Set to 'true' to run predictions automatically
long lastPredictionTime = 0;
const int PREDICTION_INTERVAL_MS = 10000; // Predict every 10 seconds in debug mode


// =================================================================
// ## Bluetooth Low Energy (BLE) Configuration ##
// =================================================================
// This service UUID is a unique identifier for your weather service.
const char* deviceServiceUuid = "19b10000-e8f2-537e-4f6c-d104768a1812";
// This characteristic receives the command from your main Arduino to run a prediction.
BLEService weatherService(deviceServiceUuid);
BLEByteCharacteristic runPredictionCharacteristic("19b10001-e8f2-537e-4f6c-d104768a1812", BLEWrite);
// This characteristic could be used to send the prediction result back.
BLEFloatCharacteristic predictionResultCharacteristic("19b10002-e8f2-537e-4f6c-d104768a1812", BLERead | BLENotify);


// =================================================================
// ## TensorFlow Lite Micro (TFLM) Globals ##
// =================================================================
namespace {
const tflite::Model* model = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* model_input = nullptr;
TfLiteTensor* model_output = nullptr;

// Create a memory area for the model. The size needed depends on your model.
constexpr int kTensorArenaSize = 10 * 1024;
alignas(16) uint8_t tensor_arena[kTensorArenaSize];
} // namespace


// =================================================================
// ## Sensor & Data Averaging Configuration ##
// =================================================================
const int AVG_DURATION_MS = 5000; // 5 seconds
const int SENSOR_READ_INTERVAL_MS = 100; // Read sensors 10 times per second
const int NUM_READINGS = AVG_DURATION_MS / SENSOR_READ_INTERVAL_MS;

// Circular buffers to store the last 5 seconds of readings
float tempReadings[NUM_READINGS];
float humidityReadings[NUM_READINGS];
float pressureReadings[NUM_READINGS];
float windReadings[NUM_READINGS];
int readingIndex = 0;
long lastReadingTime = 0;

// Variables to hold the final 5-second averages
float avgTemp, avgHumidity, avgPressure, avgWind;

// --- Wind Simulation Parameters (from motionAmplitude.ino) ---
const float GAIN_FACTOR = 0.1;
const float DECAY_FACTOR = 0.99;
float accumulatedEnergy = 0.0;


// =================================================================
// ## Model Input Data ##
// =================================================================
// --- SCALING PARAMETERS ---
// SCALING PARAMETERS (auto-generated)
const float MIN_TEMP = 6.0000;
const float MAX_TEMP = 38.7143;
const float MIN_HUMIDITY = 13.4286;
const float MAX_HUMIDITY = 100.0000;
const float MIN_WIND_SPEED = 0.0000;
const float MAX_WIND_SPEED = 42.2200;
const float MIN_PRESSURE = 991.3750;
const float MAX_PRESSURE = 1023.0000;

// --- HISTORICAL DATA ---
// This is the fixed data for past days your model needs.
// **IMPORTANT**: This must be populated with REAL, SCALED data from your training set.
// The format is {temp, humidity, wind, pressure}.
// The number of days here must match (kHistoryDays) in model_settings.h
// HISTORICAL DATA (auto-generated and scaled)
// const float historicalData[kHistoryDays][kNumFeatures] = {
//   {0.5751, 0.5183, 0.0976, 0.6721},
//   {0.5232, 0.6139, 0.1370, 0.5999},
//   {0.5174, 0.5824, 0.0571, 0.6369},
//   {0.5669, 0.4377, 0.1936, 0.6766},
//   {0.5420, 0.4487, 0.3020, 0.7040},
//   {0.5197, 0.4190, 0.1676, 0.7365},
//   {0.5355, 0.4242, 0.0764, 0.7096},
//   {0.5478, 0.4691, 0.0703, 0.7179},
//   {0.5361, 0.5153, 0.0660, 0.7519},
//   {0.5593, 0.4896, 0.1042, 0.7951},
//   {0.5302, 0.5464, 0.0354, 0.7701},
//   {0.4964, 0.4885, 0.0968, 0.7862},
//   {0.4820, 0.4598, 0.1691, 0.8273},
//   {0.4809, 0.4424, 0.2736, 0.7564},
//   {0.4809, 0.4642, 0.1843, 0.7373},
//   {0.4483, 0.5457, 0.0673, 0.6669},
//   {0.4967, 0.5368, 0.0593, 0.6949},
//   {0.4750, 0.5433, 0.0643, 0.6838},
//   {0.5068, 0.4243, 0.1895, 0.6805},
//   {0.5143, 0.4390, 0.1564, 0.6893},
//   {0.4714, 0.4395, 0.1963, 0.7454},
//   {0.5380, 0.1978, 0.3573, 0.6822},
//   {0.5592, 0.2220, 0.3433, 0.6917},
//   {0.5391, 0.2334, 0.2951, 0.7025},
//   {0.5030, 0.2760, 0.2176, 0.6536},
//   {0.4772, 0.4654, 0.0293, 0.7470},
//   {0.4240, 0.7700, 0.1011, 0.7979},
//   {0.4203, 0.8152, 0.0640, 0.8063},
//   {0.4037, 0.7213, 0.1171, 0.8340},
//   {0.4649, 0.4475, 0.1390, 0.8841},
//   {0.3943, 0.4831, 0.1342, 0.8957},
//   {0.3863, 0.4986, 0.1173, 0.8117},
//   {0.3833, 0.6526, 0.0593, 0.8407},
//   {0.3745, 0.7037, 0.0219, 0.8236},
//   {0.3332, 0.6916, 0.0418, 0.7850},
//   {0.4101, 0.6318, 0.0311, 0.6970},
//   {0.3193, 0.8017, 0.1268, 0.7154},
//   {0.4292, 0.6487, 0.1117, 0.6930},
//   {0.4252, 0.5826, 0.0777, 0.7212},
//   {0.3989, 0.5651, 0.0812, 0.7502},
//   {0.3838, 0.5219, 0.1901, 0.8208},
//   {0.3719, 0.4990, 0.2340, 0.7997},
//   {0.3006, 0.5758, 0.0928, 0.8665},
//   {0.3515, 0.5771, 0.1594, 0.8086},
//   {0.3082, 0.5904, 0.1521, 0.8445},
//   {0.3624, 0.4928, 0.2467, 0.8238},
//   {0.4218, 0.4055, 0.3772, 0.7534},
//   {0.3683, 0.4721, 0.4596, 0.7666},
//   {0.3450, 0.5132, 0.1464, 0.7832},
//   {0.2919, 0.7078, 0.0285, 0.7233},
//   {0.3460, 0.7532, 0.1240, 0.6306},
//   {0.2445, 0.9342, 0.2152, 0.7265},
//   {0.3406, 0.7096, 0.2081, 0.8088},
//   {0.3317, 0.6252, 0.1974, 0.8166},
//   {0.3429, 0.6309, 0.0840, 0.7649},
//   {0.2824, 0.8597, 0.1421, 0.8073},
//   {0.2475, 0.8806, 0.1484, 0.8389},
//   {0.2767, 0.8498, 0.1735, 0.7818},
//   {0.1223, 1.0000, 0.0000, 0.7787},
// };

const float historicalData[kHistoryDays][kNumFeatures] = {
  {0.3082, 0.5904, 0.1521, 0.8445},
  {0.3624, 0.4928, 0.2467, 0.8238},
  {0.4218, 0.4055, 0.3772, 0.7534},
  {0.3683, 0.4721, 0.4596, 0.7666},
  {0.3450, 0.5132, 0.1464, 0.7832},
  {0.2919, 0.7078, 0.0285, 0.7233},
  {0.3460, 0.7532, 0.1240, 0.6306},
  {0.2445, 0.9342, 0.2152, 0.7265},
  {0.3406, 0.7096, 0.2081, 0.8088},
  {0.3317, 0.6252, 0.1974, 0.8166},
  {0.3429, 0.6309, 0.0840, 0.7649},
  {0.2824, 0.8597, 0.1421, 0.8073},
  {0.2475, 0.8806, 0.1484, 0.8389},
  {0.2767, 0.8498, 0.1735, 0.7818},
};


// =================================================================
// ## SETUP ##
// =================================================================
void setup() {
  Serial.begin(9600);
  // while (!Serial); // Keep this commented out for deployment without a computer

  // --- Initialize Sensors ---
  if (!HS300x.begin()) Serial.println("HS3003 (Temp/Humidity) NOT found!");
  if (!BARO.begin()) Serial.println("LPS22HB (Pressure) NOT found!");
  if (!IMU.begin()) Serial.println("BMI270 (IMU) NOT found!");

  // --- Initialize BLE ---
  if (!BLE.begin()) {
    Serial.println("Starting Bluetooth® Low Energy module failed!");
    while (1);
  }
  BLE.setLocalName("Weather Predictor");
  BLE.setAdvertisedService(weatherService);
  weatherService.addCharacteristic(runPredictionCharacteristic);
  weatherService.addCharacteristic(predictionResultCharacteristic);
  BLE.addService(weatherService);
  BLE.advertise();

  // ===============================================================
  // ## Initialize TensorFlow Lite Model (UPDATED SECTION) ##
  // ===============================================================
  model = tflite::GetModel(g_model);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    Serial.println("Model schema version mismatch!");
    while (1); // Halt on error
  }

  // **FIX 1**: Increased template size from 5 to 6 for the new op.
  static tflite::MicroMutableOpResolver<6> micro_op_resolver;

  // Added the required operations for your Conv1D model
  micro_op_resolver.AddConv2D();
  micro_op_resolver.AddMaxPool2D();
  micro_op_resolver.AddReshape();
  micro_op_resolver.AddFullyConnected();
  micro_op_resolver.AddQuantize();
  // **FIX 2**: Added the missing EXPAND_DIMS operation.
  micro_op_resolver.AddExpandDims();


  // Build the interpreter
  static tflite::MicroInterpreter static_interpreter(model, micro_op_resolver, tensor_arena, kTensorArenaSize);
  interpreter = &static_interpreter;

  // Allocate memory for the model's tensors
  if (interpreter->AllocateTensors() != kTfLiteOk) {
    Serial.println("Tensor allocation failed!");
    while (1); // Halt on error
  }

  // Get pointers to the input and output tensors
  model_input = interpreter->input(0);
  model_output = interpreter->output(0);
  // ===============================================================


  Serial.println("Initialization complete. Ready to predict.");
}


// =================================================================
// ## MAIN LOOP ##
// =================================================================
void loop() {
  if (DEBUG_MODE) {
    // --- DEBUG MODE ---
    // Continuously collect and average sensor data
    if (millis() - lastReadingTime > SENSOR_READ_INTERVAL_MS) {
      lastReadingTime = millis();
      collectAndAverageSensors();
    }

    // Periodically run a prediction
    if (millis() - lastPredictionTime > PREDICTION_INTERVAL_MS) {
      lastPredictionTime = millis();
      Serial.println("--- [DEBUG MODE] Automatically running prediction ---");
      runPrediction();
    }

  } else {
    // --- BLE MODE ---
    BLEDevice central = BLE.central();

    if (central) {
      Serial.print("Connected to central: ");
      Serial.println(central.address());

      while (central.connected()) {
        // 1. Continuously collect and average sensor data
        if (millis() - lastReadingTime > SENSOR_READ_INTERVAL_MS) {
          lastReadingTime = millis();
          collectAndAverageSensors();
        }

        // 2. Check if a prediction has been requested via BLE
        if (runPredictionCharacteristic.written()) {
          Serial.println("Prediction requested!");
          runPrediction();
        }
      }
      Serial.println("Disconnected from central");
    }
  }
  delay(100);
}


// =================================================================
// ## Helper Functions ##
// =================================================================

// Generic function to scale a sensor value to the 0.0-1.0 range
float scaleValue(float value, float min_val, float max_val) {
  return (value - min_val) / (max_val - min_val);
}

// Reads all sensors and calculates simulated wind
void readSensors(float &t, float &h, float &p, float &w) {
  t = HS300x.readTemperature();
  h = HS300x.readHumidity();
  p = BARO.readPressure(); // This is in kPa, you might need to convert to hPa (* 10)

  // Calculate wind simulation
  float ax, ay, az;
  if (IMU.accelerationAvailable()) {
    IMU.readAcceleration(ax, ay, az);
    float magnitude = sqrt(sq(ax) + sq(ay) + sq(az));
    float instantaneousEnergy = max(0.0, magnitude - 1.0);
    accumulatedEnergy = (accumulatedEnergy * DECAY_FACTOR) + (instantaneousEnergy * GAIN_FACTOR);
    w = accumulatedEnergy;
  }
}

// Stores latest sensor readings in circular buffers and calculates the 5-sec average
void collectAndAverageSensors() {
  readSensors(tempReadings[readingIndex], humidityReadings[readingIndex],
              pressureReadings[readingIndex], windReadings[readingIndex]);

  readingIndex = (readingIndex + 1) % NUM_READINGS;

  // Calculate averages
  float tempSum = 0, humiditySum = 0, pressureSum = 0, windSum = 0;
  for (int i = 0; i < NUM_READINGS; i++) {
    tempSum += tempReadings[i];
    humiditySum += humidityReadings[i];
    pressureSum += pressureReadings[i];
    windSum += windReadings[i];
  }
  avgTemp = tempSum / NUM_READINGS;
  avgHumidity = humiditySum / NUM_READINGS;
  avgPressure = pressureSum / NUM_READINGS;
  avgWind = windSum / NUM_READINGS;
}

// Prepares data, runs inference, and handles the 7-day forecast result
void runPrediction() {
  // 1. Prepare the model input tensor (This part remains the same)
  // The input shape is (1, kTotalDays, kNumFeatures)
  for (int i = 0; i < kHistoryDays; i++) {
    for (int j = 0; j < kNumFeatures; j++) {
      int index = (i * kNumFeatures) + j;
      model_input->data.f[index] = historicalData[i][j];
    }
  }

  // 2. Add "today's" data (the latest 5-second average), scaled
  int todayIndex = kHistoryDays * kNumFeatures;
  model_input->data.f[todayIndex + 0] = scaleValue(avgTemp, MIN_TEMP, MAX_TEMP);
  model_input->data.f[todayIndex + 1] = scaleValue(avgHumidity, MIN_HUMIDITY, MAX_HUMIDITY);
  model_input->data.f[todayIndex + 2] = scaleValue(avgWind, MIN_WIND_SPEED, MAX_WIND_SPEED);
  model_input->data.f[todayIndex + 3] = scaleValue(avgPressure, MIN_PRESSURE, MAX_PRESSURE);

  // 3. Run inference
  if (interpreter->Invoke() != kTfLiteOk) {
    Serial.println("Invoke failed!");
    return;
  }

  // 4. Get the results for the 7-day forecast
  // **MODIFIED PART**: We now loop to get 7 values from the output tensor.
  Serial.println("--- 7-Day Forecast Prediction ---");
  for (int i = 0; i < 7; i++) {
    // Assuming the model outputs 7 consecutive float values.
    // Check your model's output tensor shape to be sure.
    float prediction = model_output->data.f[i];
    
    Serial.print("Day +");
    Serial.print(i + 1);
    Serial.print(": ");
    Serial.println(prediction);
  }
  Serial.println("---------------------------------");


  // 5. Optionally, send the result back via BLE
  // Note: BLEFloatCharacteristic sends only one float. To send all 7,
  // you would need to either use 7 characteristics or, more efficiently,
  // package the data into a byte array and send it through one characteristic.
  // For now, we are just printing to Serial.
  predictionResultCharacteristic.writeValue(model_output->data.f[0]); // Sending just the first day's prediction
}