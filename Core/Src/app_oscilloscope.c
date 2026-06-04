#include "app_oscilloscope.h"
#include "scope_ui.h"
#include "tft_softspi.h"
#include "stm32g4xx_hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCOPE_SAMPLE_COUNT        640U
#define HOST_FRAME_POINT_LIMIT    160U
#define DEFAULT_SOURCE_FREQ_HZ    250UL
#define DEFAULT_SAMPLE_RATE_HZ    20000UL
#define DEFAULT_VPP_MV            1800U
#define DEFAULT_OFFSET_MV         1650U
#define SIGNAL_FULL_SCALE_MV      3300U
#define SIGNAL_ADC_MAX            4095U
/* 1.8-inch ST7735 panels often need a small visible-area offset and bottom guard band. */
#define TFT_VIEW_WIDTH            128U
#define TFT_VIEW_HEIGHT           160U
#define TFT_X_OFFSET              0U
#define TFT_Y_OFFSET              0U
#define TFT_MADCTL_NATIVE         0xC8U
#define SCOPE_UI_LEFT             3U
#define SCOPE_UI_RIGHT            132U
#define SCOPE_UI_TOP              16U
#define SCOPE_UI_BOTTOM           111U

typedef struct {
    app_scope_config_t config;
    uint16_t latest_samples[SCOPE_SAMPLE_COUNT];
    uint32_t source_phase_accum;
} app_scope_state_t;

static app_scope_state_t scope_app;

static const uint16_t sine_quarter[65] = {
       0,  100,  201,  301,  401,  501,  601,  700,
     799,  897,  995, 1092, 1189, 1285, 1380, 1474,
    1567, 1659, 1751, 1841, 1930, 2018, 2105, 2191,
    2275, 2358, 2439, 2519, 2598, 2675, 2750, 2824,
    2896, 2966, 3034, 3101, 3165, 3228, 3289, 3348,
    3405, 3460, 3512, 3563, 3611, 3658, 3702, 3744,
    3783, 3821, 3856, 3888, 3919, 3947, 3972, 3996,
    4016, 4035, 4051, 4064, 4075, 4084, 4090, 4094,
    4095
};

static uint16_t clamp_u16(uint32_t value, uint16_t maximum)
{
    if (value > maximum) {
        return maximum;
    }

    return (uint16_t)value;
}

static uint16_t mv_to_adc_counts(uint16_t mv)
{
    uint32_t scaled = ((uint32_t)mv * SIGNAL_ADC_MAX + (SIGNAL_FULL_SCALE_MV / 2U)) / SIGNAL_FULL_SCALE_MV;
    return clamp_u16(scaled, SIGNAL_ADC_MAX);
}

static void scope_apply_defaults(void)
{
    scope_app.config.mode = APP_SCOPE_MODE_SOURCE;
    scope_app.config.waveform = APP_WAVE_SINE;
    scope_app.config.source_frequency_hz = DEFAULT_SOURCE_FREQ_HZ;
    scope_app.config.sample_rate_hz = DEFAULT_SAMPLE_RATE_HZ;
    scope_app.config.vpp_mv = DEFAULT_VPP_MV;
    scope_app.config.offset_mv = DEFAULT_OFFSET_MV;
    scope_app.config.sample_count = SCOPE_SAMPLE_COUNT;
}

static const char *scope_mode_text(app_scope_mode_t mode)
{
    if (mode == APP_SCOPE_MODE_ADC) {
        return "ADC";
    }

    return "SOURCE";
}

static const char *scope_waveform_text(app_waveform_t waveform)
{
    switch (waveform) {
    case APP_WAVE_TRIANGLE:
        return "TRIANGLE";
    case APP_WAVE_SQUARE:
        return "SQUARE";
    case APP_WAVE_SAW:
        return "SAW";
    case APP_WAVE_SINE:
    default:
        return "SINE";
    }
}

static const char *scope_mode_short_text(app_scope_mode_t mode)
{
    return (mode == APP_SCOPE_MODE_ADC) ? "ADC" : "SRC";
}

static const char *scope_waveform_short_text(app_waveform_t waveform)
{
    switch (waveform) {
    case APP_WAVE_TRIANGLE:
        return "TRI";
    case APP_WAVE_SQUARE:
        return "SQR";
    case APP_WAVE_SAW:
        return "SAW";
    case APP_WAVE_SINE:
    default:
        return "SIN";
    }
}

