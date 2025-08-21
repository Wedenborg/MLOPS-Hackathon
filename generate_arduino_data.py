import pandas as pd

# --- Configuration ---
CSV_FILE_PATH = 'data/DailyDelhiClimateTrain.csv'
HISTORICAL_DAYS = 59
FEATURE_COLS = ['meantemp', 'humidity', 'wind_speed', 'meanpressure']

def generate_arduino_code(csv_path):
    """
    Reads weather data, calculates scaling parameters, and formats historical
    data for use in an Arduino sketch.
    """
    try:
        # 1. Load the dataset
        df = pd.read_csv(csv_path)
        print(f"Successfully loaded '{csv_path}' with {len(df)} rows.")

        # Ensure the required columns exist
        if not all(col in df.columns for col in FEATURE_COLS):
            print(f"Error: CSV must contain the columns: {FEATURE_COLS}")
            return

        # 2. Calculate Min and Max values from the ENTIRE dataset
        min_vals = df[FEATURE_COLS].min()
        max_vals = df[FEATURE_COLS].max()

        # 3. Get the last 59 rows for the historical data block
        historical_df = df.tail(HISTORICAL_DAYS)
        print(f"Using the last {len(historical_df)} rows for historical data.")

        # 4. Generate the C++ code strings
        
        # --- Scaling Parameters C++ Code ---
        cpp_scaling_params = "// SCALING PARAMETERS (auto-generated)\n"
        cpp_scaling_params += f"const float MIN_TEMP = {min_vals['meantemp']:.4f};\n"
        cpp_scaling_params += f"const float MAX_TEMP = {max_vals['meantemp']:.4f};\n"
        cpp_scaling_params += f"const float MIN_HUMIDITY = {min_vals['humidity']:.4f};\n"
        cpp_scaling_params += f"const float MAX_HUMIDITY = {max_vals['humidity']:.4f};\n"
        cpp_scaling_params += f"const float MIN_WIND_SPEED = {min_vals['wind_speed']:.4f};\n"
        cpp_scaling_params += f"const float MAX_WIND_SPEED = {max_vals['wind_speed']:.4f};\n"
        cpp_scaling_params += f"const float MIN_PRESSURE = {min_vals['meanpressure']:.4f};\n"
        cpp_scaling_params += f"const float MAX_PRESSURE = {max_vals['meanpressure']:.4f};\n"

        # --- Historical Data C++ Code ---
        cpp_historical_data = "// HISTORICAL DATA (auto-generated and scaled)\n"
        cpp_historical_data += f"const float historicalData[kHistoryDays][kNumFeatures] = {{\n"
        
        for _, row in historical_df.iterrows():
            # Scale each value in the row
            scaled_temp = (row['meantemp'] - min_vals['meantemp']) / (max_vals['meantemp'] - min_vals['meantemp'])
            scaled_hum = (row['humidity'] - min_vals['humidity']) / (max_vals['humidity'] - min_vals['humidity'])
            scaled_wind = (row['wind_speed'] - min_vals['wind_speed']) / (max_vals['wind_speed'] - min_vals['wind_speed'])
            scaled_press = (row['meanpressure'] - min_vals['meanpressure']) / (max_vals['meanpressure'] - min_vals['meanpressure'])
            
            cpp_historical_data += f"  {{{scaled_temp:.4f}, {scaled_hum:.4f}, {scaled_wind:.4f}, {scaled_press:.4f}}},\n"
        
        cpp_historical_data += "};"

        # 5. Print the final output
        print("\n" + "="*50)
        print("COPY AND PASTE THE FOLLOWING CODE INTO YOUR ARDUINO SKETCH")
        print("="*50 + "\n")
        print("--- 1. Replace the SCALING PARAMETERS section ---")
        print(cpp_scaling_params)
        print("\n" + "--- 2. Replace the HISTORICAL DATA array ---")
        print(cpp_historical_data)
        print("\n" + "="*50)

    except FileNotFoundError:
        print(f"Error: The file '{csv_path}' was not found.")
    except Exception as e:
        print(f"An error occurred: {e}")

# Run the script
if __name__ == "__main__":
    generate_arduino_code(CSV_FILE_PATH)