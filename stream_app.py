import streamlit as st
import time
import pandas as pd
#import serial

# FSM:
# Send nothing while waiting for audio
# Once audio has been received, send gestures when updated
# Read from serial to get the updated prediction values
# Update the graphs

#def list_com_ports():
#    """List all available COM ports."""
#    ports = serial.tools.list_ports.comports()
#    return [port.device for port in ports]


import streamlit as st
from PIL import Image
import requests
from io import BytesIO
import pandas as pd
import numpy as np
import altair as alt

# ---------- Banner ----------
def add_banner():
    url = "https://raw.githubusercontent.com/Wedenborg/MLOPS-Hackathon/main/figures/chatGPTBanner_suggestion.png"
    response = requests.get(url)
    img = Image.open(BytesIO(response.content))
    st.image(img, use_container_width=True)

add_banner()

# ---------- Tabs ----------
tab1, tab2 = st.tabs(["Arduino Predictions", "Model Training & Optimization"])

# --- Tab 1: Arduino Predictions ---
with tab1:
    st.header("Predictions from Arduino")
    st.write("Here you can display real-time predictions from the Arduino.")
    
    # Example: simulate some live data
    times = pd.date_range("2025-08-21 10:00", periods=20, freq="S")
    predictions = np.random.rand(20) * 100
    data = pd.DataFrame({"Time": times, "Prediction": predictions})
    
    st.line_chart(data.rename(columns={"Time": "index"}).set_index("index"))

# --- Tab 2: Model Training & Optimization ---
with tab2:
    
    sub_tab = st.radio("Select Section", ["Architecture", "Training Metrics", "Carbon Footprint"])
    
    if sub_tab == "Architecture":
        st.header("### Model Architecture")
        url = "https://raw.githubusercontent.com/Wedenborg/MLOPS-Hackathon/main/figures/model.png"
        response = requests.get(url)
        img = Image.open(BytesIO(response.content))
        st.image(img, use_container_width=True)

    elif sub_tab == "Training Metrics":
        st.header("Model Training & Optimization")
        st.write("Information about model architecture, hyperparameters, and training results.")
        
        # Example: training loss chart
        loss_df = pd.read_csv("data/loss_history.csv").rename(columns={"loss": "Loss"}).reset_index().rename(columns={"index": "Epoch"})
        st.subheader("Training Loss Over Epochs")
        st.line_chart(loss_df.set_index("Epoch"))

    elif sub_tab == "Carbon Footprint":
        df = pd.read_csv("emissions.csv")  # replace with your actual path
        row = df.iloc[0]

        emissions = row['emissions']            # kg CO2
        energy_consumed = row['energy_consumed']  # kWh
        cpu_power = row['cpu_power']             # Watts
        gpu_power = row['gpu_power']             # Watts
        ram_power = row['ram_power']             # Watts
        cpu_energy = row['cpu_energy']
        gpu_energy = row['gpu_energy']
        ram_energy = row['ram_energy']
        duration = row['duration']               # seconds

        st.header("🌱 Carbon & Energy Usage Summary")

        # Metrics in columns
        col1, col2, col3 = st.columns(3)
        col1.metric("CO₂ Emissions", f"{emissions:.4f} kg")
        col2.metric("Energy Consumed", f"{energy_consumed:.4f} kWh")
        col3.metric("Duration", f"{duration:.1f} s")

        # Power bar chart (Altair)
        power_data = pd.DataFrame({
            "Component": ["CPU", "GPU", "RAM"],
            "Power (W)": [cpu_power, gpu_power, ram_power]
        })
        power_chart = alt.Chart(power_data).mark_bar(color="#2ca02c").encode(
            x='Component',
            y='Power (W)',
            color='Component'
        ).properties(title="🌱 Power Consumption by Component")
        st.altair_chart(power_chart, use_container_width=True)

        # Energy breakdown pie chart (Altair)
        energy_data = pd.DataFrame({
            "Component": ["CPU", "GPU", "RAM"],
            "Energy (kWh)": [cpu_energy, gpu_energy, ram_energy]
        })
        energy_chart = alt.Chart(energy_data).mark_arc().encode(
            theta=alt.Theta(field="Energy (kWh)", type="quantitative"),
            color=alt.Color(field="Component", type="nominal"),
            tooltip=["Component", "Energy (kWh)"]
        ).properties(title="🌱 Energy Breakdown by Component")
        st.altair_chart(energy_chart, use_container_width=True)

        # Model metrics table
        metrics = pd.DataFrame({
            "Metric": ["Accuracy", "Precision", "Recall", "F1-score"],
            "Value": [0.92, 0.89, 0.91, 0.90]
        })
        st.subheader("Model Metrics")
        st.table(metrics)


"""
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


"""