static void scope_format_compact_rate(uint32_t value_hz, char *buffer, uint16_t buffer_size)
{
    uint32_t major;
    uint32_t minor;

    if (buffer == NULL || buffer_size == 0U) {
        return;
    }

    if (value_hz >= 1000UL) {
        major = value_hz / 1000UL;
        minor = (value_hz % 1000UL) / 100UL;
        if (minor == 0UL) {
            (void)snprintf(buffer, buffer_size, "%luK", (unsigned long)major);
        } else {
            (void)snprintf(buffer, buffer_size, "%luK%lu", (unsigned long)major, (unsigned long)minor);
        }
        return;
    }

    (void)snprintf(buffer, buffer_size, "%lu", (unsigned long)value_hz);
}

static void scope_format_scale_voltage(uint16_t mv, char *buffer, uint16_t buffer_size)
{
    uint32_t whole;
    uint32_t tenth;

    if (buffer == NULL || buffer_size == 0U) {
        return;
    }

    whole = (uint32_t)mv / 1000UL;
    tenth = ((uint32_t)(mv % 1000U) + 50UL) / 100UL;
    if (tenth >= 10UL) {
        ++whole;
        tenth = 0UL;
    }

    (void)snprintf(buffer, buffer_size, "%lu.%luV", (unsigned long)whole, (unsigned long)tenth);
}

static void scope_format_footer_voltage(char prefix, uint16_t mv, char *buffer, uint16_t buffer_size)
{
    uint32_t whole;
    uint32_t hundredth;

    if (buffer == NULL || buffer_size == 0U) {
        return;
    }

    whole = (uint32_t)mv / 1000UL;
    hundredth = ((uint32_t)(mv % 1000U) + 5UL) / 10UL;
    if (hundredth >= 100UL) {
        ++whole;
        hundredth = 0UL;
    }

    (void)snprintf(buffer, buffer_size, "%c%lu.%02lu", prefix, (unsigned long)whole, (unsigned long)hundredth);
}

static void scope_build_overlay(scope_ui_overlay_t *overlay)
{
    static char status_left[16];
    static char status_right[16];
    static char scale_top[8];
    static char scale_mid[8];
    static char scale_bottom[8];
    static char footer_left[8];
    static char footer_center[8];
    static char footer_right[8];
    char rate_text[8];
    char freq_text[8];
    if (overlay == NULL) {
        return;
    }

    scope_format_compact_rate(scope_app.config.sample_rate_hz, rate_text, sizeof(rate_text));
    scope_format_compact_rate(scope_app.config.source_frequency_hz, freq_text, sizeof(freq_text));
    (void)snprintf(
        status_left,
        sizeof(status_left),
        "%s %s",
        scope_mode_short_text(scope_app.config.mode),
        scope_waveform_short_text(scope_app.config.waveform)
    );
    (void)snprintf(
        status_right,
        sizeof(status_right),
        "R%s F%s",
        rate_text,
        freq_text
    );

    scope_format_scale_voltage(SIGNAL_FULL_SCALE_MV, scale_top, sizeof(scale_top));
    scope_format_scale_voltage((uint16_t)(SIGNAL_FULL_SCALE_MV / 2U), scale_mid, sizeof(scale_mid));
    scope_format_scale_voltage(0U, scale_bottom, sizeof(scale_bottom));

    scope_format_footer_voltage('V', scope_app.config.vpp_mv, footer_left, sizeof(footer_left));
    scope_format_footer_voltage('O', scope_app.config.offset_mv, footer_center, sizeof(footer_center));
    (void)snprintf(footer_right, sizeof(footer_right), "N%u", scope_app.config.sample_count);

    overlay->status_left = status_left;
    overlay->status_right = status_right;
    overlay->scale_top = scale_top;
    overlay->scale_mid = scale_mid;
    overlay->scale_bottom = scale_bottom;
    overlay->footer_left = footer_left;
    overlay->footer_center = footer_center;
    overlay->footer_right = footer_right;
    overlay->waveform_min = 0U;
    overlay->waveform_max = SIGNAL_ADC_MAX;
    overlay->use_fixed_range = 1U;
}

