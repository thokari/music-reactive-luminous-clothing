from __future__ import annotations
import pygame
from collections import deque
from constants import (
    WINDOW_WIDTH, WINDOW_HEIGHT, LED_COUNT, FPS,
    LED_COLOR_ON, LED_COLOR_OFF, BORDER_COLOR,
    PLOT_HEIGHT, PLOT_Y, LEVEL_PLOT_HEIGHT, LEVEL_PLOT_Y,
    SLIDER_HEIGHT, SLIDER_WIDTH, LED_HEIGHT, PLOT_HISTORY,
)

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

class LEDRenderer:
    def __init__(self):
        pygame.init()
        self.screen = pygame.display.set_mode((WINDOW_WIDTH, WINDOW_HEIGHT))
        pygame.display.set_caption("ESP32 LED Sim")
        self.clock = pygame.time.Clock()
        pygame.font.init()
        self.font = pygame.font.Font(None, 22)

        # --- Panel layout ---
        self.left_width = WINDOW_WIDTH // 2
        self.right_width = WINDOW_WIDTH - self.left_width
        self.left_margin = 20
        self.right_margin = 20
        # Hotkeys area height (fixed)
        self.hotkeys_height = 120

        # --- Bottom controls block across full width ---
        row_h = 28
        self.controls_rows = 12  # total slider rows
        self.controls_height = self.controls_rows * row_h + 40
        self.controls_top_y = max(self.hotkeys_height + 160, WINDOW_HEIGHT - self.controls_height)

        # --- LED bars (right side, above controls) ---
        self.led_w = 50
        self.spacing = (self.right_width - LED_COUNT * self.led_w) // (LED_COUNT + 1)
        self.led_area_top = self.hotkeys_height + 10
        self.led_area_bottom = self.controls_top_y - 20
        self.led_y_base = self.led_area_bottom

        self.p2p_history: deque[float] = deque(maxlen=PLOT_HISTORY)
        self.level_history: deque[int] = deque(maxlen=PLOT_HISTORY)

        # --- Slider layout (bottom, full width in two columns) ---
        col1_x = self.left_margin
        col2_x = self.left_margin + (WINDOW_WIDTH // 2) + 20

        # --- Plots (left side), scaled to fit above controls ---
        self._plot_left_x = self.left_margin
        self._plot_width = self.left_width - 2 * self.left_margin
        available_top_height = self.controls_top_y - (self.hotkeys_height + 10)
        base_total = PLOT_HEIGHT + 10 + LEVEL_PLOT_HEIGHT
        scale = min(1.0, max(0.5, (available_top_height - 20) / max(base_total, 1)))
        self.plot_height = int(PLOT_HEIGHT * scale)
        self.level_plot_height = int(LEVEL_PLOT_HEIGHT * scale)
        self._plot_y = self.hotkeys_height + 10
        self._level_plot_y = self._plot_y + self.plot_height + 10
        sy = self.controls_top_y + 12

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

    def sync_auto_params(self, pipeline):
        pipeline.sampler.percentile_low = self.pct_low_slider.val
        pipeline.sampler.percentile_high = self.pct_high_slider.val
        from collections import deque as _deque
        pipeline.sampler._history = _deque(
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
        # Left-side wide plot
        plot_rect = pygame.Rect(self._plot_left_x, self._plot_y, self._plot_width, self.plot_height)
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
        # Left-side level history plot
        rect = pygame.Rect(self._plot_left_x, self._level_plot_y, self._plot_width, self.level_plot_height)
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
    def _draw_hotkeys(self,
                      double_window: bool,
                      mapper_name: str,
                      use_auto: bool,
                      input_source: str,
                      input_device_name: str | None,
                      device_index: int | None):
        # Compose hotkey entries in two columns (left pane top)
        entries = [
            (
                "W",
                "Sample Window Size",
                "Double" if double_window else "Single",
            ),
            (
                "M",
                "Mapping Algorithm",
                mapper_name,
            ),
            (
                "A",
                "Processing Mode",
                "Automatic" if use_auto else "Manual",
            ),
            (
                "I",
                "Input Source",
                "Microphone" if input_source == "MIC" else "System Loopback",
            ),
            (
                "D",
                "Microphone Input Device",
                (
                    f"{input_device_name} (Index {device_index})" if (input_device_name and device_index is not None)
                    else ("No device selected" if input_source == "MIC" else "Device selection unavailable in Loopback mode")
                ),
            ),
        ]

        # Layout
        col_w = (self.left_width - 2 * self.left_margin) // 2
        col1_x = self.left_margin
        col2_x = self.left_margin + col_w + 30
        y = 8
        row_h = 36

        for i, (key, action, state) in enumerate(entries):
            x = col1_x if i < 3 else col2_x
            yy = y + (i % 3) * row_h
            # Key in square brackets
            key_text = f"[{key}]"
            key_surf = self.font.render(key_text, True, (230, 230, 230))
            self.screen.blit(key_surf, (x, yy))
            # Action and state (compact)
            action_surf = self.font.render(action, True, (180, 180, 180))
            state_surf = self.font.render(state, True, (160, 200, 180))
            self.screen.blit(action_surf, (x + 70, yy))
            self.screen.blit(state_surf, (x + 70, yy + 18))

    def draw(self,
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
             input_source: str = "MIC",
             input_device_name: str | None = None,
             device_index: int | None = None):
        self.screen.fill((0, 0, 0))

        # Left side: hotkeys, plots, sliders
        self._draw_hotkeys(double_window, mapper_name, use_auto, input_source, input_device_name, device_index)
        self.p2p_history.append(p2p)
        self.level_history.append(level)
        self._draw_plot(self.floor_slider.val, self.ceil_slider.val, ambient, gate_open)
        self._draw_level_plot()

        # sliders
        if use_auto:
            self.floor_slider.draw(self.screen, self.font)
            self.ceil_slider.draw(self.screen, self.font)
            for s in self.auto_sliders:
                s.draw(self.screen, self.font)
        else:
            self.floor_slider.draw(self.screen, self.font)
            self.ceil_slider.draw(self.screen, self.font)

        # Right side: LED bars only (above controls)
        for i, on in enumerate(leds):
            x = self.left_width + self.spacing + i * (self.led_w + self.spacing)
            y = self.led_y_base - LED_HEIGHT
            color = LED_COLOR_ON if on else LED_COLOR_OFF
            pygame.draw.rect(self.screen, color, (x, y, self.led_w, LED_HEIGHT))
            pygame.draw.rect(self.screen, BORDER_COLOR, (x, y, self.led_w, LED_HEIGHT), 2)

        # (Removed bottom info; status is shown in hotkeys panel)

        pygame.display.flip()

    def pump_events(self, use_auto: bool) -> dict[str, bool]:
        actions: dict[str, bool] = {"quit": False, "toggle_w": False, "toggle_m": False, "toggle_a": False, "toggle_i": False}
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

            if use_auto:
                for s in self.auto_sliders:
                    s.handle_event(ev)
            else:
                for s in self.manual_sliders:
                    s.handle_event(ev)

        return actions

    def tick(self):
        self.clock.tick(FPS)

    def close(self):
        pygame.quit()
