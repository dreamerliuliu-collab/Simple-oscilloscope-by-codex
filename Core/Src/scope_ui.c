#include "scope_ui.h"
#include "tft_softspi.h"

#define SCOPE_FB_WIDTH  160U
#define SCOPE_FB_HEIGHT 128U
#define SCOPE_STATUS_BAR_HEIGHT  12U
#define SCOPE_FOOTER_BAR_HEIGHT  13U
#define SCOPE_SCALE_WIDTH        24U
#define SCOPE_CHAR_WIDTH         5U
#define SCOPE_CHAR_HEIGHT        7U
#define SCOPE_CHAR_SPACING       1U

static scope_ui_config_t scope = {
    .width = SCOPE_FB_WIDTH,
    .height = SCOPE_FB_HEIGHT,
    .top = 16,
    .bottom = 111,
    .left = 3,
    .right = 132,
    .adc_max = 4095
};

static uint16_t framebuf[SCOPE_FB_WIDTH * SCOPE_FB_HEIGHT];
static uint16_t wave_y[SCOPE_FB_WIDTH];

static void fb_draw_hline(uint16_t x, uint16_t y, uint16_t w, uint16_t color);

static uint16_t scope_scale_x(void)
{
    uint16_t preferred = (scope.width > SCOPE_SCALE_WIDTH) ? (uint16_t)(scope.width - SCOPE_SCALE_WIDTH) : scope.right;

    if (preferred <= scope.right) {
        if (scope.right + 2U >= scope.width) {
            return scope.right;
        }
        return (uint16_t)(scope.right + 2U);
    }

    return preferred;
}

static void scope_normalize_bounds(void)
{
    if (scope.width == 0U || scope.width > SCOPE_FB_WIDTH) {
        scope.width = SCOPE_FB_WIDTH;
    }
    if (scope.height == 0U || scope.height > SCOPE_FB_HEIGHT) {
        scope.height = SCOPE_FB_HEIGHT;
    }

    if (scope.left >= scope.width) {
        scope.left = 0U;
    }
    if (scope.right >= scope.width) {
        scope.right = (uint16_t)(scope.width - 1U);
    }
    if (scope.right <= scope.left) {
        scope.right = (uint16_t)(scope.width - 1U);
    }

    if (scope.top >= scope.height) {
        scope.top = 0U;
    }
    if (scope.bottom >= scope.height) {
        scope.bottom = (uint16_t)(scope.height - 1U);
    }
    if (scope.bottom <= scope.top) {
        scope.bottom = (uint16_t)(scope.height - 1U);
    }
}

static uint16_t scope_plot_height(void)
{
    return (uint16_t)(scope.bottom - scope.top + 1U);
}

static uint16_t scope_plot_width(void)
{
    return (uint16_t)(scope.right - scope.left + 1U);
}

static uint16_t adc_to_y_scaled(uint16_t adc, uint16_t min_sample, uint16_t max_sample)
{
    const uint16_t y_margin = 8U;
    uint16_t safe_top = (uint16_t)(scope.top + y_margin);
    uint16_t safe_bottom = (scope.bottom > y_margin) ? (uint16_t)(scope.bottom - y_margin) : scope.bottom;
    uint32_t sample_range = (uint32_t)max_sample - (uint32_t)min_sample;
    uint32_t draw_range;
    uint32_t scaled;
    int32_t y;

    if (safe_bottom <= safe_top) {
        safe_top = scope.top;
        safe_bottom = scope.bottom;
    }

    if (sample_range < 64U) {
        sample_range = 64U;
        if (min_sample > 32U) {
            min_sample = (uint16_t)(min_sample - 32U);
        } else {
            min_sample = 0U;
        }
    }

    draw_range = (uint32_t)safe_bottom - (uint32_t)safe_top;
    if (adc < min_sample) {
        adc = min_sample;
    }
    if (adc > max_sample) {
        adc = max_sample;
    }

    scaled = (((uint32_t)adc - (uint32_t)min_sample) * draw_range + (sample_range / 2U)) / sample_range;
    y = (int32_t)safe_bottom - (int32_t)scaled;

    if (y < (int32_t)safe_top) {
        y = safe_top;
    }
    if (y > (int32_t)safe_bottom) {
        y = safe_bottom;
    }

    return (uint16_t)y;
}

