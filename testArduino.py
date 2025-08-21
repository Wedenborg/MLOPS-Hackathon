import mpremote

from machine import Pin, I2C
import time

# On Nano 33 BLE Sense Rev2:
#   SDA = P0.14
#   SCL = P0.15
i2c = I2C(0, scl=Pin(15), sda=Pin(14))

# Scan for devices
devices = i2c.scan()

if devices:
    print("I2C devices found:", [hex(d) for d in devices])
else:
    print("No I2C devices found")


while True:
    print("Scanning for I2C devices...")
    devices = i2c.scan()
    time.sleep_ms(100)