static uint8_t text_equals(const char *lhs, const char *rhs)
{
    if (lhs == NULL || rhs == NULL) {
        return 0U;
    }

    while (*lhs != '\0' && *rhs != '\0') {
        char lhs_char = *lhs++;
        char rhs_char = *rhs++;

        if (lhs_char >= 'a' && lhs_char <= 'z') {
            lhs_char = (char)(lhs_char - ('a' - 'A'));
        }
        if (rhs_char >= 'a' && rhs_char <= 'z') {
            rhs_char = (char)(rhs_char - ('a' - 'A'));
        }

        if (lhs_char != rhs_char) {
            return 0U;
        }
    }

    return (uint8_t)((*lhs == '\0' && *rhs == '\0') ? 1U : 0U);
}

static uint8_t parse_mode_token(const char *token, app_scope_mode_t *mode)
{
    if (text_equals(token, "ADC")) {
        *mode = APP_SCOPE_MODE_ADC;
        return 1U;
    }
    if (text_equals(token, "SOURCE")) {
        *mode = APP_SCOPE_MODE_SOURCE;
        return 1U;
    }

    return 0U;
}

static uint8_t parse_waveform_token(const char *token, app_waveform_t *waveform)
{
    if (text_equals(token, "SINE")) {
        *waveform = APP_WAVE_SINE;
        return 1U;
    }
    if (text_equals(token, "TRIANGLE")) {
        *waveform = APP_WAVE_TRIANGLE;
        return 1U;
    }
    if (text_equals(token, "SQUARE")) {
        *waveform = APP_WAVE_SQUARE;
        return 1U;
    }
    if (text_equals(token, "SAW")) {
        *waveform = APP_WAVE_SAW;
        return 1U;
    }

    return 0U;
}

static uint8_t parse_u32_token(const char *token, uint32_t *value)
{
    char *end_ptr = NULL;
    unsigned long parsed_value;

    if (token == NULL || *token == '\0') {
        return 0U;
    }

    parsed_value = strtoul(token, &end_ptr, 10);
    if (end_ptr == token || (end_ptr != NULL && *end_ptr != '\0')) {
        return 0U;
    }

    *value = (uint32_t)parsed_value;
    return 1U;
}

static uint16_t sine_sample_from_phase(uint16_t phase)
{
    uint16_t quadrant = (uint16_t)((phase >> 14) & 0x03U);
    uint16_t index = (uint16_t)((phase >> 8) & 0x3FU);
    uint16_t value;

    if (quadrant == 0U) {
        value = (uint16_t)(2048U + (sine_quarter[index] / 2U));
    } else if (quadrant == 1U) {
        value = (uint16_t)(2048U + (sine_quarter[64U - index] / 2U));
    } else if (quadrant == 2U) {
        value = (uint16_t)(2048U - (sine_quarter[index] / 2U));
    } else {
        value = (uint16_t)(2048U - (sine_quarter[64U - index] / 2U));
    }

    return value;
}

static uint16_t triangle_sample_from_phase(uint16_t phase)
{
    uint32_t rising;

    if (phase < 32768U) {
        rising = ((uint32_t)phase * SIGNAL_ADC_MAX) / 32767U;
        return (uint16_t)rising;
    }

    rising = ((uint32_t)(65535U - phase) * SIGNAL_ADC_MAX) / 32767U;
    return (uint16_t)rising;
}

static uint16_t square_sample_from_phase(uint16_t phase)
{
    return (phase < 32768U) ? SIGNAL_ADC_MAX : 0U;
}

static uint16_t saw_sample_from_phase(uint16_t phase)
{
    return (uint16_t)(((uint32_t)phase * SIGNAL_ADC_MAX) / 65535U);
}

static uint16_t waveform_base_sample(app_waveform_t waveform, uint16_t phase)
{
    switch (waveform) {
    case APP_WAVE_TRIANGLE:
        return triangle_sample_from_phase(phase);
    case APP_WAVE_SQUARE:
        return square_sample_from_phase(phase);
    case APP_WAVE_SAW:
        return saw_sample_from_phase(phase);
    case APP_WAVE_SINE:
    default:
        return sine_sample_from_phase(phase);
    }
}