static void fb_clear(uint16_t color)
{
    uint32_t count = (uint32_t)scope.width * (uint32_t)scope.height;
    for (uint32_t i = 0; i < count; ++i) {
        framebuf[i] = color;
    }
}

static void fb_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (x >= scope.width || y >= scope.height || w == 0U || h == 0U) {
        return;
    }

    if ((uint32_t)x + w > scope.width) {
        w = (uint16_t)(scope.width - x);
    }
    if ((uint32_t)y + h > scope.height) {
        h = (uint16_t)(scope.height - y);
    }

    for (uint16_t row = 0; row < h; ++row) {
        fb_draw_hline(x, (uint16_t)(y + row), w, color);
    }
}

static void fb_set_pixel(int16_t x, int16_t y, uint16_t color)
{
    if (x < 0 || y < 0 || x >= (int16_t)scope.width || y >= (int16_t)scope.height) {
        return;
    }

    framebuf[(uint32_t)y * scope.width + (uint16_t)x] = color;
}

static void fb_draw_hline(uint16_t x, uint16_t y, uint16_t w, uint16_t color)
{
    if (y >= scope.height || x >= scope.width) {
        return;
    }
    if ((uint32_t)x + w > scope.width) {
        w = (uint16_t)(scope.width - x);
    }

    uint32_t index = (uint32_t)y * scope.width + x;
    for (uint16_t i = 0; i < w; ++i) {
        framebuf[index + i] = color;
    }
}

static void fb_draw_vline(uint16_t x, uint16_t y, uint16_t h, uint16_t color)
{
    if (x >= scope.width || y >= scope.height) {
        return;
    }
    if ((uint32_t)y + h > scope.height) {
        h = (uint16_t)(scope.height - y);
    }

    uint32_t index = (uint32_t)y * scope.width + x;
    for (uint16_t i = 0; i < h; ++i) {
        framebuf[index] = color;
        index += scope.width;
    }
}

static void fb_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color)
{
    int16_t dx = (x0 < x1) ? (int16_t)(x1 - x0) : (int16_t)(x0 - x1);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t dy = (y0 < y1) ? (int16_t)(y0 - y1) : (int16_t)(y1 - y0);
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = (int16_t)(dx + dy);

    for (;;) {
        fb_set_pixel(x0, y0, color);

        if (x0 == x1 && y0 == y1) {
            break;
        }

        int16_t e2 = (int16_t)(2 * err);
        if (e2 >= dy) {
            err = (int16_t)(err + dy);
            x0 = (int16_t)(x0 + sx);
        }
        if (e2 <= dx) {
            err = (int16_t)(err + dx);
            y0 = (int16_t)(y0 + sy);
        }
    }
}

