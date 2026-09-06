"""ctypes binding to capriceenv.dll, plus a Gymnasium environment wrapping it.

The DLL drives the cap32 (Amstrad CPC) libretro core headlessly: see
src/capriceenv.h for the C API this binds to.
"""
import ctypes
import os

import gymnasium as gym
import numpy as np
from gymnasium import spaces

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
_PROJECT_ROOT = os.path.abspath(os.path.join(_THIS_DIR, ".."))
_DEFAULT_DLL_PATH = os.path.join(_PROJECT_ROOT, "capriceenv.dll")

# RETRO_DEVICE_ID_JOYPAD_* bit positions (include/libretro.h).
_BTN_B = 0
_BTN_UP = 4
_BTN_DOWN = 5
_BTN_LEFT = 6
_BTN_RIGHT = 7
_BTN_A = 8

# RETROK_* (include/libretro.h) mirrors ASCII for printable characters --
# RETROK_SPACE == ord(' ') == 32, RETROK_j == ord('j') == 106 -- so a single
# printable character resolves via ord() with no lookup table needed. Only
# "named" keys with no ASCII equivalent (arrows, function keys, modifiers,
# ...) need entries here.
RETROK_NAMED = {
    "BACKSPACE": 8, "TAB": 9, "RETURN": 13, "PAUSE": 19, "ESCAPE": 27, "DELETE": 127,
    "UP": 273, "DOWN": 274, "RIGHT": 275, "LEFT": 276,
    "INSERT": 277, "HOME": 278, "END": 279, "PAGEUP": 280, "PAGEDOWN": 281,
    "F1": 282, "F2": 283, "F3": 284, "F4": 285, "F5": 286, "F6": 287, "F7": 288,
    "F8": 289, "F9": 290, "F10": 291, "F11": 292, "F12": 293,
    "LSHIFT": 304, "RSHIFT": 303, "LCTRL": 306, "RCTRL": 305, "LALT": 308, "RALT": 307,
}


def key_code(key):
    """Resolves a key spec to a RETROK_* code: a single printable character
    ('j', ' '), a named key ('UP', 'F1', ...), or an already-resolved int."""
    if isinstance(key, int):
        return key
    if len(key) == 1:
        return ord(key)
    name = key.upper()
    if name not in RETROK_NAMED:
        raise KeyError(f"Unknown key: {key!r} (add it to RETROK_NAMED, or see "
                        f"include/libretro.h's retro_key enum for the code)")
    return RETROK_NAMED[name]


# Each action is (name, spec), where spec is None (no-op), ("joypad", bit),
# or ("key", key). Harrier Attack mainly needs a joystick-equivalent plus
# SPACE/J, but this list is just data -- add more ("key", ...) entries for
# any other key, or pass `actions=full_keyboard_actions()` to CapriceGymEnv
# to get every key as its own action instead of hand-picking.
DEFAULT_ACTIONS = [
    ("NOOP", None),
    ("UP", ("joypad", _BTN_UP)),
    ("DOWN", ("joypad", _BTN_DOWN)),
    ("LEFT", ("joypad", _BTN_LEFT)),
    ("RIGHT", ("joypad", _BTN_RIGHT)),
    ("FIRE", ("joypad", _BTN_A)),
    ("FIRE2", ("joypad", _BTN_B)),
    ("SPACE", ("key", " ")),
    ("J", ("key", "j")),
]

ACTION_NAMES = [name for name, _ in DEFAULT_ACTIONS]


def full_keyboard_actions():
    """DEFAULT_ACTIONS plus every remaining printable ASCII key and named key
    as its own action. Pass as `actions=full_keyboard_actions()` to
    CapriceGymEnv for full-keyboard access instead of DEFAULT_ACTIONS' small
    hand-picked set."""
    actions = list(DEFAULT_ACTIONS)
    have = {spec for _, spec in actions}
    for code in range(33, 127):  # Printable ASCII, skipping space (already included).
        spec = ("key", chr(code))
        if spec not in have:
            actions.append((chr(code), spec))
    for name in RETROK_NAMED:
        spec = ("key", name)
        if spec not in have:
            actions.append((name, spec))
    return actions


