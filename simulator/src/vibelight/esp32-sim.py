"""
esp32_led_sim.py

ESP32 LED simulator — peak-to-peak audio sampling with pluggable LED mapping functions.
Window duration is the primary control; sample count derives from it and the simulated rate.
"""

import abc
import time
import queue
from collections import deque

import numpy as np
import pygame
import sounddevice as sd

# Optional system-audio loopback capture (Windows/Mac) via `soundcard`.
try:
    import soundcard as sc  # type: ignore
    HAS_SOUNDCARD = True
except Exception:
    HAS_SOUNDCARD = False

from auto_pipeline import AutoPipeline

# --- Hardware simulation constants ---
SIMULATED_SAMPLE_RATE = 20_000
DEVICE_SAMPLE_RATE = 44_100

# --- Display constants ---
WINDOW_WIDTH = 1200
WINDOW_HEIGHT = 800
LED_COUNT = 8
FPS = 60
LED_COLOR_ON = (255, 255, 0)
LED_COLOR_OFF = (30, 30, 30)
BORDER_COLOR = (80, 80, 80)

# --- Layout ---
PLOT_HEIGHT = 200
PLOT_Y = 10
LEVEL_PLOT_HEIGHT = 120
LEVEL_PLOT_Y = PLOT_Y + PLOT_HEIGHT + 10
SLIDER_HEIGHT = 16
SLIDER_WIDTH = 280
LED_HEIGHT = 100
PLOT_HISTORY = 200


# ──────────────────────────────────────────────
# Sampling (manual mode)
# ──────────────────────────────────────────────

class PeakToPeakSampler:
    def __init__(self, double_window: bool = False):
        self.double_window = double_window
        self._prev_min: float | None = None
        self._prev_max: float | None = None

    def feed(self, samples: np.ndarray) -> float:
        cur_min = float(np.min(samples))
        cur_max = float(np.max(samples))
        if self.double_window and self._prev_min is not None:
            p2p = max(cur_max, self._prev_max) - min(cur_min, self._prev_min)
        else:
            p2p = cur_max - cur_min
        self._prev_min = cur_min
        self._prev_max = cur_max
        return p2p


def p2p_to_level(p2p: float, floor: float, ceiling: float) -> int:
    if ceiling <= floor:
        return 0
    normalized = (p2p - floor) / (ceiling - floor)
    normalized = max(0.0, min(1.0, normalized))
    return round(normalized * LED_COUNT)


# ──────────────────────────────────────────────
# LED mapping functions
# ──────────────────────────────────────────────

class LEDMapper(abc.ABC):
    @abc.abstractmethod
    def __call__(self, level: int) -> list[bool]:
        ...


class VUMeterMapper(LEDMapper):
    def __call__(self, level: int) -> list[bool]:
        return [i < level for i in range(LED_COUNT)]


class PeakHoldMapper(LEDMapper):
    def __init__(self, hold_frames: int = 30):
        self.hold_frames = hold_frames
        self._peak = 0
        self._timer = 0

    def __call__(self, level: int) -> list[bool]:
        if level >= self._peak:
            self._peak = level
            self._timer = self.hold_frames
        elif self._timer > 0:
            self._timer -= 1
        else:
            self._peak = max(self._peak - 1, 0)
        leds = [i < level for i in range(LED_COUNT)]
        if 0 < self._peak <= LED_COUNT:
            leds[self._peak - 1] = True
        return leds


class CenterOutMapper(LEDMapper):
    def __call__(self, level: int) -> list[bool]:
        leds = [False] * LED_COUNT
        half = level / 2
        mid = LED_COUNT / 2
        for i in range(LED_COUNT):
            if mid - half <= i < mid + half:
                leds[i] = True
        return leds


class DecayPeakMapper(LEDMapper):
    def __init__(self, decay_frames: int = 4):
        self.decay_frames = decay_frames
        self._display_level = 0
        self._frame_counter = 0

    def __call__(self, level: int) -> list[bool]:
        if level > self._display_level:
            self._display_level = level
            self._frame_counter = 0
        else:
            self._frame_counter += 1
            if self._frame_counter >= self.decay_frames:
                self._display_level = max(self._display_level - 1, 0)
                self._frame_counter = 0
        return [i < self._display_level for i in range(LED_COUNT)]