static uint16_t waveform_apply_level(uint16_t base_sample)
{
    int32_t centered = (int32_t)base_sample - 2048;
    uint16_t vpp_counts = mv_to_adc_counts(scope_app.config.vpp_mv);
    uint16_t offset_counts = mv_to_adc_counts(scope_app.config.offset_mv);
    int32_t scaled = (centered * (int32_t)vpp_counts) / 4095;
    int32_t output = (int32_t)offset_counts + scaled;

    if (output < 0) {
        output = 0;
    }
    if (output > (int32_t)SIGNAL_ADC_MAX) {
        output = SIGNAL_ADC_MAX;
    }

    return (uint16_t)output;
}

static void scope_fill_source_frame(void)
{
    uint32_t phase_step;
    uint32_t phase_accum = scope_app.source_phase_accum;

    if (scope_app.config.sample_rate_hz == 0UL) {
        scope_app.config.sample_rate_hz = DEFAULT_SAMPLE_RATE_HZ;
    }

    phase_step = (uint32_t)(((uint64_t)scope_app.config.source_frequency_hz << 32) / scope_app.config.sample_rate_hz);

    for (uint16_t i = 0; i < scope_app.config.sample_count; ++i) {
        uint16_t phase = (uint16_t)(phase_accum >> 16);
        uint16_t base_sample = waveform_base_sample(scope_app.config.waveform, phase);
        scope_app.latest_samples[i] = waveform_apply_level(base_sample);
        phase_accum += phase_step;
    }

    scope_app.source_phase_accum = phase_accum;
}

static void scope_fill_adc_frame(ADC_HandleTypeDef *hadc)
{
    uint16_t last_sample = (scope_app.config.sample_count > 0U) ? scope_app.latest_samples[0] : 2048U;

    if (hadc == NULL) {
        scope_fill_source_frame();
        return;
    }

    for (uint16_t i = 0; i < scope_app.config.sample_count; ++i) {
        if (HAL_ADC_Start(hadc) != HAL_OK) {
            scope_app.latest_samples[i] = last_sample;
            continue;
        }

        if (HAL_ADC_PollForConversion(hadc, 10U) == HAL_OK) {
            last_sample = (uint16_t)HAL_ADC_GetValue(hadc);
            scope_app.latest_samples[i] = last_sample;
        } else {
            scope_app.latest_samples[i] = last_sample;
        }

        HAL_ADC_Stop(hadc);
    }
}

static void scope_draw_latest_frame(void)
{
    scope_ui_overlay_t overlay;

    scope_build_overlay(&overlay);
    ScopeUI_DrawWaveform(scope_app.latest_samples, scope_app.config.sample_count, &overlay);
}

static void scope_write_error(char *response, uint16_t response_size, const char *reason)
{
    if (response == NULL || response_size == 0U) {
        return;
    }

    (void)snprintf(response, response_size, "ERR %s", (reason != NULL) ? reason : "UNKNOWN");
}

void App_OscilloscopeInit(void)
{
    const tft_config_t tft_config = {
        .width = TFT_VIEW_WIDTH,
        .height = TFT_VIEW_HEIGHT,
        .x_offset = TFT_X_OFFSET,
        .y_offset = TFT_Y_OFFSET,
        .madctl = TFT_MADCTL_NATIVE,
        .controller = TFT_CONTROLLER_ST7735
    };

    const scope_ui_config_t ui_config = {
        .width = 160U,
        .height = 128U,
        .top = SCOPE_UI_TOP,
        .bottom = SCOPE_UI_BOTTOM,
        .left = SCOPE_UI_LEFT,
        .right = SCOPE_UI_RIGHT,
        .adc_max = SIGNAL_ADC_MAX
    };

    scope_apply_defaults();
    memset(scope_app.latest_samples, 0, sizeof(scope_app.latest_samples));
    scope_app.source_phase_accum = 0UL;

    TFT_Init(&tft_config);
    ScopeUI_Init(&ui_config);
}

void App_OscilloscopeFrame(ADC_HandleTypeDef *hadc)
{
    if (scope_app.config.mode == APP_SCOPE_MODE_SOURCE) {
        scope_fill_source_frame();
    } else {
        scope_fill_adc_frame(hadc);
    }

    scope_draw_latest_frame();
}

