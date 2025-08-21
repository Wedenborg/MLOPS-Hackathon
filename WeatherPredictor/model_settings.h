#ifndef MODEL_SETTINGS_H_
#define MODEL_SETTINGS_H_

// =================================================================
// ## Model Input Shape Configuration ##
// =================================================================
// These values MUST match the input shape of the model you trained.

// The number of past days of data your model uses for a single prediction.
constexpr int kHistoryDays = 59; // e.g., if your model looks at the past 59 days

// The number of days being predicted (usually 1, for "today").
constexpr int kFutureDays = 1;

// The total number of days in one input sequence.
constexpr int kTotalDays = kHistoryDays + kFutureDays;

// The number of features for each day (temperature, humidity, wind, pressure).
constexpr int kNumFeatures = 4;

#endif // MODEL_SETTINGS_H_