static const uint8_t *scope_font_bitmap(char ch)
{
    static const uint8_t glyph_space[5] = {0x00U, 0x00U, 0x00U, 0x00U, 0x00U};
    static const uint8_t glyph_dot[5] = {0x00U, 0x00U, 0x60U, 0x60U, 0x00U};
    static const uint8_t glyph_0[5] = {0x3EU, 0x51U, 0x49U, 0x45U, 0x3EU};
    static const uint8_t glyph_1[5] = {0x00U, 0x42U, 0x7FU, 0x40U, 0x00U};
    static const uint8_t glyph_2[5] = {0x42U, 0x61U, 0x51U, 0x49U, 0x46U};
    static const uint8_t glyph_3[5] = {0x21U, 0x41U, 0x45U, 0x4BU, 0x31U};
    static const uint8_t glyph_4[5] = {0x18U, 0x14U, 0x12U, 0x7FU, 0x10U};
    static const uint8_t glyph_5[5] = {0x27U, 0x45U, 0x45U, 0x45U, 0x39U};
    static const uint8_t glyph_6[5] = {0x3CU, 0x4AU, 0x49U, 0x49U, 0x30U};
    static const uint8_t glyph_7[5] = {0x01U, 0x71U, 0x09U, 0x05U, 0x03U};
    static const uint8_t glyph_8[5] = {0x36U, 0x49U, 0x49U, 0x49U, 0x36U};
    static const uint8_t glyph_9[5] = {0x06U, 0x49U, 0x49U, 0x29U, 0x1EU};
    static const uint8_t glyph_a[5] = {0x7EU, 0x11U, 0x11U, 0x11U, 0x7EU};
    static const uint8_t glyph_c[5] = {0x3EU, 0x41U, 0x41U, 0x41U, 0x22U};
    static const uint8_t glyph_d[5] = {0x7FU, 0x41U, 0x41U, 0x22U, 0x1CU};
    static const uint8_t glyph_f[5] = {0x7FU, 0x09U, 0x09U, 0x09U, 0x01U};
    static const uint8_t glyph_i[5] = {0x00U, 0x41U, 0x7FU, 0x41U, 0x00U};
    static const uint8_t glyph_k[5] = {0x7FU, 0x08U, 0x14U, 0x22U, 0x41U};
    static const uint8_t glyph_n[5] = {0x7FU, 0x10U, 0x08U, 0x04U, 0x7FU};
    static const uint8_t glyph_o[5] = {0x3EU, 0x41U, 0x41U, 0x41U, 0x3EU};
    static const uint8_t glyph_q[5] = {0x3EU, 0x41U, 0x51U, 0x21U, 0x5EU};
    static const uint8_t glyph_r[5] = {0x7FU, 0x09U, 0x19U, 0x29U, 0x46U};
    static const uint8_t glyph_s[5] = {0x46U, 0x49U, 0x49U, 0x49U, 0x31U};
    static const uint8_t glyph_t[5] = {0x01U, 0x01U, 0x7FU, 0x01U, 0x01U};
    static const uint8_t glyph_u[5] = {0x3FU, 0x40U, 0x40U, 0x40U, 0x3FU};
    static const uint8_t glyph_v[5] = {0x1FU, 0x20U, 0x40U, 0x20U, 0x1FU};
    static const uint8_t glyph_w[5] = {0x7FU, 0x20U, 0x18U, 0x20U, 0x7FU};

    switch (ch) {
    case '0': return glyph_0;
    case '1': return glyph_1;
    case '2': return glyph_2;
    case '3': return glyph_3;
    case '4': return glyph_4;
    case '5': return glyph_5;
    case '6': return glyph_6;
    case '7': return glyph_7;
    case '8': return glyph_8;
    case '9': return glyph_9;
    case 'A': return glyph_a;
    case 'C': return glyph_c;
    case 'D': return glyph_d;
    case 'F': return glyph_f;
    case 'I': return glyph_i;
    case 'K': return glyph_k;
    case 'N': return glyph_n;
    case 'O': return glyph_o;
    case 'Q': return glyph_q;
    case 'R': return glyph_r;
    case 'S': return glyph_s;
    case 'T': return glyph_t;
    case 'U': return glyph_u;
    case 'V': return glyph_v;
    case 'W': return glyph_w;
    case '.': return glyph_dot;
    case ' ':
    default:
        return glyph_space;
    }
}

static void fb_draw_char(uint16_t x, uint16_t y, char ch, uint16_t fg, uint16_t bg)
{
    const uint8_t *glyph = scope_font_bitmap(ch);

    for (uint16_t column = 0; column < SCOPE_CHAR_WIDTH; ++column) {
        uint8_t bits = glyph[column];
        for (uint16_t row = 0; row < SCOPE_CHAR_HEIGHT; ++row) {
            uint16_t color = ((bits >> row) & 0x01U) ? fg : bg;
            fb_set_pixel((int16_t)(x + column), (int16_t)(y + row), color);
        }
    }

    for (uint16_t row = 0; row < SCOPE_CHAR_HEIGHT; ++row) {
        fb_set_pixel((int16_t)(x + SCOPE_CHAR_WIDTH), (int16_t)(y + row), bg);
    }
}

