from __future__ import annotations
import time
import numpy as np

from constants import SIMULATED_SAMPLE_RATE, DEFAULT_INPUT_DEVICE_NAME
from sampling import PeakToPeakSampler, p2p_to_level
from mappers import MAPPER_REGISTRY
from audio import AudioCapture, SystemLoopbackCapture, HAS_SOUNDCARD, list_input_devices, list_output_devices
from renderer import LEDRenderer
from constants import LED_COUNT
from auto_pipeline import AutoPipeline


def main(window_ms: float = 14.0):
    mappers = [item.mapper for item in MAPPER_REGISTRY]
    mapper_names = [item.name for item in MAPPER_REGISTRY]
    mapper_idx = 0

    double_window = False
    manual_sampler = PeakToPeakSampler(double_window=double_window)
    auto_pipeline = AutoPipeline(double_window=double_window)
    use_auto = False
    input_source = "MIC"

    input_devices = list_input_devices()
    output_devices = list_output_devices() if HAS_SOUNDCARD else []
    device_idx: int | None = None
    current_device_name: str | None = None

    # Prefer default device name if configured
    if DEFAULT_INPUT_DEVICE_NAME:
        try:
            target = DEFAULT_INPUT_DEVICE_NAME.lower()
            for i, d in enumerate(input_devices):
                name = d.get("name", "")
                if target in name.lower():
                    device_idx = i
                    current_device_name = name
                    print(f"Selected default input device: {name} (index {i})")
                    break
        except Exception:
            pass

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
        print("W: double window | M: cycle mapper | A: auto/manual | I: input source | D: mic device | ESC: quit")

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
                        output_devices = list_output_devices()
                        audio = SystemLoopbackCapture(window_ms=window_ms, device=device_idx)
                        input_source = "LOOPBACK"
                        if device_idx is not None and 0 <= device_idx < len(output_devices):
                            current_device_name = output_devices[device_idx].get("name")
                        print("Source: LOOPBACK (system output)")
                    else:
                        print("Loopback requires 'soundcard'; staying on MIC")
                        audio = AudioCapture(window_ms=window_ms, device=device_idx)
                        input_source = "MIC"
                else:
                    input_devices = list_input_devices()
                    audio = AudioCapture(window_ms=window_ms, device=device_idx)
                    input_source = "MIC"
                    if device_idx is not None and 0 <= device_idx < len(input_devices):
                        current_device_name = input_devices[device_idx].get("name")
                    print("Source: MIC (microphone)")
                try:
                    audio.start()
                except Exception as e:
                    print(f"Failed to start audio source: {e}")


            # cycle device (microphone or loopback) — press 'D'
            if actions.get("cycle_d"):
                try:
                    if input_source == "MIC":
                        input_devices = list_input_devices()
                        if input_devices:
                            device_idx = 0 if device_idx is None else (device_idx + 1) % len(input_devices)
                            dev = input_devices[device_idx]
                            name = dev.get("name", f"idx {device_idx}")
                            print(f"Mic device: {name} (index {device_idx})")
                            current_device_name = name
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
                    else:
                        if HAS_SOUNDCARD:
                            output_devices = list_output_devices()
                            if output_devices:
                                device_idx = 0 if device_idx is None else (device_idx + 1) % len(output_devices)
                                dev = output_devices[device_idx]
                                name = dev.get("name", f"idx {device_idx}")
                                print(f"Loopback device: {name} (index {device_idx})")
                                current_device_name = name
                                try:
                                    audio.stop()
                                except Exception:
                                    pass
                                audio = SystemLoopbackCapture(window_ms=window_ms, device=device_idx)
                                try:
                                    audio.start()
                                except Exception as e:
                                    print(f"Failed to start selected loopback device: {e}")
                            else:
                                print("No output (speaker) devices found")
                        else:
                            print("Loopback requires 'soundcard'; cannot cycle output devices")
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
                input_source=input_source,
                input_device_name=(
                    input_devices[device_idx].get("name") if (
                        input_source == "MIC" and device_idx is not None and 0 <= device_idx < len(input_devices)
                    ) else (
                        output_devices[device_idx].get("name") if (
                            input_source == "LOOPBACK" and device_idx is not None and 0 <= device_idx < len(output_devices)
                        ) else current_device_name
                    )
                ),
                device_index=device_idx,
            )
            renderer.tick()
            loop_ms = (time.perf_counter() - t_start) * 1000

    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        try:
            audio.stop()
        except Exception:
            pass
        renderer.close()

if __name__ == "__main__":
    main(window_ms=15.0)
