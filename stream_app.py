import streamlit as st
import time
import pandas as pd

st.write("Welcome to the explorer; your new favourite hike companion app!")

#while True:
dataRec = False
while not dataRec:
    try:
        data = pd.read_csv('readings.txt', sep=',', header=None)
        data.columns = ["Temperature"]
        #st.write(f"Received data: {data}")
        #print(f"Received data: {data}")
        dataRec = True
    except FileNotFoundError:
        st.write("Waiting for data...")
        time.sleep(1000)

# Temperature graph for the next few days
# Humidity
# Windspeed
# Pressure?

st.line_chart(data, y="Temperature", x_label="Days")