static void fb_draw_text(uint16_t x, uint16_t y, const char *text, uint16_t fg, uint16_t bg)
{
    uint16_t cursor_x = x;

    if (text == NULL) {
        return;
    }

    while (*text != '\0') {
        fb_draw_char(cursor_x, y, *text, fg, bg);
        cursor_x = (uint16_t)(cursor_x + SCOPE_CHAR_WIDTH + SCOPE_CHAR_SPACING);
        ++text;
    }
}

static void fb_draw_text_right(uint16_t right_x, uint16_t y, const char *text, uint16_t fg, uint16_t bg)
{
    uint16_t length = 0U;
    uint16_t width;

    if (text == NULL) {
        return;
    }

    while (text[length] != '\0') {
        ++length;
    }

    width = (length == 0U) ? 0U : (uint16_t)(length * SCOPE_CHAR_WIDTH + (length - 1U) * SCOPE_CHAR_SPACING);
    if (width > right_x) {
        fb_draw_text(0U, y, text, fg, bg);
    } else {
        fb_draw_text((uint16_t)(right_x - width), y, text, fg, bg);
    }
}

static void scope_draw_overlay(const scope_ui_overlay_t *overlay)
{
    const uint16_t status_bg = 0x10A2U;
    const uint16_t footer_bg = 0x18C3U;
    const uint16_t text_color = TFT_COLOR_WHITE;
    const uint16_t accent_color = TFT_COLOR_YELLOW;
    const uint16_t scale_color = 0x94B2U;
    const uint16_t scale_x = scope_scale_x();
    const uint16_t scale_right = (scope.width > 1U) ? (uint16_t)(scope.width - 2U) : 0U;
    const uint16_t mid_y = (uint16_t)((scope.top + scope.bottom) / 2U);
    const uint16_t footer_y = (scope.height > SCOPE_FOOTER_BAR_HEIGHT) ? (uint16_t)(scope.height - SCOPE_FOOTER_BAR_HEIGHT) : 0U;

    fb_fill_rect(0U, 0U, scope.width, SCOPE_STATUS_BAR_HEIGHT, status_bg);
    fb_fill_rect(0U, footer_y, scope.width, SCOPE_FOOTER_BAR_HEIGHT, footer_bg);
    fb_draw_hline(0U, (uint16_t)(SCOPE_STATUS_BAR_HEIGHT - 1U), scope.width, 0x2945U);
    fb_draw_hline(0U, footer_y, scope.width, 0x2945U);

    if (overlay != NULL) {
        fb_draw_text(4U, 2U, overlay->status_left, accent_color, status_bg);
        fb_draw_text_right((uint16_t)(scope.width - 5U), 2U, overlay->status_right, text_color, status_bg);

        fb_draw_text(4U, (uint16_t)(footer_y + 3U), overlay->footer_left, text_color, footer_bg);
        fb_draw_text((uint16_t)(scope.width / 2U - 16U), (uint16_t)(footer_y + 3U), overlay->footer_center, text_color, footer_bg);
        fb_draw_text_right((uint16_t)(scope.width - 5U), (uint16_t)(footer_y + 3U), overlay->footer_right, accent_color, footer_bg);

        fb_draw_text(scale_x, (uint16_t)(scope.top - 1U), overlay->scale_top, scale_color, TFT_COLOR_BLACK);
        fb_draw_text(scale_x, (uint16_t)(mid_y - 3U), overlay->scale_mid, scale_color, TFT_COLOR_BLACK);
        fb_draw_text(scale_x, (uint16_t)(scope.bottom - 6U), overlay->scale_bottom, scale_color, TFT_COLOR_BLACK);
    }

    fb_draw_hline(scale_x, scope.top, (uint16_t)(scale_right - scale_x + 1U), 0x2104U);
    fb_draw_hline(scale_x, mid_y, (uint16_t)(scale_right - scale_x + 1U), 0x2104U);
    fb_draw_hline(scale_x, scope.bottom, (uint16_t)(scale_right - scale_x + 1U), 0x2104U);
    fb_draw_vline((uint16_t)(scale_x + 1U), scope.top, scope_plot_height(), 0x2104U);
}