class PeakFlashMapper(LEDMapper):
    def __init__(self, peak_threshold: int = 7, num_channels: int = 3):
        self.peak_threshold = peak_threshold
        self.num_channels = min(num_channels, LED_COUNT)
        self._prev_level = 0
        self._pattern: list[bool] = [False] * LED_COUNT

    def __call__(self, level: int) -> list[bool]:
        is_peak = level >= self.peak_threshold and self._prev_level < self.peak_threshold
        self._prev_level = level
        if is_peak:
            indices = np.random.choice(LED_COUNT, size=self.num_channels, replace=False)
            self._pattern = [i in indices for i in range(LED_COUNT)]
        return self._pattern


class SwapFlashMapper(LEDMapper):
    def __init__(self, peak_threshold: int = 7):
        self.peak_threshold = peak_threshold
        self._prev_level = 0
        self._active: list[int] = []

    def __call__(self, level: int) -> list[bool]:
        is_peak = level >= self.peak_threshold and self._prev_level < self.peak_threshold
        self._prev_level = level
        if is_peak:
            if len(self._active) == 3:
                keep = self._active[np.random.randint(3)]
                available = [i for i in range(LED_COUNT) if i != keep]
                new_two = list(np.random.choice(available, size=2, replace=False))
                self._active = sorted([keep] + new_two)
            else:
                self._active = sorted(np.random.choice(LED_COUNT, size=3, replace=False))
        return [i in self._active for i in range(LED_COUNT)]


class AdaptiveSwapMapper(LEDMapper):
    def __init__(self, peak_threshold: int = 7, quiet_threshold: int = 4, quiet_timeout: float = 0.6):
        self.peak_threshold = peak_threshold
        self.quiet_threshold = quiet_threshold
        self.quiet_timeout = quiet_timeout
        self._prev_level = 0
        self._active: list[int] = []
        self._last_high_time: float = time.time()
        self._quiet_mode = False

    def _swap_two(self):
        if len(self._active) == 3:
            keep = self._active[np.random.randint(3)]
            available = [i for i in range(LED_COUNT) if i != keep]
            new_two = list(np.random.choice(available, size=2, replace=False))
            self._active = sorted([keep] + new_two)
        else:
            self._active = sorted(np.random.choice(LED_COUNT, size=3, replace=False))

    def __call__(self, level: int) -> list[bool]:
        now = time.time()
        is_high_peak = level >= self.peak_threshold and self._prev_level < self.peak_threshold
        is_quiet_flank = level <= self.quiet_threshold and level > self._prev_level

        if is_high_peak:
            self._quiet_mode = False
            self._last_high_time = now
            self._swap_two()
        elif not self._quiet_mode and (now - self._last_high_time) >= self.quiet_timeout:
            self._quiet_mode = True
            self._active = [int(np.random.randint(LED_COUNT))]
        elif self._quiet_mode and is_quiet_flank:
            self._active = [int(np.random.randint(LED_COUNT))]

        self._prev_level = level
        return [i in self._active for i in range(LED_COUNT)]


# ──────────────────────────────────────────────
# Audio stream
# ──────────────────────────────────────────────

class AudioCapture:
    def __init__(self, window_ms: float, device: int | None = None):
        self._queue: queue.Queue[np.ndarray] = queue.Queue(maxsize=8)
        self._stream: sd.InputStream | None = None
        self._window_ms = window_ms
        self._device_block = int(DEVICE_SAMPLE_RATE * window_ms / 1000)
        self._device = device

    @property
    def simulated_samples(self) -> int:
        return int(SIMULATED_SAMPLE_RATE * self._window_ms / 1000)

    def _callback(self, indata, frames, time_info, status):
        if status:
            print(f"audio: {status}")
        try:
            self._queue.put_nowait(indata[:, 0].copy())
        except queue.Full:
            pass

    def start(self):
        self._stream = sd.InputStream(
            callback=self._callback,
            channels=1,
            samplerate=DEVICE_SAMPLE_RATE,
            blocksize=self._device_block,
            device=self._device,
        )
        self._stream.start()

    def get_window(self) -> np.ndarray | None:
        try:
            chunk = self._queue.get_nowait()
        except queue.Empty:
            return None
        indices = np.linspace(0, len(chunk) - 1, self.simulated_samples, dtype=int)
        return chunk[indices]

    def stop(self):
        if self._stream:
            self._stream.stop()
            self._stream.close()

