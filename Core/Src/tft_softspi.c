#include "tft_softspi.h"

#define TFT_CMD_SWRESET 0x01U
#define TFT_CMD_SLPOUT  0x11U
#define TFT_CMD_COLMOD  0x3AU
#define TFT_CMD_MADCTL  0x36U
#define TFT_CMD_CASET   0x2AU
#define TFT_CMD_RASET   0x2BU
#define TFT_CMD_RAMWR   0x2CU
#define TFT_CMD_INVON   0x21U
#define TFT_CMD_NORON   0x13U
#define TFT_CMD_DISPON  0x29U

static tft_config_t tft;
static uint16_t tft_linebuf[128];

#define TFT_PIN_SET(pin)   (TFT_GPIO_PORT->BSRR = (uint32_t)(pin))
#define TFT_PIN_RESET(pin) (TFT_GPIO_PORT->BSRR = ((uint32_t)(pin) << 16U))

static void tft_write_pin(uint16_t pin, GPIO_PinState state)
{
    if (state == GPIO_PIN_SET) {
        TFT_PIN_SET(pin);
    } else {
        TFT_PIN_RESET(pin);
    }
}

static void tft_write_u8(uint8_t data)
{
    for (uint8_t mask = 0x80U; mask != 0U; mask >>= 1U) {
        TFT_PIN_RESET(TFT_SCL_PIN);
        if ((data & mask) != 0U) {
            TFT_PIN_SET(TFT_SDA_PIN);
        } else {
            TFT_PIN_RESET(TFT_SDA_PIN);
        }
        TFT_PIN_SET(TFT_SCL_PIN);
    }
}

static void tft_select(void)
{
    tft_write_pin(TFT_CS_PIN, GPIO_PIN_RESET);
}

static void tft_deselect(void)
{
    tft_write_pin(TFT_CS_PIN, GPIO_PIN_SET);
}

static void tft_write_command(uint8_t command)
{
    tft_select();
    tft_write_pin(TFT_DC_PIN, GPIO_PIN_RESET);
    tft_write_u8(command);
    tft_deselect();
}

static void tft_write_data_u8(uint8_t data)
{
    tft_select();
    tft_write_pin(TFT_DC_PIN, GPIO_PIN_SET);
    tft_write_u8(data);
    tft_deselect();
}

static void tft_write_data_u16(uint16_t data)
{
    tft_select();
    tft_write_pin(TFT_DC_PIN, GPIO_PIN_SET);
    tft_write_u8((uint8_t)(data >> 8));
    tft_write_u8((uint8_t)data);
    tft_deselect();
}

