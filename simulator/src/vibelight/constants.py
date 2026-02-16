# Shared constants for simulator

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
LED_HEIGHT = 120
PLOT_HISTORY = 200

# --- Device defaults (optional) ---
# Set to a substring of your desired input device name (case-insensitive).
# Example: DEFAULT_INPUT_DEVICE_NAME = "Line In (Realtek HD Audio Line input)"
DEFAULT_INPUT_DEVICE_NAME = "Line In (Realtek HD Audio Line input)"
