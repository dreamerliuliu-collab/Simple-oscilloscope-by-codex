from __future__ import annotations

import socket
import tkinter as tk
from tkinter import messagebox, ttk

from simple_scope_protocol import (
    DEFAULT_HOST,
    DEFAULT_PORT,
    ScopeConfig,
    ScopeFrame,
    build_set_commands,
    parse_config_line,
    parse_frame_line,
)


CANVAS_WIDTH = 900
CANVAS_HEIGHT = 420
GRID_DIV_X = 10
GRID_DIV_Y = 8


class ScopeTcpClient:
    def __init__(self) -> None:
        self._socket: socket.socket | None = None
        self._reader = None
        self._writer = None

    @property
    def is_connected(self) -> bool:
        return self._socket is not None

    def connect(self, host: str, port: int, timeout: float = 2.0) -> None:
        self.close()
        sock = socket.create_connection((host, port), timeout=timeout)
        sock.settimeout(timeout)
        self._socket = sock
        self._reader = sock.makefile("r", encoding="utf-8", newline="\n")
        self._writer = sock.makefile("w", encoding="utf-8", newline="\n")

    def close(self) -> None:
        for handle in (self._reader, self._writer):
            if handle is not None:
                handle.close()
        if self._socket is not None:
            self._socket.close()
        self._socket = None
        self._reader = None
        self._writer = None

    def request(self, command: str) -> str:
        if not self.is_connected or self._writer is None or self._reader is None:
            raise ConnectionError("not connected")
        self._writer.write(command.strip() + "\n")
        self._writer.flush()
        response = self._reader.readline()
        if not response:
            raise ConnectionError("connection closed by remote host")
        return response.strip()


