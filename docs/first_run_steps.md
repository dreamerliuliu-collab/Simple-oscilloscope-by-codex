# First Run Steps

## Hardware

- ST7735 screen:
  - PE7: SCL
  - PE8: SDA
  - PE9: RST
  - PE10: DC
  - PE11: CS
  - PE12: BLK
- Oscilloscope input:
  - PA0 / ADC1_IN1
- Programmer:
  - PWLink connected through SWD
  - PA13: SWDIO
  - PA14: SWCLK
  - GND must be common

Do not feed PA0 above 3.3 V or below GND. Add an input divider/protection stage before measuring signals outside the MCU voltage range.

## CubeMX

1. Open `SimpleScope_G474.ioc`.
2. Confirm the MCU is `STM32G474VETx`.
3. Confirm these pins:
   - `PA0 = ADC1_IN1`
   - `PE7..PE12 = GPIO_Output`
   - `PA13/PA14 = Serial Wire`
4. Generate code for `MDK-ARM`.

## Keil

After CubeMX generates the MDK project, make sure these custom files are included in the Keil target:

- `Core/Src/tft_softspi.c`
- `Core/Src/scope_ui.c`
- `Core/Src/app_oscilloscope.c`

Add this include in `Core/Src/main.c` inside a user code include block:

```c
#include "app_oscilloscope.h"
#include "adc.h"
```

After `MX_GPIO_Init()` and `MX_ADC1_Init()`:

```c
App_OscilloscopeInit();
```

Inside `while (1)`:

```c
App_OscilloscopeAdcFrame(&hadc1);
```

If you want to test only the screen first, use this instead:

```c
App_OscilloscopeDemoFrame();
```

## Expected Result

- Demo mode: the ST7735 shows a grid and a moving test waveform.
- ADC mode: the ST7735 shows a grid and a waveform based on PA0 voltage.

## PC Host + Simulator

To complete the upper-computer and lower-machine simulation workflow without adding circuitry:

1. Start the simulator:

```bat
tools\run_scope_mcu_simulator.cmd
```

2. Start the host app:

```bat
tools\run_scope_host_app.cmd
```

3. Connect the host app to `127.0.0.1:9000`.

The host app can:

- switch `SOURCE` / `ADC`
- change waveform type
- change frequency, sample rate, amplitude, and offset
- fetch and draw waveform frames

For a fuller description, see:

- `docs/host_simulator_quickstart.md`
