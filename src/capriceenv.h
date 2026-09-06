// C ABI for driving the cap32 (Amstrad CPC) libretro core headlessly.
// Compiled either directly into a CLI test harness (src/main.cpp) or into
// capriceenv.dll for consumption from Python via ctypes.
#pragma once

#include <stddef.h>

#ifdef CAPRICEENV_BUILD_DLL
#define CE_API extern "C" __declspec(dllexport)
#else
#define CE_API extern "C"
#endif

// Loads the libretro core and runs retro_init(). `system_dir` is where the
// core looks for firmware ROMs (cpc464.rom etc). Returns 1 on success.
CE_API int ce_init(const char *core_path, const char *system_dir);

// Reads `rom_path` and calls retro_load_game(). Returns 1 on success.
CE_API int ce_load_game(const char *rom_path);

// Sets the RETRO_DEVICE_JOYPAD button bitmask for the given port (bit N is
// RETRO_DEVICE_ID_JOYPAD_N from libretro.h), used by every ce_step() call
// until changed again.
CE_API void ce_set_joypad(int port, unsigned short buttons);

// Presses (down=1) or releases (down=0) a keyboard key, identified by a
// RETRO_KEY (RETROK_*) code from libretro.h. Fires the core's registered
// keyboard-event callback immediately (if any -- cap32 registers one via
// RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK) and updates persistent key-held
// state used if the core instead polls RETRO_DEVICE_KEYBOARD. Covers the
// whole keyboard, e.g. RETROK_SPACE (32) or RETROK_j (106).
CE_API void ce_set_key(int keycode, int down);

// Runs one emulated video frame.
CE_API void ce_step(void);

// Returns a pointer to the last rendered frame as tightly packed 8-bit RGB
// (row-major, top-down, no pitch/format decoding needed by the caller).
// Valid until the next ce_step() call. NULL if no frame has been rendered yet.
CE_API const unsigned char *ce_get_frame_rgb(int *out_width, int *out_height);

// Snapshot support, used for fast episode resets instead of re-booting the
// disk from scratch every episode.
CE_API size_t ce_snapshot_size(void);
CE_API int ce_capture_initial_state(void);  // Snapshots the current state as the reset target.
CE_API int ce_reset(void);                  // Restores the state captured above. 0 if none captured.
CE_API int ce_save_snapshot(void *buf, size_t len);
CE_API int ce_load_snapshot(const void *buf, size_t len);

// Unloads the game, deinitializes the core, and frees the DLL.
CE_API void ce_shutdown(void);