# ──────────────────────────────────────────────
# System loopback capture (optional)
# ──────────────────────────────────────────────

class SystemLoopbackCapture:
    """Capture system output (speaker) audio via `soundcard` loopback.

    Useful for visualizing browser/app audio without a microphone.
    """

    def __init__(self, window_ms: float):
        self._window_ms = window_ms
        self._device_block = int(DEVICE_SAMPLE_RATE * window_ms / 1000)
        self._recorder: any = None

    @property
    def simulated_samples(self) -> int:
        return int(SIMULATED_SAMPLE_RATE * self._window_ms / 1000)

    def start(self):
        if not HAS_SOUNDCARD:
            raise RuntimeError("soundcard package not available; install to use loopback")
        speaker = sc.default_speaker()
        self._recorder = speaker.recorder(samplerate=DEVICE_SAMPLE_RATE)

    def get_window(self) -> np.ndarray | None:
        if not self._recorder:
            return None
        try:
            chunk = self._recorder.record(self._device_block)
        except Exception:
            return None
        if chunk is None or len(chunk) == 0:
            return None
        # use first channel if stereo
        sig = chunk[:, 0].copy() if chunk.ndim == 2 else chunk.copy()
        indices = np.linspace(0, len(sig) - 1, self.simulated_samples, dtype=int)
        return sig[indices]

    def stop(self):
        if self._recorder:
            try:
                self._recorder.close()
            finally:
                self._recorder = None


# ──────────────────────────────────────────────
# UI Slider
# ──────────────────────────────────────────────

class Slider:
    def __init__(self, x: int, y: int, w: int, h: int, val: float, lo: float, hi: float, label: str):
        self.rect = pygame.Rect(x, y, w, h)
        self.lo = lo
        self.hi = hi
        self.val = val
        self.label = label
        self.dragging = False

    def _val_to_x(self) -> int:
        t = (self.val - self.lo) / (self.hi - self.lo)
        return int(self.rect.x + t * self.rect.w)

    def handle_event(self, event: pygame.event.Event):
        if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
            knob_x = self._val_to_x()
            knob_rect = pygame.Rect(knob_x - 8, self.rect.y - 4, 16, self.rect.h + 8)
            if knob_rect.collidepoint(event.pos) or self.rect.collidepoint(event.pos):
                self.dragging = True
                self._update_from_mouse(event.pos[0])
        elif event.type == pygame.MOUSEBUTTONUP:
            self.dragging = False
        elif event.type == pygame.MOUSEMOTION and self.dragging:
            self._update_from_mouse(event.pos[0])

    def _update_from_mouse(self, mx: int):
        t = (mx - self.rect.x) / self.rect.w
        t = max(0.0, min(1.0, t))
        self.val = self.lo + t * (self.hi - self.lo)

    def draw(self, screen: pygame.Surface, font: pygame.font.Font):
        pygame.draw.rect(screen, (60, 60, 60), self.rect)
        knob_x = self._val_to_x()
        filled = pygame.Rect(self.rect.x, self.rect.y, knob_x - self.rect.x, self.rect.h)
        pygame.draw.rect(screen, (100, 100, 100), filled)
        pygame.draw.circle(screen, (220, 220, 220), (knob_x, self.rect.centery), 7)
        txt = f"{self.label}: {self.val:.3f}"
        surf = font.render(txt, True, (180, 180, 180))
        screen.blit(surf, (self.rect.right + 8, self.rect.y - 2))


# ──────────────────────────────────────────────
# Renderer
# ──────────────────────────────────────────────