static void tft_init_st7735s(void)
{
    tft_write_command(TFT_CMD_SLPOUT);
    HAL_Delay(120);

    tft_write_command(0xB1U);
    tft_write_data_u8(0x01U);
    tft_write_data_u8(0x2CU);
    tft_write_data_u8(0x2DU);

    tft_write_command(0xB2U);
    tft_write_data_u8(0x01U);
    tft_write_data_u8(0x2CU);
    tft_write_data_u8(0x2DU);

    tft_write_command(0xB3U);
    tft_write_data_u8(0x01U);
    tft_write_data_u8(0x2CU);
    tft_write_data_u8(0x2DU);
    tft_write_data_u8(0x01U);
    tft_write_data_u8(0x2CU);
    tft_write_data_u8(0x2DU);

    tft_write_command(0xB4U);
    tft_write_data_u8(0x07U);

    tft_write_command(0xC0U);
    tft_write_data_u8(0xA2U);
    tft_write_data_u8(0x02U);
    tft_write_data_u8(0x84U);

    tft_write_command(0xC1U);
    tft_write_data_u8(0xC5U);

    tft_write_command(0xC2U);
    tft_write_data_u8(0x0AU);
    tft_write_data_u8(0x00U);

    tft_write_command(0xC3U);
    tft_write_data_u8(0x8AU);
    tft_write_data_u8(0x2AU);

    tft_write_command(0xC4U);
    tft_write_data_u8(0x8AU);
    tft_write_data_u8(0xEEU);

    tft_write_command(0xC5U);
    tft_write_data_u8(0x0EU);

    tft_write_command(TFT_CMD_MADCTL);
    tft_write_data_u8(tft.madctl);

    tft_write_command(0xE0U);
    const uint8_t gamma_pos[] = {
        0x0FU, 0x1AU, 0x0FU, 0x18U, 0x2FU, 0x28U, 0x20U, 0x22U,
        0x1FU, 0x1BU, 0x23U, 0x37U, 0x00U, 0x07U, 0x02U, 0x10U
    };
    for (uint32_t i = 0; i < sizeof(gamma_pos); ++i) {
        tft_write_data_u8(gamma_pos[i]);
    }

    tft_write_command(0xE1U);
    const uint8_t gamma_neg[] = {
        0x0FU, 0x1BU, 0x0FU, 0x17U, 0x33U, 0x2CU, 0x29U, 0x2EU,
        0x30U, 0x30U, 0x39U, 0x3FU, 0x00U, 0x07U, 0x03U, 0x10U
    };
    for (uint32_t i = 0; i < sizeof(gamma_neg); ++i) {
        tft_write_data_u8(gamma_neg[i]);
    }

    tft_write_command(0xF0U);
    tft_write_data_u8(0x01U);

    tft_write_command(0xF6U);
    tft_write_data_u8(0x00U);

    tft_write_command(TFT_CMD_COLMOD);
    tft_write_data_u8(0x05U);

    tft_write_command(TFT_CMD_DISPON);
    HAL_Delay(120);
}

