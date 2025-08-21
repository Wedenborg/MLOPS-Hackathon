#include <Arduino_HS300x.h>
#include <Arduino_LPS22HB.h>

#define LED_PIN LED_BUILTIN

void setup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);
  while (!Serial);

  Serial.println("Starting Nano 33 BLE Sense Rev2 sensor test...");

  if (!HS300x.begin()) {
    Serial.println("HS3003 sensor not found!");
  } else {
    Serial.println("HS3003 initialized.");
  }

  if (!BARO.begin()) {
    Serial.println("LPS22HB sensor not found!");
  } else {
    Serial.println("LPS22HB initialized.");
  }
}

void loop() {
  // Read HS3003
  float hsTemp = HS300x.readTemperature();
  float hsHum  = HS300x.readHumidity();

  // Read LPS22HB
  float baroPress = BARO.readPressure();
  float baroTemp  = BARO.readTemperature();

  // Print results
  Serial.print("HS3003 -> Temp: ");
  Serial.print(hsTemp, 2);
  Serial.print(" °C, Humidity: ");
  Serial.print(hsHum, 2);
  Serial.println(" %");

  Serial.print("LPS22HB -> Pressure: ");
  Serial.print(baroPress, 2);
  Serial.print(" hPa, Temp: ");
  Serial.print(baroTemp, 2);
  Serial.println(" °C");

  Serial.println("------");

  // Blink LED
  digitalWrite(LED_PIN, HIGH);
  delay(200);
  digitalWrite(LED_PIN, LOW);
  delay(800);
}
