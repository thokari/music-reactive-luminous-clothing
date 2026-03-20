"""
auto_pipeline.py

Auto-calibrating, compressing peak-to-peak sampler with adaptive noise gate.
Drop-in alternative to manual floor/ceiling control.
"""

import numpy as np
from collections import deque

LED_COUNT = 8


class AutoCalibratingSampler:
    """Peak-to-peak sampler with running percentile-based auto floor/ceiling.

    Floor tracks the ambient noise level (low percentile of recent history).
    Ceiling tracks the loud transients (high percentile).
    Only signals above the floor produce output — background noise is gated.
    """

    def __init__(
        self,
        double_window: bool = False,
        history_seconds: float = 5.0,
        windows_per_second: float = 66.0,
        percentile_low: float = 30.0,
        percentile_high: float = 95.0,
        min_range: float = 0.01,
    ):
        self.double_window = double_window
        self.percentile_low = percentile_low
        self.percentile_high = percentile_high
        self.min_range = min_range

        history_len = int(history_seconds * windows_per_second)
        self._history: deque[float] = deque(maxlen=max(history_len, 30))

        self._prev_min: float | None = None
        self._prev_max: float | None = None

        self.floor: float = 0.0
        self.ceiling: float = min_range

    def feed(self, samples: np.ndarray) -> float:
        cur_min = float(np.min(samples))
        cur_max = float(np.max(samples))

        if self.double_window and self._prev_min is not None:
            p2p = max(cur_max, self._prev_max) - min(cur_min, self._prev_min)
        else:
            p2p = cur_max - cur_min

        self._prev_min = cur_min
        self._prev_max = cur_max

        self._history.append(p2p)
        self._recalc()
        return p2p

    def _recalc(self):
        if len(self._history) < 10:
            return
        arr = np.array(self._history)
        self.floor = float(np.percentile(arr, self.percentile_low))
        raw_ceil = float(np.percentile(arr, self.percentile_high))
        self.ceiling = max(raw_ceil, self.floor + self.min_range)


class Compressor:
    """Dynamic range compressor operating on level 0–8.

    Levels below threshold pass through. Above threshold, excess is divided
    by ratio. Makeup gain scales the result back up.

    Fast attack preserves transients (beats), slow release prevents pumping.
    """

    def __init__(
        self,
        threshold: int = 3,
        ratio: float = 3.0,
        attack_frames: int = 2,
        release_frames: int = 8,
        makeup_gain: float = 1.5,
    ):
        self.threshold = threshold
        self.ratio = ratio
        self.attack_frames = max(attack_frames, 1)
        self.release_frames = max(release_frames, 1)
        self.makeup_gain = makeup_gain
        self._envelope: float = 0.0

    def process(self, level: int) -> int:
        above = level > self.threshold

        if above:
            self._envelope += (1.0 - self._envelope) / self.attack_frames
        else:
            self._envelope -= self._envelope / self.release_frames
        self._envelope = max(0.0, min(1.0, self._envelope))

        if level <= self.threshold:
            out = float(level)
        else:
            excess = float(level - self.threshold)
            compressed_excess = excess / (1.0 + (self.ratio - 1.0) * self._envelope)
            out = self.threshold + compressed_excess

        out *= self.makeup_gain
        return max(0, min(8, round(out)))


class NoiseGate:
    """Adaptive noise gate that suppresses output during non-musical content.

    Tracks a smoothed ambient level. Signal must exceed ambient by
    `gate_headroom` to open the gate. Gate holds open for `hold_frames`
    after the last transient to avoid choppy behavior.

    In loud/noisy environments, the ambient tracker rises so only beats
    that stand out above the noise floor get through.
    """

    def __init__(
        self,
        gate_headroom: float = 1.5,
        hold_frames: int = 6,
        ambient_attack: float = 0.02,
        ambient_release: float = 0.005,
    ):
        self.gate_headroom = gate_headroom
        self.hold_frames = hold_frames
        self.ambient_attack = ambient_attack
        self.ambient_release = ambient_release

        self._ambient: float = 0.0
        self._hold_counter: int = 0
        self.gate_open: bool = False

    def process(self, p2p: float, level: int) -> int:
        # update ambient tracker (fast rise, slow fall)
        if p2p > self._ambient:
            self._ambient += (p2p - self._ambient) * self.ambient_attack
        else:
            self._ambient += (p2p - self._ambient) * self.ambient_release

        # gate logic: signal must exceed ambient by headroom factor
        if p2p > self._ambient * self.gate_headroom:
            self._hold_counter = self.hold_frames
            self.gate_open = True
        elif self._hold_counter > 0:
            self._hold_counter -= 1
            self.gate_open = True
        else:
            self.gate_open = False

        return level if self.gate_open else 0

    @property
    def ambient(self) -> float:
        return self._ambient


class AutoPipeline:
    """Complete auto pipeline: sample → gate → map → compress.

    Replaces manual PeakToPeakSampler + p2p_to_level.
    """

    def __init__(
        self,
        double_window: bool = False,
        # calibration
        history_seconds: float = 5.0,
        percentile_low: float = 30.0,
        percentile_high: float = 95.0,
        # compressor
        compress: bool = True,
        comp_threshold: int = 3,
        comp_ratio: float = 3.0,
        comp_makeup: float = 1.5,
        # noise gate
        gate_headroom: float = 1.5,
        gate_hold_frames: int = 6,
    ):
        self.sampler = AutoCalibratingSampler(
            double_window=double_window,
            history_seconds=history_seconds,
            percentile_low=percentile_low,
            percentile_high=percentile_high,
        )
        self.gate = NoiseGate(
            gate_headroom=gate_headroom,
            hold_frames=gate_hold_frames,
        )
        self.compressor = Compressor(
            threshold=comp_threshold,
            ratio=comp_ratio,
            makeup_gain=comp_makeup,
        ) if compress else None

        self.raw_p2p: float = 0.0
        self.raw_level: int = 0
        self.final_level: int = 0

    @property
    def floor(self) -> float:
        return self.sampler.floor

    @property
    def ceiling(self) -> float:
        return self.sampler.ceiling

    @property
    def ambient(self) -> float:
        return self.gate.ambient

    @property
    def gate_open(self) -> bool:
        return self.gate.gate_open

    def feed(self, samples: np.ndarray) -> int:
        self.raw_p2p = self.sampler.feed(samples)

        # map to 0–8 using auto floor/ceiling
        span = self.sampler.ceiling - self.sampler.floor
        if span > 0:
            normalized = (self.raw_p2p - self.sampler.floor) / span
            normalized = max(0.0, min(1.0, normalized))
        else:
            normalized = 0.0
        self.raw_level = round(normalized * LED_COUNT)

        level = self.raw_level
        if self.compressor:
            level = self.compressor.process(level)

        level = self.gate.process(self.raw_p2p, level)
        self.final_level = level
        return level