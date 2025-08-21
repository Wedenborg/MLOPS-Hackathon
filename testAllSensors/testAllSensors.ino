#include <Arduino_BMI270_BMM150.h>
#include <PDM.h>
#include <Arduino_HS300x.h>    // Temperature/Humidity (HS3003)
#include <Arduino_LPS22HB.h>   // Barometer (LPS22HB)

// Audio buffer
short sampleBuffer[256];

void setup() {
  Serial.begin(9600);
  while (!Serial);

  // --- IMU ---
  if (!IMU.begin()) {
    Serial.println("Failed to initialize BMI270/BMM150!");
    while (1);
  }

  // --- HS3003 ---
  if (!HS300x.begin()) {
    Serial.println("HS3003 NOT found!");
  }

  // --- LPS22HB ---
  if (!BARO.begin()) {
    Serial.println("LPS22HB NOT found!");
  }

  Serial.println("MicAmp,Ax,Ay,Az,TempHS,HumHS,TempBaro,Pressure");
}

void loop() {
  // --- Re-initialize microphone ---
  PDM.end();
  if (!PDM.begin(1, 16000)) {
    return; // skip loop if mic not working
  }
  PDM.setGain(80);
  delay(50); // allow samples to accumulate

  // --- Read microphone ---
  int micAmplitude = 0;
  int samplesAvailable = PDM.available();
  if (samplesAvailable) {
    PDM.read(sampleBuffer, samplesAvailable);
    int numSamples = samplesAvailable / 2;
    long sum = 0;
    for (int i = 0; i < numSamples; i++) sum += abs(sampleBuffer[i]);
    if (numSamples > 0) micAmplitude = sum / numSamples;
  }

  // --- Read IMU ---
  float ax=0, ay=0, az=0;
  if (IMU.accelerationAvailable()) IMU.readAcceleration(ax, ay, az);

  // --- Read HS3003 ---
  float tempHS=0, humHS=0;
  if (HS300x.begin()) {
    tempHS = HS300x.readTemperature();
    humHS = HS300x.readHumidity();
  }

  // --- Read LPS22HB ---
  float tempBaro=0, pressure=0;
  if (BARO.begin()) {
    tempBaro = BARO.readTemperature();
    pressure = BARO.readPressure();
  }

  // --- Print all data ---
  Serial.print(micAmplitude); Serial.print(",");
  Serial.print(ax); Serial.print(",");
  Serial.print(ay); Serial.print(",");
  Serial.print(az); Serial.print(",");
  Serial.print(tempHS); Serial.print(",");
  Serial.print(humHS); Serial.print(",");
  Serial.print(tempBaro); Serial.print(",");
  Serial.println(pressure);

  delay(200);
}
