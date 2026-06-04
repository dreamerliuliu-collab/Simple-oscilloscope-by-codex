# Simple Oscilloscope by Codex

An STM32G474-based simple oscilloscope and signal-source project with:

- on-board TFT waveform display
- MCU-side internal signal source
- ADC-based oscilloscope sampling path
- PC host application
- MCU simulator for offline host-side testing

This repository is intended for a small 1.8-inch ST7735 TFT module and an STM32G474VETx target generated from STM32CubeMX and built with Keil MDK-ARM.

## Project Overview

This project combines two usage paths:

1. Real hardware path
   The STM32 drives the TFT directly and displays a waveform on the screen.
2. PC demo and development path
   A Python host app connects to a Python MCU simulator so the protocol and UI can be tested without extra analog circuitry.

The firmware currently supports two logical modes:

- `SOURCE`
  Internal waveform generation for display/demo.
- `ADC`
  Sample `PA0 / ADC1_IN1` and display the measured waveform.

## Main Features

- STM32G474 firmware based on CubeMX project structure
- Software SPI driver for ST7735 TFT on `PE7..PE12`
- Oscilloscope-style UI with waveform area, status bar, scale labels, and footer
- Internal sine / triangle / square / saw waveform source
- ADC sampling display path for external analog input on `PA0`
- Text command protocol for configuration and frame export
- Python host application for control and waveform display
- Python MCU simulator for offline integration testing

## Repository Structure

- `Core/`
  Main firmware source files and custom oscilloscope UI logic.
- `Drivers/`
  STM32 CMSIS and HAL driver sources.
- `MDK-ARM/`
  Keil project files.
- `docs/`
  Setup notes and quick-start documentation.
- `tools/`
  Python host application, MCU simulator, and protocol helpers.
- `SimpleScope_G474.ioc`
  STM32CubeMX project file.

## Hardware

### MCU

- STM32G474VETx

### Display

- 1.8-inch ST7735 TFT
- Effective project UI target: landscape display

### TFT Wiring

- `PE7`  -> `SCL`
- `PE8`  -> `SDA`
- `PE9`  -> `RST`
- `PE10` -> `DC`
- `PE11` -> `CS`
- `PE12` -> `BLK`

### Oscilloscope Input

- `PA0` -> `ADC1_IN1`

### SWD

- `PA13` -> `SWDIO`
- `PA14` -> `SWCLK`
- `GND` must be common

### Safety

Do not apply a voltage above `3.3V` or below `GND` to `PA0`.

## Firmware Build and Flash

### 1. Open CubeMX

Open:

- `SimpleScope_G474.ioc`

Confirm:

- MCU is `STM32G474VETx`
- `PA0 = ADC1_IN1`
- `PE7..PE12 = GPIO_Output`
- `PA13/PA14 = Serial Wire`

Generate code for `MDK-ARM`.

### 2. Open Keil

Open:

- `MDK-ARM/SimpleScope_G474.uvprojx`

Make sure these custom files are included in the target:

- `Core/Src/tft_softspi.c`
- `Core/Src/scope_ui.c`
- `Core/Src/app_oscilloscope.c`

### 3. Build and Flash

Build the Keil target and flash it through your SWD tool.

## How to See the Oscilloscope Effect

There are two different visual behaviors:

### `SOURCE` mode

The waveform on the TFT is generated internally by firmware.
This is useful for UI testing and display verification.

### `ADC` mode

The waveform on the TFT comes from `PA0 / ADC1_IN1`.
This is the actual oscilloscope path.

Important:

- If `PA0` is connected to a fixed DC voltage, you will mostly see a flat line.
- To see a changing oscilloscope waveform, `PA0` must receive a time-varying signal within `0V ~ 3.3V`.

## PC Host App and MCU Simulator

The `tools/` directory contains a complete host-side demo path.

### Files

- `tools/scope_host_app.py`
  PC host UI.
- `tools/scope_mcu_simulator.py`
  Lower-machine simulator over TCP.
- `tools/simple_scope_protocol.py`
  Shared protocol parser / formatter / waveform helper.
- `tools/run_scope_host_app.cmd`
  Start the host app.
- `tools/run_scope_mcu_simulator.cmd`
  Start the simulator.

### Start the Simulator

Run:

```bat
tools\run_scope_mcu_simulator.cmd
```

Default address:

- Host: `127.0.0.1`
- Port: `9000`

### Start the Host App

Run:

```bat
tools\run_scope_host_app.cmd
```

Then:

1. Set `Host = 127.0.0.1`
2. Set `Port = 9000`
3. Click `Connect`
4. Use `Apply` or `Fetch Frame`

### Host-Side Supported Commands

- `HELLO`
- `GET CONFIG`
- `GET FRAME`
- `SET PRESET DEFAULT`
- `SET MODE SOURCE`
- `SET MODE ADC`
- `SET WAVE SINE`
- `SET WAVE TRIANGLE`
- `SET WAVE SQUARE`
- `SET WAVE SAW`
- `SET FREQ_HZ <value>`
- `SET RATE_HZ <value>`
- `SET VPP_MV <value>`
- `SET OFFSET_MV <value>`

## Recommended First Demo Flow

1. Flash the firmware to the STM32 board.
2. Confirm the TFT powers up and shows the oscilloscope UI.
3. Use `SOURCE` mode first to verify display rendering.
4. Feed a safe analog signal into `PA0` to verify `ADC` mode.
5. Run the simulator and host app on PC for offline protocol testing.

## Current Project Boundary

The firmware already contains command parsing logic in:

- `Core/Src/app_oscilloscope.c`

However, the real board currently does not yet expose a completed UART transport path for those commands.
That means:

- the real MCU can display waveforms locally on the TFT
- the host app can fully control the simulator
- direct host-to-real-board remote control is not yet finished

## Related Documents

- `docs/first_run_steps.md`
- `docs/host_simulator_quickstart.md`
- `docs/oscilloscope_hardware_plan.md`

## License / Notes

This repository contains STM32 HAL and CMSIS files from ST's generated project structure.
Please follow the original upstream license terms included in the `Drivers/` directory when redistributing or reusing those components.
