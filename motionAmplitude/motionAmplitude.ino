/*
  Arduino BMI270 - Leaky Integral Motion Accumulator

  This example calculates an "energy" value from motion that
  accumulates with movement and slowly decays over time when still.
  This "leaky integrator" provides a smoother, more persistent
  representation of motion intensity.

  The circuit:
  - Arduino Nano 33 BLE Sense Rev2
*/

#include "Arduino_BMI270_BMM150.h"

// ===============================================
// ## TUNING PARAMETERS ##
// ===============================================

// GAIN_FACTOR controls how quickly the energy accumulates.
// Higher value = faster rise, more sensitive.
const float GAIN_FACTOR = 0.1; 

// DECAY_FACTOR controls how quickly the energy "leaks" or decays.
// Must be less than 1.0. A value closer to 1.0 (e.g., 0.995) means a slower decay.
// A smaller value (e.g., 0.95) means a very fast decay.
const float DECAY_FACTOR = 0.99;
// ===============================================

// Global variable to hold the accumulated energy value between loops.
float accumulatedEnergy = 0.0;

void setup() {
  Serial.begin(9600);
  while (!Serial); // Wait for serial port to connect

  if (!IMU.begin()) {
    Serial.println("Failed to initialize IMU!");
    while (1); // Halt execution if IMU fails
  }

  // Set up the header for the Serial Plotter/Monitor
  Serial.println("AccumulatedEnergy");
}

void loop() {
  float x, y, z;

  if (IMU.accelerationAvailable()) {
    IMU.readAcceleration(x, y, z);

    // 1. Calculate the instantaneous motion energy (same as before).
    float magnitude = sqrt(sq(x) + sq(y) + sq(z));
    float instantaneousEnergy = max(0.0, magnitude - 1.0);

    // 2. Apply the leaky integrator logic.
    // First, decay the existing value slightly.
    // Then, add the new instantaneous energy (scaled by the gain).
    accumulatedEnergy = (accumulatedEnergy * DECAY_FACTOR) + (instantaneousEnergy * GAIN_FACTOR);

    // 3. Print the accumulated value.
    Serial.println(accumulatedEnergy);
    
    // A small delay to keep the timing of the decay consistent.
    delay(10);
  }
}