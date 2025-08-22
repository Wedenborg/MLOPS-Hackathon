import onnx
from onnx_tf.backend import prepare

# Load the ONNX model
onnx_model = onnx.load("temperature_cnn.onnx")

# Create a TensorFlow backend representation
tf_rep = prepare(onnx_model)

# Export the TensorFlow model to SavedModel format
tf_model_path = "tensorflow_model"
tf_rep.export_saved_model(tf_model_path)

print(f"TensorFlow SavedModel exported successfully to {tf_model_path}")