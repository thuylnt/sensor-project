#pragma once
// Placeholder cho model TFLite int8.
// Sau khi train (Edge Impulse hoac Keras+TFLM), export ra mang byte va dat vao day.
//
// Vi du Edge Impulse: download "Arduino library" -> file
//   src/tflite-model/tflite_learn_..._compiled.h
// hoac chuyen ra mang voi xxd -i model_quant.tflite > model_data.h
//
// extern const unsigned char g_model_data[];
// extern const unsigned int g_model_data_len;

static const unsigned char g_model_data[] = {
    // TODO: dan byte model o day
    0x00
};
static const unsigned int g_model_data_len = 1;
