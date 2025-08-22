import streamlit as st
import time
import pandas as pd
import streamlit as st
from PIL import Image
import requests
from io import BytesIO
import pandas as pd
import numpy as np
import altair as alt
import serial
import threading 
import queue
import streamlit as st
from streamlit_extras.colored_header import colored_header
from streamlit_extras.stylable_container import stylable_container


# FSM:
# Send nothing while waiting for audio
# Once audio has been received, send gestures when updated
# Read from serial to get the updated prediction values
# Update the graphs

def list_com_ports():
    """List all available COM ports."""
    ports = serial.tools.list_ports.comports()
    return [port.device for port in ports]


# ---------- Banner ----------
def add_banner():
    url = "https://raw.githubusercontent.com/Wedenborg/MLOPS-Hackathon/main/figures/chatGPTBanner_suggestion.png"
    response = requests.get(url)
    img = Image.open(BytesIO(response.content))
    st.image(img, use_container_width=True)


def showModelArchitecture():
    st.header("Model Architecture")
    url = "https://raw.githubusercontent.com/Wedenborg/MLOPS-Hackathon/main/figures/model.png"
    response = requests.get(url)
    img = Image.open(BytesIO(response.content))
    st.image(img, use_container_width=True)

    st.subheader("Key Learnings")
    st.write("We tried first with a LSTM model, but it could not be compressed")

def showModelTrainingMetrics():
    st.header("Model Training & Optimization")
    st.write("Information about model architecture, hyperparameters, and training results.")
    
    # Example: training loss chart
    loss_df = pd.read_csv("data/loss_history.csv").rename(columns={"loss": "Loss"}).reset_index().rename(columns={"index": "Epoch"})
    st.subheader("Training Loss Over Epochs")
    st.line_chart(loss_df.set_index("Epoch"))

    # ---------- Metrics ----------
    metrics = pd.DataFrame({
        "Metric": ["Model size before compression [KB]", "Model size after compression [KB]"],
        "Value": [400, 60],
        "Emoji": ["💾", "🗜️"]
    })
    st.subheader("Model Sizes Overview")

    # Maximum value for relative bar
    max_val = metrics["Value"].max()

    # Create columns for each metric
    cols = st.columns(len(metrics))
    for col, (_, row) in zip(cols, metrics.iterrows()):
        # Display emoji + value
        col.markdown(f"### {row['Emoji']} {row['Value']} KB")
        # Display description
        col.caption(row['Metric'])
        # Show relative compression as a horizontal bar
        col.progress(row['Value'] / max_val)

def showCarbonFootprint():
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







# --- Session State for Navigation ---
if "page" not in st.session_state:
    st.session_state.page = "menu"

def go_to(page_name: str):
    st.session_state.page = page_name

# --- Menu Page ---
def menu_page():
    st.title("🎮 Gesture-Controlled App")
    st.markdown("Use your Arduino Nano 33 BLE Sense to navigate using gestures — or try the buttons below to test manually.")

    colored_header(
        label="Gesture Options",
        description="Each gesture triggers a different function in the app.",
        color_name="blue-70"
    )

    col1, col2, col3 = st.columns(3)

    with col1:
        with stylable_container(
            key="card1",
            css_styles="""
                {
                    background-color: #eaf4ff;
                    border-radius: 20px;
                    padding: 20px;
                    box-shadow: 0px 4px 8px rgba(0,0,0,0.1);
                    text-align: center;
                }
            """
        ):
            st.markdown("### ⬆️ Up Gesture")
            st.markdown("**Execution Mode**")
            st.write("Run a new task when you move your hand **upwards**.")
            if st.button("▶️ Execute", key="exec"):
                go_to("execution")

    with col2:
        with stylable_container(
            key="card2",
            css_styles="""
                {
                    background-color: #fff7e6;
                    border-radius: 20px;
                    padding: 20px;
                    box-shadow: 0px 4px 8px rgba(0,0,0,0.1);
                    text-align: center;
                }
            """
        ):
            st.markdown("### ⬅️ Left Gesture")
            st.markdown("**Prediction Mode**")
            st.write("Trigger the model to **predict on a new sample**.")
            if st.button("🔮 Predict", key="predict"):
                go_to("prediction")

    with col3:
        with stylable_container(
            key="card3",
            css_styles="""
                {
                    background-color: #e6fff2;
                    border-radius: 20px;
                    padding: 20px;
                    box-shadow: 0px 4px 8px rgba(0,0,0,0.1);
                    text-align: center;
                }
            """
        ):
            st.markdown("### ➡️ Right Gesture")
            st.markdown("**Training Metrics**")
            st.write("Display **training results & model performance**.")
            if st.button("📊 Show Metrics", key="metrics"):
                go_to("metrics")

# --- Execution Page ---
def execution_page():
    st.header("⬆️ Execution Mode")
    st.write("Here you can execute a task based on your gesture.")
    if st.button("⬅️ Back to Menu", key="back_exec"):
        go_to("menu")

# --- Prediction Page ---
def prediction_page():
    st.header("🔮 Prediction Mode")
    st.write("This page will handle predictions on new samples.")
    if st.button("⬅️ Back to Menu", key="back_pred"):
        go_to("menu")
        st.header("Predictions from Arduino")
        st.write("Here you can display real-time predictions from the Arduino.")
        
        # Example: simulate some live data
        times = pd.date_range("2025-08-21 10:00", periods=20, freq="S")
        predictions = np.random.rand(20) * 100
        data = pd.DataFrame({"Time": times, "Prediction": predictions})
        
        st.line_chart(data.rename(columns={"Time": "index"}).set_index("index"))


    

# --- Metrics Page ---
def metrics_page():
    st.title("📊 Training Metrics")
    st.write("Accuracy: 92% | Loss: 0.18")
    if st.button("⬅️ Back to Menu", key="back_metrics"):
        go_to("menu")

    showModelTrainingMetrics()

    showCarbonFootprint()  

    showModelArchitecture()




        

if __name__ == "__main__":
    dataRec = False
    prevGesture = None

    add_banner()
    st.write("Welcome to the explorer; your new favourite hike companion app!")

    st.set_page_config(page_title="Gesture Menu", page_icon="🤖", layout="wide")

    # --- Router ---
    if st.session_state.page == "menu":
        menu_page()
    elif st.session_state.page == "execution":
        execution_page()
    elif st.session_state.page == "prediction":
        prediction_page()
    elif st.session_state.page == "metrics":
        metrics_page()


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