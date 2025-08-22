/* Edge Impulse ingestion SDK
 * Copyright (c) 2022 EdgeImpulse Inc.
 * Licensed under the Apache License, Version 2.0
 */

// If your target is limited in memory remove this macro to save ~10K RAM
#define EIDSP_QUANTIZE_FILTERBANK   0

#include <PDM.h>
#include <a1_inferencing.h>

/* -------------------- Tunables: extend + slide the listening window -------------------- */
#define CAPTURE_MS   2000   // total listening span per iteration (e.g., 3 seconds)
#define STRIDE_MS     500   // step between windows (e.g., check every 200 ms)
/* -------------------------------------------------------------------------------------- */

/** Audio buffers, pointers and selectors */
typedef struct {
    int16_t *buffer;
    uint8_t  buf_ready;
    uint32_t buf_count;
    uint32_t n_samples;
} inference_t;

static inference_t inference;
static signed short sampleBuffer[2048];
static bool debug_nn = false; // Set true to see features from the raw signal

// Current sliding-window start inside the big capture buffer
static size_t g_base_offset = 0;

/* Forward decls */
static bool microphone_inference_start(uint32_t n_samples);
static bool microphone_inference_record(void);
static void microphone_inference_end(void);
static int  microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr);
static void pdm_data_ready_inference_callback(void);

/**
 * @brief      Arduino setup function
 */
void setup()
{
    Serial.begin(115200);
    while (!Serial);
    Serial.println("Edge Impulse Sliding-Window Inferencing Demo");

    // summary of inferencing settings (from model_metadata.h)
    ei_printf("Inferencing settings:\n");
    ei_printf("\tInterval: %.2f ms.\n", (float)EI_CLASSIFIER_INTERVAL_MS);
    ei_printf("\tFrame size: %d\n", EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE);
    ei_printf("\tSample length (model window): %d ms.\n", EI_CLASSIFIER_RAW_SAMPLE_COUNT / 16);
    ei_printf("\tNo. of classes: %d\n", (int)(sizeof(ei_classifier_inferencing_categories) / sizeof(ei_classifier_inferencing_categories[0])));

    // Allocate a larger recording buffer for a multi-second capture
    const uint32_t capture_samples = (uint32_t)((EI_CLASSIFIER_FREQUENCY * CAPTURE_MS) / 1000);
    if (!microphone_inference_start(capture_samples)) {
        ei_printf("ERR: Could not allocate audio buffer (size %lu). Reduce CAPTURE_MS.\r\n",
                  (unsigned long)capture_samples);
        return;
    }

    ei_printf("Capture window: %lu ms (%lu samples @ %lu Hz)\n",
              (unsigned long)CAPTURE_MS,
              (unsigned long)capture_samples,
              (unsigned long)EI_CLASSIFIER_FREQUENCY);
}

/**
 * @brief      Arduino main function. Runs the inferencing loop.
 */
void loop()
{
    ei_printf("\nStarting capture in 2 seconds...\n");
    delay(2000);

    ei_printf("Recording...\n");
    if (!microphone_inference_record()) {
        ei_printf("ERR: Failed to record audio...\n");
        return;
    }
    ei_printf("Recording done.\n");

    // We will slide the model's fixed window through the larger capture buffer
    signal_t signal;
    signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT; // model window size stays fixed
    signal.get_data     = &microphone_audio_signal_get_data;

    const uint32_t stride_samples = (uint32_t)((EI_CLASSIFIER_FREQUENCY * STRIDE_MS) / 1000);
    const uint32_t max_start =
        (inference.n_samples >= EI_CLASSIFIER_RAW_SAMPLE_COUNT)
            ? (inference.n_samples - EI_CLASSIFIER_RAW_SAMPLE_COUNT)
            : 0;

    float best_score = -1.0f;
    const char* best_label = "(none)";
    uint32_t best_start_ms = 0;

    for (uint32_t start = 0; start <= max_start; start += (stride_samples > 0 ? stride_samples : 1)) {
        g_base_offset = start;

        ei_impulse_result_t result = { 0 };
        EI_IMPULSE_ERROR r = run_classifier(&signal, &result, debug_nn);
        if (r != EI_IMPULSE_OK) {
            ei_printf("ERR: Failed to run classifier (%d)\n", r);
            break;
        }

        const uint32_t start_ms = (uint32_t)((start * 1000UL) / EI_CLASSIFIER_FREQUENCY);
        ei_printf("\nWindow @ %lu ms  (DSP: %d ms, Cls: %d ms",
                  (unsigned long)start_ms, result.timing.dsp, result.timing.classification);
#if EI_CLASSIFIER_HAS_ANOMALY == 1
        ei_printf(", Anom: %d ms", result.timing.anomaly);
#endif
        ei_printf("):\n");

        for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
            float v = result.classification[ix].value;
            ei_printf("  %s: %.5f\n", result.classification[ix].label, v);
            if (v > best_score) {
                best_score = v;
                best_label = result.classification[ix].label;
                best_start_ms = start_ms;
            }
        }
#if EI_CLASSIFIER_HAS_ANOMALY == 1
        ei_printf("  anomaly: %.3f\n", result.anomaly);
#endif
    }

    ei_printf("\nBest over ~%lu ms capture: %s (%.5f) around %lu ms into buffer.\n",
              (unsigned long)CAPTURE_MS, best_label, best_score, (unsigned long)best_start_ms);

    // Optional pause before next capture
    delay(500);
}