class LEDRenderer:
    def __init__(self):
        pygame.init()
        # Fixed-size window to keep layout consistent
        self.screen = pygame.display.set_mode((WINDOW_WIDTH, WINDOW_HEIGHT))
        pygame.display.set_caption("ESP32 LED Sim")
        self.clock = pygame.time.Clock()
        pygame.font.init()
        self.font = pygame.font.Font(None, 22)

        self.led_w = 50
        self.spacing = (WINDOW_WIDTH - LED_COUNT * self.led_w) // (LED_COUNT + 1)
        # Leave more space for info text at the bottom
        self.led_y_base = WINDOW_HEIGHT - 120

        self.p2p_history: deque[float] = deque(maxlen=PLOT_HISTORY)
        self.level_history: deque[int] = deque(maxlen=PLOT_HISTORY)

        # --- Slider layout ---
        row_h = 28
        col1_x = 20
        col2_x = 620

        # Sliders start below the level plot
        sy = LEVEL_PLOT_Y + LEVEL_PLOT_HEIGHT + 12
        # Manual sliders (left column, top)
        self.floor_slider = Slider(col1_x, sy, SLIDER_WIDTH, SLIDER_HEIGHT, 0.01, 0.0, 0.5, "Floor")
        self.ceil_slider = Slider(col1_x, sy + row_h, SLIDER_WIDTH, SLIDER_HEIGHT, 0.3, 0.01, 1.0, "Ceiling")

        # Auto sliders — gate (left column, below manual)
        ay = sy + row_h * 3
        self.gate_headroom_slider = Slider(col1_x, ay, SLIDER_WIDTH, SLIDER_HEIGHT, 1.5, 1.0, 5.0, "Gate Headroom")
        self.gate_hold_slider = Slider(col1_x, ay + row_h, SLIDER_WIDTH, SLIDER_HEIGHT, 6, 1, 30, "Gate Hold (frames)")
        self.amb_attack_slider = Slider(col1_x, ay + row_h * 2, SLIDER_WIDTH, SLIDER_HEIGHT, 0.02, 0.001, 0.2, "Amb Attack")
        self.amb_release_slider = Slider(col1_x, ay + row_h * 3, SLIDER_WIDTH, SLIDER_HEIGHT, 0.005, 0.001, 0.1, "Amb Release")

        # Auto sliders — calibration (right column, top)
        self.pct_low_slider = Slider(col2_x, sy, SLIDER_WIDTH, SLIDER_HEIGHT, 30, 5, 60, "Pct Low")
        self.pct_high_slider = Slider(col2_x, sy + row_h, SLIDER_WIDTH, SLIDER_HEIGHT, 95, 70, 99, "Pct High")
        self.hist_seconds_slider = Slider(col2_x, sy + row_h * 2, SLIDER_WIDTH, SLIDER_HEIGHT, 5.0, 1.0, 20.0, "History (s)")

        # Auto sliders — compressor (right column, below calibration)
        cy = sy + row_h * 4
        self.comp_thresh_slider = Slider(col2_x, cy, SLIDER_WIDTH, SLIDER_HEIGHT, 3, 0, 7, "Comp Thresh")
        self.comp_ratio_slider = Slider(col2_x, cy + row_h, SLIDER_WIDTH, SLIDER_HEIGHT, 3.0, 1.0, 10.0, "Comp Ratio")
        self.comp_makeup_slider = Slider(col2_x, cy + row_h * 2, SLIDER_WIDTH, SLIDER_HEIGHT, 1.5, 0.5, 4.0, "Comp Makeup")

        self.manual_sliders = [self.floor_slider, self.ceil_slider]
        self.auto_sliders = [
            self.gate_headroom_slider, self.gate_hold_slider,
            self.amb_attack_slider, self.amb_release_slider,
            self.pct_low_slider, self.pct_high_slider, self.hist_seconds_slider,
            self.comp_thresh_slider, self.comp_ratio_slider, self.comp_makeup_slider,
        ]

    def sync_auto_params(self, pipeline: AutoPipeline):
        """Push slider values into the live auto pipeline."""
        pipeline.sampler.percentile_low = self.pct_low_slider.val
        pipeline.sampler.percentile_high = self.pct_high_slider.val
        pipeline.sampler._history = deque(
            pipeline.sampler._history,
            maxlen=max(int(self.hist_seconds_slider.val * 66), 30),
        )
        pipeline.gate.gate_headroom = self.gate_headroom_slider.val
        pipeline.gate.hold_frames = int(self.gate_hold_slider.val)
        pipeline.gate.ambient_attack = self.amb_attack_slider.val
        pipeline.gate.ambient_release = self.amb_release_slider.val
        if pipeline.compressor:
            pipeline.compressor.threshold = int(self.comp_thresh_slider.val)
            pipeline.compressor.ratio = self.comp_ratio_slider.val
            pipeline.compressor.makeup_gain = self.comp_makeup_slider.val

    def _draw_plot(self, floor: float, ceiling: float, ambient: float | None, gate_open: bool | None):
        plot_rect = pygame.Rect(20, PLOT_Y, WINDOW_WIDTH - 40, PLOT_HEIGHT)
        pygame.draw.rect(self.screen, (20, 20, 20), plot_rect)
        pygame.draw.rect(self.screen, (60, 60, 60), plot_rect, 1)

        max_plot = max(self.ceil_slider.hi, 0.1)

        def val_to_y(v: float) -> int:
            t = min(v / max_plot, 1.0)
            return int(plot_rect.bottom - t * plot_rect.h)

        # floor (blue)
        fy = val_to_y(floor)
        pygame.draw.line(self.screen, (80, 80, 255), (plot_rect.left, fy), (plot_rect.right, fy), 1)
        self.screen.blit(self.font.render("floor", True, (80, 80, 255)), (plot_rect.right - 38, fy - 14))

        # ceiling (red)
        cy = val_to_y(ceiling)
        pygame.draw.line(self.screen, (255, 80, 80), (plot_rect.left, cy), (plot_rect.right, cy), 1)
        self.screen.blit(self.font.render("ceil", True, (255, 80, 80)), (plot_rect.right - 32, cy + 2))

        # ambient (green, auto only)
        if ambient is not None:
            ay = val_to_y(ambient)
            color = (0, 200, 0) if gate_open else (0, 100, 0)
            pygame.draw.line(self.screen, color, (plot_rect.left, ay), (plot_rect.right, ay), 1)
            label = "amb OPEN" if gate_open else "amb GATE"
            self.screen.blit(self.font.render(label, True, color), (plot_rect.left + 5, ay - 14))

            # gate threshold line (ambient * headroom, dashed-ish)
            gate_thresh = ambient * self.gate_headroom_slider.val
            gy = val_to_y(gate_thresh)
            for x in range(plot_rect.left, plot_rect.right, 8):
                pygame.draw.line(self.screen, (0, 150, 0), (x, gy), (min(x + 4, plot_rect.right), gy), 1)

        # signal trace (yellow)
        if len(self.p2p_history) >= 2:
            n = len(self.p2p_history)
            step_x = plot_rect.w / PLOT_HISTORY
            points = []
            for i, val in enumerate(self.p2p_history):
                x = plot_rect.left + int((PLOT_HISTORY - n + i) * step_x)
                y = val_to_y(val)
                points.append((x, y))
            pygame.draw.lines(self.screen, (255, 255, 0), False, points, 2)

    def _draw_level_plot(self):
        rect = pygame.Rect(20, LEVEL_PLOT_Y, WINDOW_WIDTH - 40, LEVEL_PLOT_HEIGHT)
        pygame.draw.rect(self.screen, (18, 18, 18), rect)
        pygame.draw.rect(self.screen, (60, 60, 60), rect, 1)

        # y scale: 0..LED_COUNT
        def lvl_to_y(lv: int) -> int:
            t = min(max(lv / LED_COUNT, 0.0), 1.0)
            return int(rect.bottom - t * rect.h)

        # grid lines for levels
        for lv in range(0, LED_COUNT + 1):
            y = lvl_to_y(lv)
            color = (40, 40, 40) if lv % 2 == 0 else (30, 30, 30)
            pygame.draw.line(self.screen, color, (rect.left, y), (rect.right, y), 1)

        # level history line (cyan)
        if len(self.level_history) >= 2:
            n = len(self.level_history)
            step_x = rect.w / PLOT_HISTORY
            pts = []
            for i, lv in enumerate(self.level_history):
                x = rect.left + int((PLOT_HISTORY - n + i) * step_x)
                y = lvl_to_y(int(lv))
                pts.append((x, y))
            pygame.draw.lines(self.screen, (80, 200, 220), False, pts, 2)

    def draw(
        self,
        leds: list[bool],
        level: int,
        p2p: float,
        double_window: bool,
        mapper_name: str,
        window_ms: float,
        sample_count: int,
        loop_ms: float,
        use_auto: bool,
        ambient: float | None = None,
        gate_open: bool | None = None,
    ):
        self.screen.fill((0, 0, 0))

        self.p2p_history.append(p2p)
        self.level_history.append(level)
        self._draw_plot(self.floor_slider.val, self.ceil_slider.val, ambient, gate_open)
        self._draw_level_plot()
        # sliders
        if use_auto:
            # show floor/ceil as read-only trackers
            self.floor_slider.draw(self.screen, self.font)
            self.ceil_slider.draw(self.screen, self.font)
            for s in self.auto_sliders:
                s.draw(self.screen, self.font)
        else:
            self.floor_slider.draw(self.screen, self.font)
            self.ceil_slider.draw(self.screen, self.font)

        # LEDs
        for i, on in enumerate(leds):
            x = self.spacing + i * (self.led_w + self.spacing)
            y = self.led_y_base - LED_HEIGHT
            color = LED_COLOR_ON if on else LED_COLOR_OFF
            pygame.draw.rect(self.screen, color, (x, y, self.led_w, LED_HEIGHT))
            pygame.draw.rect(self.screen, BORDER_COLOR, (x, y, self.led_w, LED_HEIGHT), 2)

        # info
        loop_hz = 1000 / loop_ms if loop_ms > 0 else 0
        mode_str = "AUTO" if use_auto else "MANUAL"
        lines = [
            f"Level: {level}/8  |  p2p: {p2p:.4f}  |  Window: {window_ms:.1f} ms [{sample_count} smp]  |  "
            f"Mode: {'dbl' if double_window else 'sgl'} [W]",
            f"Mapper: {mapper_name} [M]  |  {mode_str} [A]  |  Loop: {loop_ms:.1f} ms ({loop_hz:.0f} Hz)",
        ]
        info_y = self.led_y_base + 4
        for i, line in enumerate(lines):
            surf = self.font.render(line, True, (180, 180, 180))
            self.screen.blit(surf, (10, info_y + i * 20))

        pygame.display.flip()

    def pump_events(self, use_auto: bool) -> dict[str, bool]:
        actions = {"quit": False, "toggle_w": False, "toggle_m": False, "toggle_a": False, "toggle_i": False}
        actions["cycle_d"] = False
        for ev in pygame.event.get():
            if ev.type == pygame.QUIT:
                actions["quit"] = True
            elif ev.type == pygame.KEYDOWN:
                if ev.key == pygame.K_ESCAPE:
                    actions["quit"] = True
                elif ev.key == pygame.K_w:
                    actions["toggle_w"] = True
                elif ev.key == pygame.K_m:
                    actions["toggle_m"] = True
                elif ev.key == pygame.K_a:
                    actions["toggle_a"] = True
                elif ev.key == pygame.K_i:
                    actions["toggle_i"] = True
                elif ev.key == pygame.K_d:
                    actions["cycle_d"] = True

            # route events to active sliders
            if use_auto:
                for s in self.auto_sliders:
                    s.handle_event(ev)
                # floor/ceil are read-only in auto mode, don't handle events
            else:
                for s in self.manual_sliders:
                    s.handle_event(ev)

        return actions

    def tick(self):
        self.clock.tick(FPS)

    def close(self):
        pygame.quit()