static void scope_draw_grid_to_buffer(void)
{
    const uint16_t grid_color = 0x3186U;
    const uint16_t axis_color = TFT_COLOR_GRAY;
    const uint16_t border_color = 0x52AAU;
    uint16_t width = scope_plot_width();
    uint16_t height = scope_plot_height();

    fb_clear(TFT_COLOR_BLACK);

    for (uint16_t x = scope.left; x < scope.right; x = (uint16_t)(x + 20U)) {
        fb_draw_vline(x, scope.top, height, grid_color);
    }

    for (uint16_t y = scope.top; y < scope.bottom; y = (uint16_t)(y + 20U)) {
        fb_draw_hline(scope.left, y, width, grid_color);
    }

    fb_draw_hline(scope.left, scope.top, width, border_color);
    fb_draw_hline(scope.left, scope.bottom, width, border_color);
    fb_draw_vline(scope.left, scope.top, height, border_color);
    fb_draw_vline(scope.right, scope.top, height, border_color);

    fb_draw_hline(scope.left, (uint16_t)((scope.top + scope.bottom) / 2U), width, axis_color);
    fb_draw_vline((uint16_t)((scope.left + scope.right) / 2U), scope.top, height, axis_color);
}

static void scope_push_frame(void)
{
    TFT_WritePixelsLandscape(framebuf, scope.width, scope.height);
}

void ScopeUI_Init(const scope_ui_config_t *config)
{
    if (config != NULL) {
        scope = *config;
    }
    scope_normalize_bounds();

    ScopeUI_DrawGrid();
}

void ScopeUI_DrawGrid(void)
{
    scope_draw_grid_to_buffer();
    scope_draw_overlay(NULL);
    scope_push_frame();
}

void ScopeUI_DrawWaveform(const uint16_t *samples, uint16_t sample_count, const scope_ui_overlay_t *overlay)
{
    uint16_t min_sample;
    uint16_t max_sample;

    if (samples == NULL || sample_count < 2U) {
        return;
    }

    scope_draw_grid_to_buffer();

    if (overlay != NULL && overlay->use_fixed_range != 0U && overlay->waveform_max > overlay->waveform_min) {
        min_sample = overlay->waveform_min;
        max_sample = overlay->waveform_max;
    } else {
        min_sample = samples[0];
        max_sample = samples[0];
        for (uint16_t i = 0; i < sample_count; ++i) {
            uint16_t sample = samples[i];
            if (sample < min_sample) {
                min_sample = sample;
            }
            if (sample > max_sample) {
                max_sample = sample;
            }
        }
    }

    uint16_t plot_w = scope_plot_width();
    if (plot_w > SCOPE_FB_WIDTH) {
        plot_w = SCOPE_FB_WIDTH;
    }

    for (uint16_t x_index = 0; x_index < plot_w; ++x_index) {
        uint32_t start = ((uint32_t)x_index * sample_count) / plot_w;
        uint32_t end = ((uint32_t)(x_index + 1U) * sample_count) / plot_w;
        if (end <= start) {
            end = start + 1U;
        }
        if (end > sample_count) {
            end = sample_count;
        }

        uint32_t bucket_sum = 0U;
        uint32_t bucket_count = 0U;

        for (uint32_t i = start; i < end; ++i) {
            uint16_t sample = samples[i];
            bucket_sum += sample;
            ++bucket_count;
        }

        uint16_t bucket_avg = (uint16_t)(bucket_sum / bucket_count);
        wave_y[x_index] = adc_to_y_scaled(bucket_avg, min_sample, max_sample);
    }

    uint16_t prev_x = scope.left;
    uint16_t prev_y = wave_y[0];

    for (uint16_t x_index = 1; x_index < plot_w; ++x_index) {
        uint16_t x = (uint16_t)(scope.left + x_index);
        uint16_t y = wave_y[x_index];
        fb_draw_line((int16_t)prev_x, (int16_t)prev_y, (int16_t)x, (int16_t)y, TFT_COLOR_GREEN);
        prev_x = x;
        prev_y = y;
    }

    scope_draw_overlay(overlay);
    scope_push_frame();
}