class ScopeHostApp:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.client = ScopeTcpClient()
        self.polling_job: str | None = None

        self.host_var = tk.StringVar(value=DEFAULT_HOST)
        self.port_var = tk.StringVar(value=str(DEFAULT_PORT))
        self.mode_var = tk.StringVar(value="SOURCE")
        self.wave_var = tk.StringVar(value="SINE")
        self.freq_var = tk.StringVar(value="250")
        self.rate_var = tk.StringVar(value="20000")
        self.vpp_var = tk.StringVar(value="1800")
        self.offset_var = tk.StringVar(value="1650")
        self.auto_refresh_var = tk.BooleanVar(value=True)
        self.status_var = tk.StringVar(value="Disconnected")
        self.frame_info_var = tk.StringVar(value="No frame yet")
        self.config_info_var = tk.StringVar(value="Config not loaded")

        self._build_window()
        self._draw_scope_background()
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    def _build_window(self) -> None:
        self.root.title("SimpleScope Host Console")
        self.root.geometry("1180x760")
        self.root.minsize(1080, 700)
        self.root.configure(bg="#efe6d8")

        style = ttk.Style()
        style.theme_use("clam")
        style.configure("TFrame", background="#efe6d8")
        style.configure("TLabelframe", background="#efe6d8", borderwidth=1)
        style.configure("TLabelframe.Label", background="#efe6d8", foreground="#2c2a28")
        style.configure("TLabel", background="#efe6d8", foreground="#2c2a28")
        style.configure("TButton", padding=6)
        style.configure("Accent.TButton", padding=6)

        root_frame = ttk.Frame(self.root, padding=12)
        root_frame.pack(fill="both", expand=True)
        root_frame.columnconfigure(0, weight=2)
        root_frame.columnconfigure(1, weight=1)
        root_frame.rowconfigure(1, weight=1)

        header = ttk.Frame(root_frame)
        header.grid(row=0, column=0, columnspan=2, sticky="ew", pady=(0, 10))
        header.columnconfigure(0, weight=1)
        ttk.Label(
            header,
            text="SimpleScope G474 Host Console",
            font=("Segoe UI", 17, "bold"),
        ).grid(row=0, column=0, sticky="w")
        ttk.Label(header, textvariable=self.status_var, font=("Segoe UI", 10, "bold")).grid(
            row=0, column=1, sticky="e"
        )

        left = ttk.Frame(root_frame)
        left.grid(row=1, column=0, sticky="nsew", padx=(0, 10))
        left.rowconfigure(0, weight=1)
        left.rowconfigure(1, weight=0)
        left.columnconfigure(0, weight=1)

        right = ttk.Frame(root_frame)
        right.grid(row=1, column=1, sticky="nsew")
        right.columnconfigure(0, weight=1)

        self.canvas = tk.Canvas(
            left,
            width=CANVAS_WIDTH,
            height=CANVAS_HEIGHT,
            background="#f8f5ee",
            highlightthickness=1,
            highlightbackground="#cabfae",
        )
        self.canvas.grid(row=0, column=0, sticky="nsew")

        info_panel = ttk.LabelFrame(left, text="Acquisition", padding=10)
        info_panel.grid(row=1, column=0, sticky="ew", pady=(10, 0))
        ttk.Label(info_panel, textvariable=self.frame_info_var, font=("Consolas", 10)).pack(anchor="w")
        ttk.Label(info_panel, textvariable=self.config_info_var, font=("Consolas", 10)).pack(anchor="w", pady=(6, 0))

        conn_panel = ttk.LabelFrame(right, text="Connection", padding=10)
        conn_panel.grid(row=0, column=0, sticky="ew")
        conn_panel.columnconfigure(1, weight=1)
        ttk.Label(conn_panel, text="Host").grid(row=0, column=0, sticky="w")
        ttk.Entry(conn_panel, textvariable=self.host_var).grid(row=0, column=1, sticky="ew", padx=(8, 0))
        ttk.Label(conn_panel, text="Port").grid(row=1, column=0, sticky="w", pady=(8, 0))
        ttk.Entry(conn_panel, textvariable=self.port_var).grid(row=1, column=1, sticky="ew", padx=(8, 0), pady=(8, 0))
        ttk.Button(conn_panel, text="Connect", command=self.connect).grid(row=2, column=0, sticky="ew", pady=(10, 0))
        ttk.Button(conn_panel, text="Disconnect", command=self.disconnect).grid(row=2, column=1, sticky="ew", padx=(8, 0), pady=(10, 0))

        control_panel = ttk.LabelFrame(right, text="Source / Scope Controls", padding=10)
        control_panel.grid(row=1, column=0, sticky="ew", pady=(10, 0))
        control_panel.columnconfigure(1, weight=1)

        self._add_combo(control_panel, "Mode", self.mode_var, ("SOURCE", "ADC"), 0)
        self._add_combo(control_panel, "Wave", self.wave_var, ("SINE", "TRIANGLE", "SQUARE", "SAW"), 1)
        self._add_entry(control_panel, "Freq Hz", self.freq_var, 2)
        self._add_entry(control_panel, "Rate Hz", self.rate_var, 3)
        self._add_entry(control_panel, "Vpp mV", self.vpp_var, 4)
        self._add_entry(control_panel, "Offset mV", self.offset_var, 5)

        ttk.Checkbutton(control_panel, text="Auto refresh", variable=self.auto_refresh_var).grid(
            row=6, column=0, columnspan=2, sticky="w", pady=(8, 0)
        )
        ttk.Button(control_panel, text="Apply", command=self.apply_settings).grid(
            row=7, column=0, sticky="ew", pady=(10, 0)
        )
        ttk.Button(control_panel, text="Preset Default", command=self.reset_preset).grid(
            row=7, column=1, sticky="ew", padx=(8, 0), pady=(10, 0)
        )
        ttk.Button(control_panel, text="Read Config", command=self.refresh_config).grid(
            row=8, column=0, sticky="ew", pady=(8, 0)
        )
        ttk.Button(control_panel, text="Fetch Frame", command=self.fetch_frame_once).grid(
            row=8, column=1, sticky="ew", padx=(8, 0), pady=(8, 0)
        )

        log_panel = ttk.LabelFrame(right, text="Log", padding=10)
        log_panel.grid(row=2, column=0, sticky="nsew", pady=(10, 0))
        right.rowconfigure(2, weight=1)
        log_panel.rowconfigure(0, weight=1)
        log_panel.columnconfigure(0, weight=1)
        self.log_text = tk.Text(
            log_panel,
            height=18,
            wrap="word",
            background="#fffaf1",
            foreground="#2f2a22",
            insertbackground="#2f2a22",
            relief="flat",
        )
        self.log_text.grid(row=0, column=0, sticky="nsew")
        scrollbar = ttk.Scrollbar(log_panel, orient="vertical", command=self.log_text.yview)
        scrollbar.grid(row=0, column=1, sticky="ns")
        self.log_text.configure(yscrollcommand=scrollbar.set)
        self.log("Host app started and waiting for the simulator.")

    def _add_combo(self, parent: ttk.LabelFrame, label: str, variable: tk.StringVar, values: tuple[str, ...], row: int) -> None:
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", pady=(0 if row == 0 else 8, 0))
        ttk.Combobox(parent, textvariable=variable, values=values, state="readonly").grid(
            row=row,
            column=1,
            sticky="ew",
            padx=(8, 0),
            pady=(0 if row == 0 else 8, 0),
        )

    def _add_entry(self, parent: ttk.LabelFrame, label: str, variable: tk.StringVar, row: int) -> None:
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", pady=(8, 0))
        ttk.Entry(parent, textvariable=variable).grid(row=row, column=1, sticky="ew", padx=(8, 0), pady=(8, 0))

    def log(self, message: str) -> None:
        self.log_text.insert("end", message + "\n")
        self.log_text.see("end")

    def connect(self) -> None:
        try:
            host = self.host_var.get().strip() or DEFAULT_HOST
            port = int(self.port_var.get().strip())
            self.client.connect(host, port)
            hello = self.client.request("HELLO")
            self.status_var.set(f"Connected to {host}:{port}")
            self.log(f"Connected: {hello}")
            self.refresh_config()
            self._schedule_polling()
        except Exception as exc:
            self.disconnect()
            messagebox.showerror("Connect Failed", str(exc))

    def disconnect(self) -> None:
        self._cancel_polling()
        self.client.close()
        self.status_var.set("Disconnected")
        self.log("Connection closed.")

    def refresh_config(self) -> None:
        try:
            config = parse_config_line(self.client.request("GET CONFIG"))
            self._apply_config_to_controls(config)
            self.config_info_var.set(
                f"MODE={config.mode}  WAVE={config.waveform}  FREQ={config.freq_hz}  RATE={config.rate_hz}  "
                f"VPP={config.vpp_mv}  OFFSET={config.offset_mv}  COUNT={config.sample_count}"
            )
            self.log("Configuration synchronized.")
        except Exception as exc:
            messagebox.showerror("Read Config Failed", str(exc))

    def apply_settings(self) -> None:
        try:
            config = self._config_from_controls()
            for command in build_set_commands(config):
                response = self.client.request(command)
                if not response.startswith("OK "):
                    raise ValueError(response)
                self.log(response)
            self.refresh_config()
        except Exception as exc:
            messagebox.showerror("Apply Failed", str(exc))

    def reset_preset(self) -> None:
        try:
            response = self.client.request("SET PRESET DEFAULT")
            if not response.startswith("OK "):
                raise ValueError(response)
            self.log(response)
            self.refresh_config()
        except Exception as exc:
            messagebox.showerror("Preset Failed", str(exc))

    def fetch_frame_once(self) -> None:
        try:
            frame = parse_frame_line(self.client.request("GET FRAME"))
            self._draw_frame(frame)
            self._update_frame_info(frame)
        except Exception as exc:
            messagebox.showerror("Fetch Failed", str(exc))

    def _schedule_polling(self) -> None:
        self._cancel_polling()
        self.polling_job = self.root.after(150, self._poll_frame)

    def _cancel_polling(self) -> None:
        if self.polling_job is not None:
            self.root.after_cancel(self.polling_job)
            self.polling_job = None

    def _poll_frame(self) -> None:
        self.polling_job = None
        if not self.client.is_connected:
            return
        try:
            if self.auto_refresh_var.get():
                frame = parse_frame_line(self.client.request("GET FRAME"))
                self._draw_frame(frame)
                self._update_frame_info(frame)
        except Exception as exc:
            self.log(f"Auto refresh stopped: {exc}")
            self.disconnect()
            return
        self._schedule_polling()

    def _draw_scope_background(self) -> None:
        self.canvas.delete("all")
        pad_x = 30
        pad_y = 24
        left = pad_x
        right = CANVAS_WIDTH - pad_x
        top = pad_y
        bottom = CANVAS_HEIGHT - pad_y

        self.canvas.create_rectangle(left, top, right, bottom, outline="#5c5348", width=2)
        for index in range(1, GRID_DIV_X):
            x = left + ((right - left) * index / GRID_DIV_X)
            self.canvas.create_line(x, top, x, bottom, fill="#d8cfbe")
        for index in range(1, GRID_DIV_Y):
            y = top + ((bottom - top) * index / GRID_DIV_Y)
            self.canvas.create_line(left, y, right, y, fill="#d8cfbe")

        mid_x = left + (right - left) / 2
        mid_y = top + (bottom - top) / 2
        self.canvas.create_line(mid_x, top, mid_x, bottom, fill="#9b8d78", dash=(4, 4))
        self.canvas.create_line(left, mid_y, right, mid_y, fill="#9b8d78", dash=(4, 4))
        self.canvas.create_text(left + 6, top + 8, text="Signal View", anchor="w", fill="#7f7260", font=("Segoe UI", 10, "bold"))

    def _draw_frame(self, frame: ScopeFrame) -> None:
        self._draw_scope_background()
        if len(frame.samples) < 2:
            return

        pad_x = 30
        pad_y = 24
        left = pad_x
        right = CANVAS_WIDTH - pad_x
        top = pad_y
        bottom = CANVAS_HEIGHT - pad_y

        minimum = min(frame.samples)
        maximum = max(frame.samples)
        amplitude = max(1, maximum - minimum)
        points: list[float] = []

        for index, sample in enumerate(frame.samples):
            x = left + ((right - left) * index / max(1, len(frame.samples) - 1))
            normalized = (sample - minimum) / amplitude
            y = bottom - normalized * (bottom - top)
            points.extend((x, y))

        self.canvas.create_line(*points, fill="#137f78", width=2.5, smooth=False)
        self.canvas.create_text(
            right - 6,
            top + 8,
            anchor="e",
            text=f"{frame.mode} / {frame.waveform}",
            fill="#7f7260",
            font=("Segoe UI", 10, "bold"),
        )

    def _update_frame_info(self, frame: ScopeFrame) -> None:
        self.frame_info_var.set(
            f"FRAME  MODE={frame.mode}  WAVE={frame.waveform}  COUNT={frame.count}  "
            f"RATE={frame.rate_hz}Hz  MIN={frame.minimum}  MAX={frame.maximum}"
        )

    def _apply_config_to_controls(self, config: ScopeConfig) -> None:
        self.mode_var.set(config.mode)
        self.wave_var.set(config.waveform)
        self.freq_var.set(str(config.freq_hz))
        self.rate_var.set(str(config.rate_hz))
        self.vpp_var.set(str(config.vpp_mv))
        self.offset_var.set(str(config.offset_mv))

    def _config_from_controls(self) -> ScopeConfig:
        return ScopeConfig(
            mode=self.mode_var.get().strip().upper(),
            waveform=self.wave_var.get().strip().upper(),
            freq_hz=int(self.freq_var.get().strip()),
            rate_hz=int(self.rate_var.get().strip()),
            vpp_mv=int(self.vpp_var.get().strip()),
            offset_mv=int(self.offset_var.get().strip()),
            sample_count=640,
        )

    def _on_close(self) -> None:
        self.disconnect()
        self.root.destroy()


def main() -> None:
    root = tk.Tk()
    app = ScopeHostApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
