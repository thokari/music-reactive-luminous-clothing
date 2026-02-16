from __future__ import annotations
import abc
import time
import numpy as np
from constants import LED_COUNT

class LEDMapper(abc.ABC):
    @abc.abstractmethod
    def __call__(self, level: int) -> list[bool]:
        ...

class VUMeterMapper(LEDMapper):
    def __call__(self, level: int) -> list[bool]:
        return [i < level for i in range(LED_COUNT)]

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

# Registry to mirror firmware labels without adding timing semantics
class MapperRegistryItem:
    def __init__(self, name: str, mapper: LEDMapper, on_enter: callable | None = None):
        self.name = name
        self.mapper = mapper
        self.on_enter = on_enter

MAPPER_REGISTRY: list[MapperRegistryItem] = [
    MapperRegistryItem("VU Meter", VUMeterMapper()),
    MapperRegistryItem("Decay Peak", DecayPeakMapper()),
    MapperRegistryItem("Peak Flash", PeakFlashMapper()),
    MapperRegistryItem("Swap Flash", SwapFlashMapper()),
    MapperRegistryItem("Adaptive Swap", AdaptiveSwapMapper()),
]
