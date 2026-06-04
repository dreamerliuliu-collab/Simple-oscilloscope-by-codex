from __future__ import annotations

from dataclasses import dataclass
import math


ADC_MAX = 4095
FULL_SCALE_MV = 3300
DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 9000
HOST_FRAME_POINT_LIMIT = 160


@dataclass
class ScopeConfig:
    mode: str = "SOURCE"
    waveform: str = "SINE"
    freq_hz: int = 250
    rate_hz: int = 20_000
    vpp_mv: int = 1800
    offset_mv: int = 1650
    sample_count: int = 640


@dataclass
class ScopeFrame:
    mode: str
    waveform: str
    count: int
    rate_hz: int
    minimum: int
    maximum: int
    samples: list[int]


def clamp(value: int, minimum: int, maximum: int) -> int:
    return max(minimum, min(value, maximum))


def mv_to_counts(mv: int) -> int:
    return clamp(round(mv * ADC_MAX / FULL_SCALE_MV), 0, ADC_MAX)


def normalize_wave_name(name: str) -> str:
    return (name or "SINE").strip().upper()


def normalize_mode_name(name: str) -> str:
    return (name or "SOURCE").strip().upper()


def parse_key_value_fields(payload: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for token in payload.split():
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        fields[key.strip().upper()] = value.strip()
    return fields


def parse_config_line(line: str) -> ScopeConfig:
    if not line.startswith("CONFIG "):
        raise ValueError(f"unexpected config line: {line!r}")
    fields = parse_key_value_fields(line[7:])
    return ScopeConfig(
        mode=fields.get("MODE", "SOURCE"),
        waveform=fields.get("WAVE", "SINE"),
        freq_hz=int(fields.get("FREQ_HZ", "250")),
        rate_hz=int(fields.get("RATE_HZ", "20000")),
        vpp_mv=int(fields.get("VPP_MV", "1800")),
        offset_mv=int(fields.get("OFFSET_MV", "1650")),
        sample_count=int(fields.get("SAMPLE_COUNT", "640")),
    )


def parse_frame_line(line: str) -> ScopeFrame:
    if not line.startswith("FRAME "):
        raise ValueError(f"unexpected frame line: {line!r}")
    meta, _, data_blob = line.partition(" DATA=")
    fields = parse_key_value_fields(meta[6:])
    samples = [int(part) for part in data_blob.split(",") if part]
    return ScopeFrame(
        mode=fields.get("MODE", "SOURCE"),
        waveform=fields.get("WAVE", "SINE"),
        count=int(fields.get("COUNT", str(len(samples)))),
        rate_hz=int(fields.get("RATE_HZ", "20000")),
        minimum=int(fields.get("MIN", "0")),
        maximum=int(fields.get("MAX", "0")),
        samples=samples,
    )


def build_set_commands(config: ScopeConfig) -> list[str]:
    return [
        f"SET MODE {normalize_mode_name(config.mode)}",
        f"SET WAVE {normalize_wave_name(config.waveform)}",
        f"SET FREQ_HZ {int(config.freq_hz)}",
        f"SET RATE_HZ {int(config.rate_hz)}",
        f"SET VPP_MV {int(config.vpp_mv)}",
        f"SET OFFSET_MV {int(config.offset_mv)}",
    ]


def _base_wave_sample(waveform: str, phase: float) -> float:
    waveform = normalize_wave_name(waveform)
    phase = phase % 1.0
    if waveform == "TRIANGLE":
        return 1.0 - abs((phase * 4.0) - 2.0)
    if waveform == "SQUARE":
        return 1.0 if phase < 0.5 else -1.0
    if waveform == "SAW":
        return (phase * 2.0) - 1.0
    return math.sin(phase * 2.0 * math.pi)


def generate_wave_samples(
    config: ScopeConfig,
    count: int,
    phase_seed: float = 0.0,
    adc_noise_counts: int = 0,
) -> tuple[list[int], float]:
    count = max(2, int(count))
    freq_hz = max(1, int(config.freq_hz))
    rate_hz = max(1000, int(config.rate_hz))
    vpp_counts = mv_to_counts(int(config.vpp_mv))
    offset_counts = mv_to_counts(int(config.offset_mv))
    step = freq_hz / rate_hz
    phase = phase_seed
    samples: list[int] = []

    for index in range(count):
        base = _base_wave_sample(config.waveform, phase)
        value = offset_counts + round(base * (vpp_counts / 2.0))
        if adc_noise_counts:
            noise = ((index * 29) % (adc_noise_counts * 2 + 1)) - adc_noise_counts
            value += noise
        samples.append(clamp(value, 0, ADC_MAX))
        phase += step

    return samples, phase % 1.0


def format_config_line(config: ScopeConfig) -> str:
    return (
        "CONFIG "
        f"MODE={normalize_mode_name(config.mode)} "
        f"WAVE={normalize_wave_name(config.waveform)} "
        f"FREQ_HZ={int(config.freq_hz)} "
        f"RATE_HZ={int(config.rate_hz)} "
        f"VPP_MV={int(config.vpp_mv)} "
        f"OFFSET_MV={int(config.offset_mv)} "
        f"SAMPLE_COUNT={int(config.sample_count)}"
    )


def resample_samples(samples: list[int], point_count: int) -> list[int]:
    if not samples:
        return []

    point_count = clamp(int(point_count), 1, min(len(samples), HOST_FRAME_POINT_LIMIT))
    reduced: list[int] = []
    for index in range(point_count):
        sample_index = (index * len(samples)) // point_count
        reduced.append(samples[sample_index])
    return reduced


def format_frame_line(config: ScopeConfig, samples: list[int], point_count: int = HOST_FRAME_POINT_LIMIT) -> str:
    if len(samples) < 2:
        raise ValueError("at least two samples are required")

    sampled = resample_samples(samples, point_count)
    payload = ",".join(str(value) for value in sampled)
    return (
        "FRAME "
        f"MODE={normalize_mode_name(config.mode)} "
        f"WAVE={normalize_wave_name(config.waveform)} "
        f"COUNT={len(sampled)} "
        f"RATE_HZ={int(config.rate_hz)} "
        f"MIN={min(samples)} "
        f"MAX={max(samples)} "
        f"DATA={payload}"
    )
