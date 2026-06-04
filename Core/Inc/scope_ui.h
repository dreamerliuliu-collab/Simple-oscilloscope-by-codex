#ifndef SCOPE_UI_H
#define SCOPE_UI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t top;
    uint16_t bottom;
    uint16_t left;
    uint16_t right;
    uint16_t adc_max;
} scope_ui_config_t;

typedef struct {
    const char *status_left;
    const char *status_right;
    const char *scale_top;
    const char *scale_mid;
    const char *scale_bottom;
    const char *footer_left;
    const char *footer_center;
    const char *footer_right;
    uint16_t waveform_min;
    uint16_t waveform_max;
    uint8_t use_fixed_range;
} scope_ui_overlay_t;

void ScopeUI_Init(const scope_ui_config_t *config);
void ScopeUI_DrawGrid(void);
void ScopeUI_DrawWaveform(const uint16_t *samples, uint16_t sample_count, const scope_ui_overlay_t *overlay);

#ifdef __cplusplus
}
#endif

#endif
