# STM32G474VET6 Simple Oscilloscope Hardware Plan

## Confirmed Hardware

- MCU: STM32G474VET6 / STM32G474VETx, LQFP100
- Toolchain: STM32CubeMX + Keil MDK-ARM
- Programmer/debugger: PWLink through SWD
- UART connected to PC: COM8, optional for later debug output
- Oscilloscope analog input: PA0 / ADC1_IN1
- TFT controller: ST7735, matching the previous Voltage_R_Button_Test project
- Previous TFT reference project: E:\num1\Voltage_R_Button_Test
- TFT connection:

| TFT Signal | STM32 Pin | Suggested Mode |
| --- | --- | --- |
| SCL | PE7 | GPIO output, software SPI clock |
| SDA | PE8 | GPIO output, software SPI MOSI |
| RST | PE9 | GPIO output |
| DC | PE10 | GPIO output |
| CS | PE11 | GPIO output |
| BLK | PE12 | GPIO output or timer PWM later |

## Display Strategy

The shown TFT wiring uses SCL/SDA/RST/DC/CS/BLK only. To avoid being blocked by alternate-function conflicts, the first firmware should drive the TFT with software SPI on GPIOE. After the display is proven, it can be migrated to hardware SPI only if the selected pins support the same SPI peripheral.

Default driver target:

- ST7735/ST7735S compatible SPI TFT
- Initial resolution used by the previous project: 160x128
- Initial MADCTL value from the previous project: 0xA0
- 16-bit RGB565 framebuffer-less drawing
- Basic primitives first: clear screen, grid, waveform polyline, text labels

The previous project's `lcd_write_data_u16()` wrote the low color byte as `data & 0x0f`. The new driver writes the full low byte so RGB565 colors are preserved.

## Oscilloscope Firmware Plan

1. ADC samples one analog input through DMA circular buffer.
2. Timer trigger controls the sampling rate.
3. Main loop converts ADC buffer to screen coordinates.
4. TFT renders grid and waveform.
5. UART COM8 prints status and accepts simple commands later.

Recommended first target:

- 1 channel
- Input pin: PA0 / ADC1_IN1
- 12-bit ADC
- 1024-sample circular buffer
- 10 kS/s to 100 kS/s initial sampling range
- Input protected and limited to 0 V to 3.3 V at the MCU pin

## CubeMX Baseline

- Chip: STM32G474VETx
- Project toolchain: MDK-ARM / Keil
- System:
  - SYS Debug: Serial Wire
  - RCC: HSE if the board has an external crystal, otherwise HSI
- GPIO:
  - PE7, PE8, PE9, PE10, PE11, PE12 as output
- ADC:
  - PA0 as ADC1_IN1
- UART:
  - Optional. Enable the USART mapped to COM8 only after the display and ADC are running.
  - Start with 115200 8N1
- ADC:
  - Enable one ADC channel for the oscilloscope input
  - External trigger from timer update event
  - DMA circular mode
- Timer:
  - Generates ADC trigger at selected sampling rate

## First Screen Test Hook

After CubeMX generates `main.c`, add these includes and calls inside the user code sections:

```c
#include "app_oscilloscope.h"
```

After `MX_GPIO_Init()`:

```c
App_OscilloscopeInit();
```

For screen-only testing, use this inside the main `while (1)` loop:

```c
App_OscilloscopeDemoFrame();
```

After `MX_ADC1_Init()` exists and `adc.h` is included in `main.c`, use this for PA0 polling acquisition:

```c
App_OscilloscopeAdcFrame(&hadc1);
```

This should draw the oscilloscope grid and a waveform from PA0 before ADC/DMA is connected.