void App_OscilloscopeDemoFrame(void)
{
    scope_fill_source_frame();
    scope_draw_latest_frame();
}

void App_OscilloscopeAdcFrame(ADC_HandleTypeDef *hadc)
{
    App_OscilloscopeFrame(hadc);
}

void App_DisplayDiagnosticFrame(void)
{
    TFT_Backlight(0);
    HAL_Delay(1000);

    TFT_Backlight(1);
    TFT_Fill(TFT_COLOR_RED);
    HAL_Delay(1000);

    TFT_Fill(TFT_COLOR_GREEN);
    HAL_Delay(1000);

    TFT_Fill(TFT_COLOR_BLUE);
    HAL_Delay(1000);

    TFT_Fill(TFT_COLOR_BLACK);
    HAL_Delay(1000);
}

void App_OscilloscopeSetMode(app_scope_mode_t mode)
{
    if (mode == APP_SCOPE_MODE_ADC || mode == APP_SCOPE_MODE_SOURCE) {
        scope_app.config.mode = mode;
    }
}

void App_OscilloscopeSetWaveform(app_waveform_t waveform)
{
    if (waveform <= APP_WAVE_SAW) {
        scope_app.config.waveform = waveform;
    }
}

void App_OscilloscopeGetConfig(app_scope_config_t *config)
{
    if (config != NULL) {
        *config = scope_app.config;
    }
}