class CapriceLib:
    """Thin ctypes wrapper around capriceenv.dll's extern "C" API."""

    def __init__(self, dll_path=_DEFAULT_DLL_PATH):
        self.lib = ctypes.CDLL(dll_path)
        lib = self.lib

        lib.ce_init.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
        lib.ce_init.restype = ctypes.c_int

        lib.ce_load_game.argtypes = [ctypes.c_char_p]
        lib.ce_load_game.restype = ctypes.c_int

        lib.ce_set_joypad.argtypes = [ctypes.c_int, ctypes.c_ushort]
        lib.ce_set_joypad.restype = None

        lib.ce_set_key.argtypes = [ctypes.c_int, ctypes.c_int]
        lib.ce_set_key.restype = None

        lib.ce_step.argtypes = []
        lib.ce_step.restype = None

        lib.ce_get_frame_rgb.argtypes = [ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]
        lib.ce_get_frame_rgb.restype = ctypes.POINTER(ctypes.c_ubyte)

        lib.ce_snapshot_size.argtypes = []
        lib.ce_snapshot_size.restype = ctypes.c_size_t

        lib.ce_capture_initial_state.argtypes = []
        lib.ce_capture_initial_state.restype = ctypes.c_int

        lib.ce_reset.argtypes = []
        lib.ce_reset.restype = ctypes.c_int

        lib.ce_shutdown.argtypes = []
        lib.ce_shutdown.restype = None

    def init(self, core_path, system_dir):
        if not self.lib.ce_init(core_path.encode(), system_dir.encode()):
            raise RuntimeError(f"ce_init failed for core={core_path!r} system_dir={system_dir!r}")

    def load_game(self, rom_path):
        if not self.lib.ce_load_game(rom_path.encode()):
            raise RuntimeError(f"ce_load_game failed for rom={rom_path!r}")

    def set_joypad(self, port, buttons):
        self.lib.ce_set_joypad(port, buttons)

    def set_key(self, key, down):
        self.lib.ce_set_key(key_code(key), 1 if down else 0)

    def step(self):
        self.lib.ce_step()

    def get_frame_rgb(self):
        """Returns the last rendered frame as an (H, W, 3) uint8 array, or None."""
        w = ctypes.c_int(0)
        h = ctypes.c_int(0)
        ptr = self.lib.ce_get_frame_rgb(ctypes.byref(w), ctypes.byref(h))
        if not ptr:
            return None
        count = w.value * h.value * 3
        # The DLL owns this buffer and overwrites it on the next ce_step(),
        # so copy it out immediately.
        array_type = ctypes.c_ubyte * count
        frame = np.frombuffer(array_type.from_address(ctypes.addressof(ptr.contents)), dtype=np.uint8)
        return frame.reshape(h.value, w.value, 3).copy()

    def capture_initial_state(self):
        return bool(self.lib.ce_capture_initial_state())

    def reset(self):
        if not self.lib.ce_reset():
            raise RuntimeError("ce_reset failed (no snapshot captured yet?)")

    def shutdown(self):
        self.lib.ce_shutdown()


class CapriceGymEnv(gym.Env):
    """Minimal Gymnasium environment driving the cap32 core.

    No reward signal or episode-termination logic yet (reward is always 0.0,
    terminated/truncated are always False) -- that needs game-specific RAM
    inspection (see retro_get_memory_data) as a follow-up. This env only
    proves out the load -> reset -> step -> observation loop.
    """

    metadata = {"render_modes": ["rgb_array"]}

    def __init__(self, core_path=None, rom_path=None, system_dir=None,
                 warmup_frames=300, actions=None, dll_path=_DEFAULT_DLL_PATH):
        super().__init__()
        self.core_path = core_path or os.path.join(_PROJECT_ROOT, "cores", "cap32_libretro.dll")
        self.rom_path = rom_path or os.path.join(
            _PROJECT_ROOT, "roms", "Harrier Attack (UK) (1984) [!].dsk")
        self.system_dir = system_dir or os.path.join(_PROJECT_ROOT, "system")
        self._actions = actions if actions is not None else DEFAULT_ACTIONS
        self.action_names = [name for name, _ in self._actions]
        self._held_key = None  # RETROK_* code currently pressed via ce_set_key, if any.

        self._lib = CapriceLib(dll_path)
        self._lib.init(self.core_path, self.system_dir)
        self._lib.load_game(self.rom_path)

        # Run past the disk autoload before treating the state as the reset
        # point, so every episode starts from roughly the same place instead
        # of a blank/loading screen. This count is an approximation (carried
        # over from the CLI smoke test) -- tune it by inspecting frames if
        # the reset point isn't actually in gameplay yet.
        for _ in range(warmup_frames):
            self._lib.step()
        if not self._lib.capture_initial_state():
            raise RuntimeError("core does not support save states; cannot set a reset point")

        frame = self._lib.get_frame_rgb()
        if frame is None:
            raise RuntimeError("no frame was rendered during warmup")
        h, w, _ = frame.shape
        self.observation_space = spaces.Box(low=0, high=255, shape=(h, w, 3), dtype=np.uint8)
        self.action_space = spaces.Discrete(len(self._actions))
        self._last_frame = frame

    def _release_held_key(self):
        if self._held_key is not None:
            self._lib.set_key(self._held_key, False)
            self._held_key = None

    def reset(self, *, seed=None, options=None):
        super().reset(seed=seed)
        self._release_held_key()
        self._lib.set_joypad(0, 0)
        self._lib.reset()
        frame = self._lib.get_frame_rgb()
        if frame is not None:
            self._last_frame = frame
        return self._last_frame, {}

    def step(self, action):
        # Each action is held for exactly this one step, mirroring how a
        # single Discrete action is normally interpreted -- release whatever
        # key the previous action pressed before applying the new one.
        self._release_held_key()

        _, spec = self._actions[action]
        buttons = 0
        if spec is not None:
            kind, value = spec
            if kind == "joypad":
                buttons = 1 << value
            elif kind == "key":
                code = key_code(value)
                self._lib.set_key(code, True)
                self._held_key = code

        self._lib.set_joypad(0, buttons)
        self._lib.step()
        frame = self._lib.get_frame_rgb()
        if frame is not None:
            self._last_frame = frame
        return self._last_frame, 0.0, False, False, {}

    def render(self):
        return self._last_frame

    def close(self):
        self._lib.shutdown()
