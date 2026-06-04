#ifndef TFT_SOFTSPI_H
#define TFT_SOFTSPI_H

#include "stm32g4xx_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TFT_GPIO_PORT
#define TFT_GPIO_PORT GPIOE
#endif

#ifndef TFT_SCL_PIN
#define TFT_SCL_PIN GPIO_PIN_7
#endif

#ifndef TFT_SDA_PIN
#define TFT_SDA_PIN GPIO_PIN_8
#endif

#ifndef TFT_RST_PIN
#define TFT_RST_PIN GPIO_PIN_9
#endif

#ifndef TFT_DC_PIN
#define TFT_DC_PIN GPIO_PIN_10
#endif

#ifndef TFT_CS_PIN
#define TFT_CS_PIN GPIO_PIN_11
#endif

#ifndef TFT_BLK_PIN
#define TFT_BLK_PIN GPIO_PIN_12
#endif

typedef enum {
    TFT_CONTROLLER_ST7735 = 0,
    TFT_CONTROLLER_ST7789 = 1
} tft_controller_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t x_offset;
    uint8_t y_offset;
    uint8_t madctl;
    tft_controller_t controller;
} tft_config_t;

#define TFT_COLOR_BLACK   0x0000U
#define TFT_COLOR_WHITE   0xFFFFU
#define TFT_COLOR_RED     0xF800U
#define TFT_COLOR_GREEN   0x07E0U
#define TFT_COLOR_BLUE    0x001FU
#define TFT_COLOR_CYAN    0x07FFU
#define TFT_COLOR_YELLOW  0xFFE0U
#define TFT_COLOR_GRAY    0x8410U

void TFT_Init(const tft_config_t *config);
void TFT_Backlight(uint8_t on);
void TFT_Fill(uint16_t color);
void TFT_DrawPixel(uint16_t x, uint16_t y, uint16_t color);
void TFT_DrawFastHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color);
void TFT_DrawFastVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color);
void TFT_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
void TFT_SetAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void TFT_WriteColorRepeat(uint16_t color, uint32_t count);
void TFT_WritePixels(const uint16_t *pixels, uint32_t count);
void TFT_WritePixelsLandscape(const uint16_t *pixels, uint16_t logical_width, uint16_t logical_height);

#ifdef __cplusplus
}
#endif

#endif
