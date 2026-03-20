from __future__ import annotations
import numpy as np
from constants import LED_COUNT

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