HAL_StatusTypeDef App_OscilloscopeFormatConfigLine(char *response, uint16_t response_size)
{
    int written;

    if (response == NULL || response_size == 0U) {
        return HAL_ERROR;
    }

    written = snprintf(
        response,
        response_size,
        "CONFIG MODE=%s WAVE=%s FREQ_HZ=%lu RATE_HZ=%lu VPP_MV=%u OFFSET_MV=%u SAMPLE_COUNT=%u",
        scope_mode_text(scope_app.config.mode),
        scope_waveform_text(scope_app.config.waveform),
        (unsigned long)scope_app.config.source_frequency_hz,
        (unsigned long)scope_app.config.sample_rate_hz,
        scope_app.config.vpp_mv,
        scope_app.config.offset_mv,
        scope_app.config.sample_count
    );

    return (written > 0 && (uint32_t)written < response_size) ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef App_OscilloscopeFormatFrameLine(char *response, uint16_t response_size, uint16_t max_points)
{
    uint16_t min_sample;
    uint16_t max_sample;
    int written;
    uint16_t point_count;
    uint16_t offset;

    if (response == NULL || response_size == 0U || scope_app.config.sample_count < 2U) {
        return HAL_ERROR;
    }

    point_count = max_points;
    if (point_count == 0U || point_count > HOST_FRAME_POINT_LIMIT) {
        point_count = HOST_FRAME_POINT_LIMIT;
    }
    if (point_count > scope_app.config.sample_count) {
        point_count = scope_app.config.sample_count;
    }

    min_sample = scope_app.latest_samples[0];
    max_sample = scope_app.latest_samples[0];
    for (uint16_t i = 1U; i < scope_app.config.sample_count; ++i) {
        uint16_t sample = scope_app.latest_samples[i];
        if (sample < min_sample) {
            min_sample = sample;
        }
        if (sample > max_sample) {
            max_sample = sample;
        }
    }

    written = snprintf(
        response,
        response_size,
        "FRAME MODE=%s WAVE=%s COUNT=%u RATE_HZ=%lu MIN=%u MAX=%u DATA=",
        scope_mode_text(scope_app.config.mode),
        scope_waveform_text(scope_app.config.waveform),
        point_count,
        (unsigned long)scope_app.config.sample_rate_hz,
        min_sample,
        max_sample
    );
    if (written <= 0 || (uint32_t)written >= response_size) {
        return HAL_ERROR;
    }

    offset = (uint16_t)written;
    for (uint16_t index = 0U; index < point_count; ++index) {
        uint16_t sample_index = (uint16_t)(((uint32_t)index * scope_app.config.sample_count) / point_count);
        uint16_t sample = scope_app.latest_samples[sample_index];

        written = snprintf(
            &response[offset],
            (size_t)(response_size - offset),
            (index == 0U) ? "%u" : ",%u",
            sample
        );
        if (written <= 0 || (uint32_t)written >= (response_size - offset)) {
            return HAL_ERROR;
        }

        offset = (uint16_t)(offset + written);
    }

    return HAL_OK;
}

HAL_StatusTypeDef App_OscilloscopeHandleHostCommand(const char *command, char *response, uint16_t response_size)
{
    uint32_t value;
    app_scope_mode_t mode;
    app_waveform_t waveform;

    if (response == NULL || response_size == 0U) {
        return HAL_ERROR;
    }

    if (command == NULL || *command == '\0') {
        scope_write_error(response, response_size, "EMPTY");
        return HAL_ERROR;
    }

    if (text_equals(command, "HELLO")) {
        (void)snprintf(response, response_size, "OK SIMPLE_SCOPE_G474");
        return HAL_OK;
    }

    if (text_equals(command, "GET CONFIG")) {
        return App_OscilloscopeFormatConfigLine(response, response_size);
    }

    if (text_equals(command, "GET FRAME")) {
        return App_OscilloscopeFormatFrameLine(response, response_size, HOST_FRAME_POINT_LIMIT);
    }

    if (text_equals(command, "SET PRESET DEFAULT")) {
        scope_apply_defaults();
        (void)snprintf(response, response_size, "OK PRESET=DEFAULT");
        return HAL_OK;
    }

    if (strncmp(command, "SET MODE ", 9U) == 0) {
        if (!parse_mode_token(&command[9], &mode)) {
            scope_write_error(response, response_size, "MODE");
            return HAL_ERROR;
        }
        App_OscilloscopeSetMode(mode);
        (void)snprintf(response, response_size, "OK MODE=%s", scope_mode_text(scope_app.config.mode));
        return HAL_OK;
    }

    if (strncmp(command, "SET WAVE ", 9U) == 0) {
        if (!parse_waveform_token(&command[9], &waveform)) {
            scope_write_error(response, response_size, "WAVE");
            return HAL_ERROR;
        }
        App_OscilloscopeSetWaveform(waveform);
        (void)snprintf(response, response_size, "OK WAVE=%s", scope_waveform_text(scope_app.config.waveform));
        return HAL_OK;
    }

    if (strncmp(command, "SET FREQ_HZ ", 12U) == 0) {
        if (!parse_u32_token(&command[12], &value) || value == 0UL || value > 50000UL) {
            scope_write_error(response, response_size, "FREQ_HZ");
            return HAL_ERROR;
        }
        scope_app.config.source_frequency_hz = value;
        (void)snprintf(response, response_size, "OK FREQ_HZ=%lu", (unsigned long)scope_app.config.source_frequency_hz);
        return HAL_OK;
    }

    if (strncmp(command, "SET RATE_HZ ", 12U) == 0) {
        if (!parse_u32_token(&command[12], &value) || value < 1000UL || value > 100000UL) {
            scope_write_error(response, response_size, "RATE_HZ");
            return HAL_ERROR;
        }
        scope_app.config.sample_rate_hz = value;
        (void)snprintf(response, response_size, "OK RATE_HZ=%lu", (unsigned long)scope_app.config.sample_rate_hz);
        return HAL_OK;
    }

    if (strncmp(command, "SET VPP_MV ", 11U) == 0) {
        if (!parse_u32_token(&command[11], &value) || value > SIGNAL_FULL_SCALE_MV) {
            scope_write_error(response, response_size, "VPP_MV");
            return HAL_ERROR;
        }
        scope_app.config.vpp_mv = (uint16_t)value;
        (void)snprintf(response, response_size, "OK VPP_MV=%u", scope_app.config.vpp_mv);
        return HAL_OK;
    }

    if (strncmp(command, "SET OFFSET_MV ", 14U) == 0) {
        if (!parse_u32_token(&command[14], &value) || value > SIGNAL_FULL_SCALE_MV) {
            scope_write_error(response, response_size, "OFFSET_MV");
            return HAL_ERROR;
        }
        scope_app.config.offset_mv = (uint16_t)value;
        (void)snprintf(response, response_size, "OK OFFSET_MV=%u", scope_app.config.offset_mv);
        return HAL_OK;
    }

    scope_write_error(response, response_size, "UNKNOWN_CMD");
    return HAL_ERROR;
}
