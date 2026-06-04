#ifndef APP_OSCILLOSCOPE_H
#define APP_OSCILLOSCOPE_H

#include <stdint.h>
#include "stm32g4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_SCOPE_MODE_ADC = 0,
    APP_SCOPE_MODE_SOURCE = 1
} app_scope_mode_t;

typedef enum {
    APP_WAVE_SINE = 0,
    APP_WAVE_TRIANGLE = 1,
    APP_WAVE_SQUARE = 2,
    APP_WAVE_SAW = 3
} app_waveform_t;

typedef struct {
    app_scope_mode_t mode;
    app_waveform_t waveform;
    uint32_t source_frequency_hz;
    uint32_t sample_rate_hz;
    uint16_t vpp_mv;
    uint16_t offset_mv;
    uint16_t sample_count;
} app_scope_config_t;

void App_OscilloscopeInit(void);
void App_OscilloscopeFrame(ADC_HandleTypeDef *hadc);
void App_OscilloscopeDemoFrame(void);
void App_OscilloscopeAdcFrame(ADC_HandleTypeDef *hadc);
void App_DisplayDiagnosticFrame(void);
void App_OscilloscopeSetMode(app_scope_mode_t mode);
void App_OscilloscopeSetWaveform(app_waveform_t waveform);
void App_OscilloscopeGetConfig(app_scope_config_t *config);
HAL_StatusTypeDef App_OscilloscopeHandleHostCommand(const char *command, char *response, uint16_t response_size);
HAL_StatusTypeDef App_OscilloscopeFormatFrameLine(char *response, uint16_t response_size, uint16_t max_points);
HAL_StatusTypeDef App_OscilloscopeFormatConfigLine(char *response, uint16_t response_size);

#ifdef __cplusplus
}
#endif

#endif