# ──────────────────────────────────────────────
# Main loop
# ──────────────────────────────────────────────

def main(window_ms: float = 15.0):
    mappers: list[LEDMapper] = [
        VUMeterMapper(), PeakHoldMapper(), CenterOutMapper(),
        DecayPeakMapper(), PeakFlashMapper(), SwapFlashMapper(), AdaptiveSwapMapper(),
    ]
    mapper_names = [
        "VU Meter", "Peak Hold", "Center Out",
        "Decay Peak", "Peak Flash", "Swap Flash", "Adaptive Swap",
    ]
    mapper_idx = 0

    double_window = False
    manual_sampler = PeakToPeakSampler(double_window=double_window)
    auto_pipeline = AutoPipeline(double_window=double_window)
    use_auto = False
    input_source = "MIC"

    # Discover input (microphone) devices
    def list_input_devices() -> list[dict]:
        try:
            devs = sd.query_devices()
        except Exception:
            devs = []
        return [d for d in devs if isinstance(d, dict) and d.get("max_input_channels", 0) > 0]

    input_devices = list_input_devices()
    device_idx: int | None = None

    audio = AudioCapture(window_ms=window_ms, device=device_idx)
    renderer = LEDRenderer()
    level = 0
    leds = [False] * LED_COUNT
    loop_ms = 0.0
    current_p2p = 0.0

    try:
        audio.start()
        print(
            f"Running — window: {window_ms} ms ({audio.simulated_samples} samples @ "
            f"{SIMULATED_SAMPLE_RATE / 1000:.0f} kHz)"
        )
        print("W: double window | M: cycle mapper | A: auto/manual | ESC: quit")

        running = True
        while running:
            t_start = time.perf_counter()

            actions = renderer.pump_events(use_auto)
            if actions["quit"]:
                break

            if actions["toggle_a"]:
                use_auto = not use_auto
                print(f"Mode: {'AUTO' if use_auto else 'MANUAL'}")

            if actions["toggle_w"]:
                double_window = not double_window
                manual_sampler = PeakToPeakSampler(double_window=double_window)
                auto_pipeline = AutoPipeline(double_window=double_window)
                print(f"Window: {'double' if double_window else 'single'}")

            if actions["toggle_m"]:
                mapper_idx = (mapper_idx + 1) % len(mappers)
                print(f"Mapper: {mapper_names[mapper_idx]}")

            # input source toggle (MIC vs LOOPBACK)
            if actions.get("toggle_i"):
                try:
                    audio.stop()
                except Exception:
                    pass
                if input_source == "MIC":
                    if HAS_SOUNDCARD:
                        audio = SystemLoopbackCapture(window_ms=window_ms)
                        input_source = "LOOPBACK"
                        print("Source: LOOPBACK (system output)")
                    else:
                        print("Loopback requires 'soundcard'; staying on MIC")
                        audio = AudioCapture(window_ms=window_ms, device=device_idx)
                        input_source = "MIC"
                else:
                    audio = AudioCapture(window_ms=window_ms, device=device_idx)
                    input_source = "MIC"
                    print("Source: MIC (microphone)")
                try:
                    audio.start()
                except Exception as e:
                    print(f"Failed to start audio source: {e}")

            # cycle microphone device (if available) — press 'D'
            if actions.get("cycle_d") and input_source == "MIC":
                try:
                    input_devices = list_input_devices()
                    if input_devices:
                        if device_idx is None:
                            device_idx = 0
                        else:
                            device_idx = (device_idx + 1) % len(input_devices)
                        dev = input_devices[device_idx]
                        name = dev.get("name", f"idx {device_idx}")
                        print(f"Mic device: {name} (index {device_idx})")
                        try:
                            audio.stop()
                        except Exception:
                            pass
                        audio = AudioCapture(window_ms=window_ms, device=device_idx)
                        try:
                            audio.start()
                        except Exception as e:
                            print(f"Failed to start selected device: {e}")
                    else:
                        print("No input devices found")
                except Exception as e:
                    print(f"Device switch error: {e}")

            # push slider values into auto pipeline every frame
            if use_auto:
                renderer.sync_auto_params(auto_pipeline)

            window = audio.get_window()
            if window is not None:
                if use_auto:
                    level = auto_pipeline.feed(window)
                    current_p2p = auto_pipeline.raw_p2p
                    renderer.floor_slider.val = auto_pipeline.floor
                    renderer.ceil_slider.val = auto_pipeline.ceiling
                else:
                    current_p2p = manual_sampler.feed(window)
                    level = p2p_to_level(current_p2p, renderer.floor_slider.val, renderer.ceil_slider.val)
                leds = mappers[mapper_idx](level)

            renderer.draw(
                leds, level, current_p2p, double_window, mapper_names[mapper_idx],
                window_ms, audio.simulated_samples, loop_ms,
                use_auto,
                ambient=auto_pipeline.ambient if use_auto else None,
                gate_open=auto_pipeline.gate_open if use_auto else None,
            )
            renderer.tick()
            loop_ms = (time.perf_counter() - t_start) * 1000

    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        audio.stop()
        renderer.close()


if __name__ == "__main__":
    # %% Run simulator
    main(window_ms=15.0)