/**
 * @brief      PDM buffer full callback
 *             Get data and fill our large capture buffer
 */
static void pdm_data_ready_inference_callback(void)
{
    int bytesAvailable = PDM.available();
    int bytesRead = PDM.read((char *)&sampleBuffer[0], bytesAvailable);

    if (inference.buf_ready == 0) {
        // bytesRead is in bytes; >>1 converts to int16 samples
        for (int i = 0; i < (bytesRead >> 1); i++) {
            inference.buffer[inference.buf_count++] = sampleBuffer[i];

            if (inference.buf_count >= inference.n_samples) {
                inference.buf_count = 0;
                inference.buf_ready = 1;
                break;
            }
        }
    }
}

/**
 * @brief      Init inferencing struct and setup/start PDM
 *
 * @param[in]  n_samples  Number of int16 samples to capture
 */
static bool microphone_inference_start(uint32_t n_samples)
{
    inference.buffer = (int16_t *)malloc(n_samples * sizeof(int16_t));
    if (inference.buffer == NULL) {
        return false;
    }

    inference.buf_count  = 0;
    inference.n_samples  = n_samples;
    inference.buf_ready  = 0;

    // configure the data receive callback
    PDM.onReceive(&pdm_data_ready_inference_callback);

    // PDM internal DMA buffer (bytes). 4096 works well with 16 kHz mono.
    PDM.setBufferSize(4096);

    // initialize PDM:
    // - one channel (mono mode)
    // - sample rate from model metadata (typically 16 kHz)
    if (!PDM.begin(1, EI_CLASSIFIER_FREQUENCY)) {
        ei_printf("Failed to start PDM!");
        microphone_inference_end();
        return false;
    }

    // set the gain (0..127), defaults to ~20; 127 is very sensitive
    PDM.setGain(127);

    return true;
}

/**
 * @brief      Block until our big capture buffer is full
 *
 * @return     True when finished
 */
static bool microphone_inference_record(void)
{
    inference.buf_ready = 0;
    inference.buf_count = 0;

    while (inference.buf_ready == 0) {
        delay(10);
    }
    return true;
}

/**
 * @brief  Provide samples to the classifier from the current sliding window
 *         (the model reads [0, EI_CLASSIFIER_RAW_SAMPLE_COUNT) which we offset by g_base_offset)
 */
static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr)
{
    // Safety check (optional in release builds)
    size_t abs_start = g_base_offset + offset;
    if (abs_start + length > inference.n_samples) {
        // Clamp to available data to avoid OOB in unusual cases
        size_t safe_len = (abs_start < inference.n_samples) ? (inference.n_samples - abs_start) : 0;
        if (safe_len > 0) {
            numpy::int16_to_float(&inference.buffer[abs_start], out_ptr, safe_len);
        }
        if (safe_len < length) {
            // zero-pad remainder
            for (size_t i = safe_len; i < length; i++) out_ptr[i] = 0.0f;
        }
        return 0;
    }

    numpy::int16_to_float(&inference.buffer[abs_start], out_ptr, length);
    return 0;
}

/**
 * @brief      Stop PDM and release buffers
 */
static void microphone_inference_end(void)
{
    PDM.end();
    if (inference.buffer) {
        free(inference.buffer);
        inference.buffer = NULL;
    }
}

#if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_MICROPHONE
#error "Invalid model for current sensor."
#endif
