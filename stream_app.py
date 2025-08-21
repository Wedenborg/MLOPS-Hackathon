import streamlit as st
import time
import pandas as pd
import serial

# FSM:
# Send nothing while waiting for audio
# Once audio has been received, send gestures when updated
# Read from serial to get the updated prediction values
# Update the graphs

def list_com_ports():
    """List all available COM ports."""
    ports = serial.tools.list_ports.comports()
    return [port.device for port in ports]

def add_banner():
    custom_html = """
    <div class="banner">
    <img src="https://img.freepik.com/premium-photo/wide-banner-with-many-random-square-hexagons-charcoal-dark-black-color_105589-1820.jpg" alt="Banner Image">
    </div>
    <style>
    .banner {
    width: 100%;
    height: 150px;
    overflow: hidden;
    }
    .banner img {
    width: 100%;
    object-fit: cover;
    }
    </style>
    """
    st.components.v1.html(custom_html, height=200)

if __name__ == "__main__":
    dataRec = False
    prevGesture = None

    add_banner()
    st.write("Welcome to the explorer; your new favourite hike companion app!")

    ports = list_com_ports()
    if not ports:
        print("No COM ports found.")

    print("Available COM ports:")
    for i, p in enumerate(ports):
        print(f"[{i}] {p}")

    # Pick the first port (you can change this to select manually)
    port = ports[0]
    baudrate = 9600  # must match your Arduino sketch

    print(f"\nConnecting to {port} at {baudrate} baud...")

    # Gesture updates
    while prevGesture != "UP":
        try:
            with serial.Serial(port, baudrate, timeout=1) as ser:
                while True: # Change this
                    line = ser.readline().decode("utf-8").strip()
                    if line:
                        print(f"> {line}")
                    else:
                        break
                
                prevGesture = line
                if line == "LEFT":
                    # Do left things
                    continue
                elif line == "RIGHT":
                    # Do right things
                    continue
                else:
                    break

        except Exception as e:
            print(f"Error: {e}")

    # Maybe this should instead be reading from serial? 
    while not dataRec:
        try:
            data = pd.read_csv('readings.txt', sep=',', header=None)
            data.columns = ["Temperature"]
            dataRec = True
        except FileNotFoundError:
            st.write("Waiting for data...")
            time.sleep(1000)

    # Temperature graph for the next few days
    # Humidity
    # Windspeed
    # Pressure?

    st.line_chart(data, y="Temperature", x_label="Days")