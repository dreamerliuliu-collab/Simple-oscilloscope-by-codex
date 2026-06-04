from __future__ import annotations

import argparse
import socketserver
import threading

from simple_scope_protocol import (
    DEFAULT_HOST,
    DEFAULT_PORT,
    FULL_SCALE_MV,
    ScopeConfig,
    format_config_line,
    format_frame_line,
    generate_wave_samples,
    normalize_mode_name,
    normalize_wave_name,
)


SIMULATOR_ID = "SIMPLE_SCOPE_G474_SIM"
ADC_NOISE_COUNTS = 18
MAX_FREQ_HZ = 50_000
MIN_RATE_HZ = 1_000
MAX_RATE_HZ = 100_000


class ScopeSimulator:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._config = ScopeConfig()
        self._phase_seed = 0.0

    def handle_command(self, command: str) -> str:
        command = (command or "").strip()
        if not command:
            return "ERR EMPTY"

        with self._lock:
            if command.upper() == "HELLO":
                return f"OK {SIMULATOR_ID}"

            if command.upper() == "GET CONFIG":
                return format_config_line(self._config)

            if command.upper() == "GET FRAME":
                return self._build_frame_line()

            if command.upper() == "SET PRESET DEFAULT":
                self._config = ScopeConfig()
                self._phase_seed = 0.0
                return "OK PRESET=DEFAULT"

            if command.upper().startswith("SET MODE "):
                mode = normalize_mode_name(command[9:])
                if mode not in {"ADC", "SOURCE"}:
                    return "ERR MODE"
                self._config.mode = mode
                return f"OK MODE={mode}"

            if command.upper().startswith("SET WAVE "):
                wave = normalize_wave_name(command[9:])
                if wave not in {"SINE", "TRIANGLE", "SQUARE", "SAW"}:
                    return "ERR WAVE"
                self._config.waveform = wave
                return f"OK WAVE={wave}"

            if command.upper().startswith("SET FREQ_HZ "):
                value = self._parse_int(command[12:])
                if value is None or value <= 0 or value > MAX_FREQ_HZ:
                    return "ERR FREQ_HZ"
                self._config.freq_hz = value
                return f"OK FREQ_HZ={value}"

            if command.upper().startswith("SET RATE_HZ "):
                value = self._parse_int(command[12:])
                if value is None or value < MIN_RATE_HZ or value > MAX_RATE_HZ:
                    return "ERR RATE_HZ"
                self._config.rate_hz = value
                return f"OK RATE_HZ={value}"

            if command.upper().startswith("SET VPP_MV "):
                value = self._parse_int(command[11:])
                if value is None or value < 0 or value > FULL_SCALE_MV:
                    return "ERR VPP_MV"
                self._config.vpp_mv = value
                return f"OK VPP_MV={value}"

            if command.upper().startswith("SET OFFSET_MV "):
                value = self._parse_int(command[14:])
                if value is None or value < 0 or value > FULL_SCALE_MV:
                    return "ERR OFFSET_MV"
                self._config.offset_mv = value
                return f"OK OFFSET_MV={value}"

        return "ERR UNKNOWN_CMD"

    def _build_frame_line(self) -> str:
        noise = ADC_NOISE_COUNTS if normalize_mode_name(self._config.mode) == "ADC" else 0
        samples, self._phase_seed = generate_wave_samples(
            self._config,
            self._config.sample_count,
            phase_seed=self._phase_seed,
            adc_noise_counts=noise,
        )
        return format_frame_line(self._config, samples)

    @staticmethod
    def _parse_int(token: str) -> int | None:
        token = (token or "").strip()
        if not token:
            return None
        try:
            return int(token, 10)
        except ValueError:
            return None


class ScopeRequestHandler(socketserver.StreamRequestHandler):
    def handle(self) -> None:
        server: ScopeSimulatorServer = self.server  # type: ignore[assignment]
        address = f"{self.client_address[0]}:{self.client_address[1]}"
        print(f"[sim] client connected: {address}")
        try:
            while True:
                raw = self.rfile.readline()
                if not raw:
                    break
                command = raw.decode("utf-8", errors="replace").strip()
                response = server.simulator.handle_command(command)
                self.wfile.write((response + "\n").encode("utf-8"))
                self.wfile.flush()
                print(f"[sim] {command} -> {response}")
        finally:
            print(f"[sim] client disconnected: {address}")


class ScopeSimulatorServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, server_address: tuple[str, int], simulator: ScopeSimulator) -> None:
        super().__init__(server_address, ScopeRequestHandler)
        self.simulator = simulator


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="SimpleScope G474 lower-machine simulator over TCP."
    )
    parser.add_argument("--host", default=DEFAULT_HOST, help="bind address")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="bind port")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    simulator = ScopeSimulator()
    with ScopeSimulatorServer((args.host, args.port), simulator) as server:
        print(f"[sim] listening on {args.host}:{args.port}")
        print("[sim] commands: HELLO, GET CONFIG, GET FRAME, SET ...")
        server.serve_forever()


if __name__ == "__main__":
    main()
