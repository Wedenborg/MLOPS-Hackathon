import pandas as pd
from sklearn.preprocessing import MinMaxScaler

# Define the columns you want to use
feature_cols = ['meantemp', 'humidity', 'wind_speed', 'meanpressure']
data_dir = 'data/DailyDelhiClimateTrain.csv'

try:
    df = pd.read_csv(data_dir)

    # Make sure all columns exist
    if not all(col in df.columns for col in feature_cols):
        raise ValueError("One or more specified columns are not in the CSV file.")

    # Extract the data for the specified columns
    data = df[feature_cols].values

    # Create and fit the scaler
    scaler = MinMaxScaler(feature_range=(0, 1))
    scaler.fit(data)

    # Get the min and max for each feature
    min_values = scaler.data_min_
    max_values = scaler.data_max_

    print("--- Scaling Parameters ---")
    for i, col in enumerate(feature_cols):
        print(f"Feature: {col}")
        print(f"  Min Value: {min_values[i]}")
        print(f"  Max Value: {max_values[i]}")
        print("-" * 20)

except FileNotFoundError:
    print(f"Error: The file '{data_dir}' was not found.")
    print("Please make sure the file is in the same directory as your script.")
except ValueError as e:
    print(f"Error: {e}")