from __future__ import annotations
import queue
import numpy as np
import sounddevice as sd

# Optional system-audio loopback capture (Windows/Mac) via `soundcard`.
try:
    import soundcard as sc  # type: ignore
    HAS_SOUNDCARD = True
except Exception:
    HAS_SOUNDCARD = False

from constants import DEVICE_SAMPLE_RATE, SIMULATED_SAMPLE_RATE

class AudioCapture:
    def __init__(self, window_ms: float, device: int | None = None):
        self._queue: queue.Queue[np.ndarray] = queue.Queue(maxsize=8)
        self._stream: sd.InputStream | None = None
        self._window_ms = window_ms
        self._device_block = int(DEVICE_SAMPLE_RATE * window_ms / 1000)
        self._device = device
        self._channels = 1

    @property
    def simulated_samples(self) -> int:
        return int(SIMULATED_SAMPLE_RATE * self._window_ms / 1000)

    def _callback(self, indata, frames, time_info, status):
        if status:
            print(f"audio: {status}")
        try:
            # Work with mono; if multi-channel, take first channel
            sig = indata
            if sig.ndim == 2:
                sig = sig[:, 0]
            self._queue.put_nowait(sig.copy())
        except queue.Full:
            pass

    def start(self):
        # Determine channels from device capabilities
        channels_try = [1, 2]
        try:
            if self._device is not None:
                dev_info = sd.query_devices(self._device)
                max_ch = int(dev_info.get("max_input_channels", 1)) if isinstance(dev_info, dict) else 1
                # Prefer 1, but if device requires more, try 2
                channels_try = [1] + ([2] if max_ch >= 2 else [])
        except Exception:
            pass

        last_err: Exception | None = None
        for ch in channels_try:
            try:
                self._stream = sd.InputStream(
                    callback=self._callback,
                    channels=ch,
                    samplerate=DEVICE_SAMPLE_RATE,
                    blocksize=self._device_block,
                    device=self._device,
                )
                self._stream.start()
                self._channels = ch
                return
            except Exception as e:
                last_err = e
                self._stream = None
        # If we get here, opening failed
        raise RuntimeError(f"Failed to open input stream: {last_err}")

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

class SystemLoopbackCapture:
    def __init__(self, window_ms: float, device: int | None = None):
        self._window_ms = window_ms
        self._device_block = int(DEVICE_SAMPLE_RATE * window_ms / 1000)
        self._recorder: any = None
        self._device = device

    @property
    def simulated_samples(self) -> int:
        return int(SIMULATED_SAMPLE_RATE * self._window_ms / 1000)

    def start(self):
        if not HAS_SOUNDCARD:
            raise RuntimeError("soundcard package not available; install to use loopback")
        try:
            if self._device is None:
                speaker = sc.default_speaker()
            else:
                speakers = sc.all_speakers()
                if 0 <= self._device < len(speakers):
                    speaker = speakers[self._device]
                else:
                    speaker = sc.default_speaker()
            mic = sc.get_microphone(id=str(getattr(speaker, "name", speaker)), include_loopback=True)
            self._recorder = mic.recorder(samplerate=DEVICE_SAMPLE_RATE)
        except Exception:
            # Fallback to basic speaker loopback if microphone loopback fails
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
        sig = chunk[:, 0].copy() if chunk.ndim == 2 else chunk.copy()
        indices = np.linspace(0, len(sig) - 1, self.simulated_samples, dtype=int)
        return sig[indices]

    def stop(self):
        if self._recorder:
            try:
                self._recorder.close()
            finally:
                self._recorder = None

# Utilities

def list_input_devices() -> list[dict]:
    try:
        devs = sd.query_devices()
    except Exception:
        devs = []
    return [d for d in devs if isinstance(d, dict) and d.get("max_input_channels", 0) > 0]

def list_output_devices() -> list[dict]:
    if not HAS_SOUNDCARD:
        return []
    devices: list[dict] = []
    try:
        for idx, spk in enumerate(sc.all_speakers()):
            name = getattr(spk, "name", None) or str(spk)
            devices.append({"name": name, "index": idx})
    except Exception:
        pass
    return devices
