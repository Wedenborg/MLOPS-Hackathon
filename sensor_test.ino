#include <Arduino_BMI270_BMM150.h>
#include <PDM.h>

// Buffer to read samples into, each sample is 16-bits
short sampleBuffer[256];

void setup() {
  Serial.begin(9600);
  while (!Serial);

  if (!IMU.begin()) {
    Serial.println("Failed to initialize BMI270/BMM150!");
    while (1);
  }

  Serial.println("Mic,ax,ay,az");
}

void loop() {
  // --- Brute-Force Re-initialization of PDM ---
  PDM.end(); 
  if (!PDM.begin(1, 16000)) { 
    return;
  }
  
  // FIX #1: Explicitly set the microphone gain to a high value.
  PDM.setGain(80); 
  
  // FIX #2: Give the PDM more time to capture sound.
  delay(50); 

  // --- Now, try to read from both sensors ---
  int micAmplitude = 0;
  float ax = 0.0, ay = 0.0, az = 0.0;

  // Poll the Microphone
  int samplesAvailable = PDM.available();
  if (samplesAvailable) {
    PDM.read(sampleBuffer, samplesAvailable);
    int numSamples = samplesAvailable / 2;
    
    long sum = 0;
    for (int i = 0; i < numSamples; i++) {
      sum += abs(sampleBuffer[i]);
    }
    
    if (numSamples > 0) {
      micAmplitude = sum / numSamples;
    }
  }

  // Poll the IMU
  if (IMU.accelerationAvailable()) {
    IMU.readAcceleration(ax, ay, az);
  }
  
  // --- Print the captured data ---
  Serial.print(micAmplitude);
  Serial.print(",");
  Serial.print(ax);
  Serial.print(",");
  Serial.print(ay);
  Serial.print(",");
  Serial.println(az);
}