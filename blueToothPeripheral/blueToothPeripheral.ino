/*
  BLE_Peripheral.ino

  This program uses the ArduinoBLE library to set-up an Arduino Nano 33 BLE 
  as a peripheral device and specifies a service and a characteristic. Depending 
  of the value of the specified characteristic, an on-board LED gets on. 

  The circuit:
  - Arduino Nano 33 BLE. 

  This example code is in the public domain.
*/

#include <ArduinoBLE.h>
#include <Arduino_HS300x.h>
      
const char* deviceServiceUuid = "19b10000-e8f2-537e-4f6c-d104768a1812";
const char* deviceServiceCharacteristicUuid = "19b10001-e8f2-537e-4f6c-d104768a1812";


// Collect is a flag for when to sample data
BLEService collect(deviceServiceUuid); 
BLEByteCharacteristic collectCharacteristic(deviceServiceCharacteristicUuid, BLERead | BLEWrite);

// Readings
BLEByteCharacteristic latestPredictions(deviceServiceCharacteristicUuid, BLERead | BLEWrite)


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

    if (!BLE.begin()) {
    Serial.println("- Starting Bluetooth® Low Energy module failed!");
    while (1);
    }

    BLE.setLocalName("Arduino Nano 33 BLE (Peripheral)");
    // Update this when we have a model
    // BLE.setAdvertisedService(gestureService);
    // gestureService.addCharacteristic(gestureCharacteristic);
    // BLE.addService(gestureService);
    gestureCharacteristic.writeValue(-1);
    BLE.advertise();

    Serial.println("Nano 33 BLE (Peripheral Device)");
    Serial.println(" ");
}

void loop() {
  BLEDevice central = BLE.central();
  Serial.println("- Discovering central device...");
  delay(500);

  if (central) {
    Serial.println("* Connected to central device!");
    Serial.print("* Device MAC address: ");
    Serial.println(central.address());
    Serial.println(" ");

    while (central.connected()) {
        // Change this into running the model
    //   if (gestureCharacteristic.written()) {
    //      gesture = gestureCharacteristic.value();
    //      writeGesture(gesture);
    //    }
    }
    
    Serial.println("* Disconnected to central device!");
  }
}

