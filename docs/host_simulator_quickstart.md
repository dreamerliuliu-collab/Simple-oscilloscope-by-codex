# Host App and MCU Simulator Quick Start

## What Is Completed

- MCU firmware:
  - Runs on STM32G474VETx.
  - Drives the ST7735 display through software SPI on `PE7..PE12`.
  - Shows a waveform locally on the TFT.
  - Has two logical modes in `Core/Src/app_oscilloscope.c`:
    - `SOURCE`: internal signal source, no extra circuit required.
    - `ADC`: oscilloscope sampling on `PA0 / ADC1_IN1`.
- Lower-machine simulator:
  - `tools/scope_mcu_simulator.py`
  - Speaks the same text protocol used by the firmware command parser.
- Host app:
  - `tools/scope_host_app.py`
  - Connects to the simulator over TCP, changes waveform parameters, and draws frames.

## No Extra Circuit Path

If the target is to finish a simple signal source + oscilloscope without adding hardware:

1. Flash the firmware already present in this project.
2. Let the board run in default `SOURCE` mode and display the waveform on the TFT.
3. Run the simulator and host app on the PC for host-side control and protocol demonstration.

This gives a complete deliverable with:

- local waveform display on the MCU side
- host-side signal source controls
- host-side oscilloscope plotting
- no added analog loopback circuit

## Run the Simulator

Use:

```bat
tools\run_scope_mcu_simulator.cmd
```

Default bind address:

- host: `127.0.0.1`
- port: `9000`

Optional:

```bat
tools\run_scope_mcu_simulator.cmd --host 127.0.0.1 --port 9001
```

## Run the Host App

Use:

```bat
tools\run_scope_host_app.cmd
```

Then:

1. Keep `Host = 127.0.0.1`
2. Keep `Port = 9000`
3. Click `Connect`
4. Click `Apply` or `Fetch Frame`

## Supported Host Commands

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

## Current Boundary on the Real Board

The protocol parser is already implemented in:

- `Core/Src/app_oscilloscope.c`

But the current CubeMX project does not yet enable a UART transport path in firmware, so the PC host app is intended to talk to the simulator right now, not directly to the board.

That means the deliverable is:

- real MCU waveform display on TFT
- real MCU oscilloscope sampling path on `PA0`
- PC host software and lower-machine simulator fully available for offline integration and demo

## Recommended Demo Flow

1. Power the board and confirm the TFT shows the internal waveform.
2. Start `tools\run_scope_mcu_simulator.cmd`.
3. Start `tools\run_scope_host_app.cmd`.
4. Switch waveform, frequency, sample rate, and amplitude from the host app.
5. Use `ADC` mode in the simulator to demonstrate captured-wave behavior with noise.