static void tft_reset(void)
{
    tft_write_pin(TFT_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(20);
    tft_write_pin(TFT_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(20);
    tft_write_pin(TFT_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(120);
}

void TFT_Init(const tft_config_t *config)
{
    if (config != NULL) {
        tft = *config;
    } else {
        tft.width = 128;
        tft.height = 160;
        tft.x_offset = 0;
        tft.y_offset = 0;
        tft.madctl = 0xA8U;
        tft.controller = TFT_CONTROLLER_ST7735;
    }

    tft_deselect();
    tft_write_pin(TFT_SCL_PIN, GPIO_PIN_SET);
    tft_write_pin(TFT_SDA_PIN, GPIO_PIN_SET);
    TFT_Backlight(1);
    tft_reset();

    if (tft.controller == TFT_CONTROLLER_ST7735) {
        tft_init_st7735s();
        TFT_Fill(TFT_COLOR_BLACK);
        return;
    }

    tft_write_command(TFT_CMD_SWRESET);
    HAL_Delay(150);
    tft_write_command(TFT_CMD_SLPOUT);
    HAL_Delay(120);

    tft_write_command(TFT_CMD_COLMOD);
    tft_write_data_u8(0x55U);

    tft_write_command(TFT_CMD_MADCTL);
    tft_write_data_u8(tft.madctl);

    if (tft.controller == TFT_CONTROLLER_ST7789) {
        tft_write_command(TFT_CMD_INVON);
    }

    tft_write_command(TFT_CMD_NORON);
    HAL_Delay(10);
    tft_write_command(TFT_CMD_DISPON);
    HAL_Delay(120);
    TFT_Fill(TFT_COLOR_BLACK);
}

void TFT_Backlight(uint8_t on)
{
    tft_write_pin(TFT_BLK_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void TFT_SetAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    x0 = (uint16_t)(x0 + tft.x_offset);
    x1 = (uint16_t)(x1 + tft.x_offset);
    y0 = (uint16_t)(y0 + tft.y_offset);
    y1 = (uint16_t)(y1 + tft.y_offset);

    tft_write_command(TFT_CMD_CASET);
    tft_write_data_u16(x0);
    tft_write_data_u16(x1);

    tft_write_command(TFT_CMD_RASET);
    tft_write_data_u16(y0);
    tft_write_data_u16(y1);

    tft_write_command(TFT_CMD_RAMWR);
}

void TFT_WriteColorRepeat(uint16_t color, uint32_t count)
{
    tft_select();
    tft_write_pin(TFT_DC_PIN, GPIO_PIN_SET);
    while (count-- > 0U) {
        tft_write_u8((uint8_t)(color >> 8));
        tft_write_u8((uint8_t)color);
    }
    tft_deselect();
}

void TFT_WritePixels(const uint16_t *pixels, uint32_t count)
{
    if (pixels == NULL) {
        return;
    }

    tft_select();
    tft_write_pin(TFT_DC_PIN, GPIO_PIN_SET);
    while (count-- > 0U) {
        uint16_t color = *pixels++;
        tft_write_u8((uint8_t)(color >> 8));
        tft_write_u8((uint8_t)color);
    }
    tft_deselect();
}

void TFT_WritePixelsLandscape(const uint16_t *pixels, uint16_t logical_width, uint16_t logical_height)
{
    if (pixels == NULL) {
        return;
    }

    if (logical_width != tft.height || logical_height != tft.width) {
        return;
    }

    TFT_SetAddressWindow(0U, 0U, (uint16_t)(tft.width - 1U), (uint16_t)(tft.height - 1U));

    tft_select();
    tft_write_pin(TFT_DC_PIN, GPIO_PIN_SET);

    for (uint16_t phys_y = 0U; phys_y < tft.height; ++phys_y) {
        for (uint16_t phys_x = 0U; phys_x < tft.width; ++phys_x) {
            uint16_t logical_x = phys_y;
            uint16_t logical_y = (uint16_t)(logical_height - 1U - phys_x);
            tft_linebuf[phys_x] = pixels[(uint32_t)logical_y * logical_width + logical_x];
        }

        for (uint16_t phys_x = 0U; phys_x < tft.width; ++phys_x) {
            uint16_t color = tft_linebuf[phys_x];
            tft_write_u8((uint8_t)(color >> 8));
            tft_write_u8((uint8_t)color);
        }
    }

    tft_deselect();
}

void TFT_Fill(uint16_t color)
{
    TFT_SetAddressWindow(0, 0, (uint16_t)(tft.width - 1U), (uint16_t)(tft.height - 1U));
    TFT_WriteColorRepeat(color, (uint32_t)tft.width * (uint32_t)tft.height);
}

void TFT_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= tft.width || y >= tft.height) {
        return;
    }

    TFT_SetAddressWindow(x, y, x, y);
    TFT_WriteColorRepeat(color, 1);
}

void TFT_DrawFastHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color)
{
    if (x >= tft.width || y >= tft.height || w == 0U) {
        return;
    }
    if ((uint32_t)x + w > tft.width) {
        w = (uint16_t)(tft.width - x);
    }

    TFT_SetAddressWindow(x, y, (uint16_t)(x + w - 1U), y);
    TFT_WriteColorRepeat(color, w);
}

void TFT_DrawFastVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color)
{
    if (x >= tft.width || y >= tft.height || h == 0U) {
        return;
    }
    if ((uint32_t)y + h > tft.height) {
        h = (uint16_t)(tft.height - y);
    }

    TFT_SetAddressWindow(x, y, x, (uint16_t)(y + h - 1U));
    TFT_WriteColorRepeat(color, h);
}

void TFT_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color)
{
    int16_t dx = (x0 < x1) ? (int16_t)(x1 - x0) : (int16_t)(x0 - x1);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t dy = (y0 < y1) ? (int16_t)(y0 - y1) : (int16_t)(y1 - y0);
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = (int16_t)(dx + dy);

    for (;;) {
        if (x0 >= 0 && y0 >= 0) {
            TFT_DrawPixel((uint16_t)x0, (uint16_t)y0, color);
        }

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
