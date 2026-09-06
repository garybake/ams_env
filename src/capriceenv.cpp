#include "capriceenv.h"

#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../include/libretro.h"

typedef void (RETRO_CALLCONV *retro_set_environment_t)(retro_environment_t);
typedef void (RETRO_CALLCONV *retro_init_t)(void);
typedef void (RETRO_CALLCONV *retro_deinit_t)(void);
typedef unsigned (RETRO_CALLCONV *retro_api_version_t)(void);
typedef void (RETRO_CALLCONV *retro_get_system_info_t)(struct retro_system_info *);
typedef bool (RETRO_CALLCONV *retro_load_game_t)(const struct retro_game_info *);
typedef void (RETRO_CALLCONV *retro_get_system_av_info_t)(struct retro_system_av_info *);
typedef void (RETRO_CALLCONV *retro_run_t)(void);
typedef void (RETRO_CALLCONV *retro_unload_game_t)(void);
typedef size_t (RETRO_CALLCONV *retro_serialize_size_t)(void);
typedef bool (RETRO_CALLCONV *retro_serialize_t)(void *, size_t);
typedef bool (RETRO_CALLCONV *retro_unserialize_t)(const void *, size_t);

static HMODULE g_core = NULL;
static retro_deinit_t g_deinit = NULL;
static retro_load_game_t g_load_game = NULL;
static retro_get_system_av_info_t g_get_system_av_info = NULL;
static retro_run_t g_run = NULL;
static retro_unload_game_t g_unload_game = NULL;
static retro_serialize_size_t g_serialize_size = NULL;
static retro_serialize_t g_serialize = NULL;
static retro_unserialize_t g_unserialize = NULL;
static bool g_game_loaded = false;

static void RETRO_CALLCONV core_log(enum retro_log_level level, const char *fmt, ...)
{
    static const char *prefixes[] = { "DEBUG", "INFO", "WARN", "ERROR" };
    const char *prefix = (level <= RETRO_LOG_ERROR) ? prefixes[level] : "?";
    std::printf("  [core log/%s] ", prefix);
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
}

static enum retro_pixel_format g_pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;

// Last rendered frame, pre-converted to packed 8-bit RGB (row-major, top-down).
static std::vector<unsigned char> g_rgb;
static int g_rgb_width = 0;
static int g_rgb_height = 0;

static const struct retro_variable *g_core_vars = NULL;

// Two ports' worth of RETRO_DEVICE_JOYPAD button bitmasks (bit N = RETRO_DEVICE_ID_JOYPAD_N).
static unsigned short g_joypad[2] = { 0, 0 };

// Keyboard state: cores can either poll RETRO_DEVICE_KEYBOARD (level-based,
// read from g_key_held) or register an event callback via
// RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK (edge-based, fired from
// ce_set_key). We support both since we don't know which cap32 relies on.
static retro_keyboard_event_t g_keyboard_callback = NULL;
static bool g_key_held[512] = { false }; // Sized past RETROK_LAST (~342).

// Set by ce_init() before retro_init() runs, so it must be declared up here.
static char g_system_dir[MAX_PATH];

// SET_VARIABLES descriptions look like "Label; opt1|opt2|...", where the
// first listed option is the default. Extract it so GET_VARIABLE can hand
// back a real value instead of NULL (some cores dereference the value
// unconditionally to pick a renderer/config path and crash on NULL).
static const char *default_value_for(const char *desc)
{
    static char buf[64];
    const char *semi = std::strchr(desc, ';');
    if (!semi)
        return NULL;
    const char *start = semi + 1;
    while (*start == ' ')
        ++start;
    const char *bar = std::strchr(start, '|');
    size_t len = bar ? (size_t)(bar - start) : std::strlen(start);
    if (len >= sizeof(buf))
        len = sizeof(buf) - 1;
    std::memcpy(buf, start, len);
    buf[len] = '\0';
    return buf;
}

static bool RETRO_CALLCONV core_environment(unsigned cmd, void *data)
{
    switch (cmd)
    {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
    {
        auto *cb = static_cast<struct retro_log_callback *>(data);
        cb->log = core_log;
        return true;
    }
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    {
        g_core_vars = static_cast<const struct retro_variable *>(data);
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE:
    {
        auto *var = static_cast<struct retro_variable *>(data);
        var->value = NULL;
        for (auto *v = g_core_vars; v && v->key; ++v)
        {
            if (var->key && std::strcmp(v->key, var->key) == 0)
            {
                var->value = default_value_for(v->value);
                break;
            }
        }
        // cap32 defaults to the CPC6128 model, which needs cpc6128.rom.
        // We only have cpc464.rom in system/, so force the 464 model.
        if (var->key && std::strcmp(var->key, "cap32_model") == 0)
            var->value = "464";
        return true;
    }
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        g_pixel_format = *static_cast<const enum retro_pixel_format *>(data);
        return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *static_cast<bool *>(data) = true;
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *static_cast<const char **>(data) = g_system_dir;
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY:
    {
        // Some cores build file paths (e.g. system ROM lookups) with
        // snprintf("%s/...", dir) and crash if dir is NULL, so hand back a
        // real directory rather than leaving it unset.
        static const char *dir = ".";
        *static_cast<const char **>(data) = dir;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        // Called every retro_run() frame; we never change variables.
        *static_cast<bool *>(data) = false;
        return true;
    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
        g_keyboard_callback = static_cast<struct retro_keyboard_callback *>(data)->callback;
        return true;
    default:
        return false;
    }
}

static void RETRO_CALLCONV core_video_refresh(const void *data, unsigned width,
                                               unsigned height, size_t pitch)
{
    // A NULL data pointer means "same as last frame" (we advertised
    // GET_CAN_DUPE support), so just keep whatever we already have.
    if (!data)
        return;

    unsigned bytes_per_pixel = (g_pixel_format == RETRO_PIXEL_FORMAT_XRGB8888) ? 4 : 2;
    g_rgb.resize(static_cast<size_t>(width) * height * 3);
    const unsigned char *src = static_cast<const unsigned char *>(data);

    for (unsigned y = 0; y < height; ++y)
    {
        const unsigned char *row = src + y * pitch;
        unsigned char *out = g_rgb.data() + static_cast<size_t>(y) * width * 3;
        for (unsigned x = 0; x < width; ++x)
        {
            const unsigned char *p = row + x * bytes_per_pixel;
            unsigned char r, g, b;
            if (g_pixel_format == RETRO_PIXEL_FORMAT_XRGB8888)
            {
                // Native-endian 0xXXRRGGBB -> bytes in memory are B,G,R,X on x86.
                b = p[0]; g = p[1]; r = p[2];
            }
            else
            {
                unsigned short pixel = static_cast<unsigned short>(p[0] | (p[1] << 8));
                if (g_pixel_format == RETRO_PIXEL_FORMAT_RGB565)
                {
                    unsigned r5 = (pixel >> 11) & 0x1F, g6 = (pixel >> 5) & 0x3F, b5 = pixel & 0x1F;
                    r = static_cast<unsigned char>((r5 << 3) | (r5 >> 2));
                    g = static_cast<unsigned char>((g6 << 2) | (g6 >> 4));
                    b = static_cast<unsigned char>((b5 << 3) | (b5 >> 2));
                }
                else // RETRO_PIXEL_FORMAT_0RGB1555
                {
                    unsigned r5 = (pixel >> 10) & 0x1F, g5 = (pixel >> 5) & 0x1F, b5 = pixel & 0x1F;
                    r = static_cast<unsigned char>((r5 << 3) | (r5 >> 2));
                    g = static_cast<unsigned char>((g5 << 3) | (g5 >> 2));
                    b = static_cast<unsigned char>((b5 << 3) | (b5 >> 2));
                }
            }
            out[x * 3 + 0] = r;
            out[x * 3 + 1] = g;
            out[x * 3 + 2] = b;
        }
    }
    g_rgb_width = static_cast<int>(width);
    g_rgb_height = static_cast<int>(height);
}

static void RETRO_CALLCONV core_audio_sample(int16_t left, int16_t right)
{
    (void)left; (void)right;
}

static size_t RETRO_CALLCONV core_audio_sample_batch(const int16_t *data, size_t frames)
{
    (void)data;
    return frames;
}

static void RETRO_CALLCONV core_input_poll(void)
{
}

static int16_t RETRO_CALLCONV core_input_state(unsigned port, unsigned device,
                                                unsigned index, unsigned id)
{
    (void)index;
    if (device == RETRO_DEVICE_KEYBOARD)
        return (id < 512 && g_key_held[id]) ? 1 : 0;
    if (device != RETRO_DEVICE_JOYPAD || port >= 2)
        return 0;
    unsigned short buttons = g_joypad[port];
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
        return static_cast<int16_t>(buttons);
    if (id < 16)
        return (buttons & (1u << id)) ? 1 : 0;
    return 0;
}

template <typename T>
static T resolve(const char *name)
{
    return reinterpret_cast<T>(GetProcAddress(g_core, name));
}

int ce_init(const char *core_path, const char *system_dir)
{
    std::setvbuf(stdout, NULL, _IONBF, 0);
    std::setvbuf(stderr, NULL, _IONBF, 0);

    std::strncpy(g_system_dir, system_dir, sizeof(g_system_dir) - 1);
    g_system_dir[sizeof(g_system_dir) - 1] = '\0';

    g_core = LoadLibraryA(core_path);
    if (!g_core)
    {
        std::fprintf(stderr, "ce_init: failed to load core %s (error %lu)\n", core_path,
                      GetLastError());
        return 0;
    }

    auto set_environment = resolve<retro_set_environment_t>("retro_set_environment");
    auto set_video_refresh = resolve<void (RETRO_CALLCONV *)(retro_video_refresh_t)>(
        "retro_set_video_refresh");
    auto set_audio_sample = resolve<void (RETRO_CALLCONV *)(retro_audio_sample_t)>(
        "retro_set_audio_sample");
    auto set_audio_sample_batch = resolve<void (RETRO_CALLCONV *)(retro_audio_sample_batch_t)>(
        "retro_set_audio_sample_batch");
    auto set_input_poll = resolve<void (RETRO_CALLCONV *)(retro_input_poll_t)>(
        "retro_set_input_poll");
    auto set_input_state = resolve<void (RETRO_CALLCONV *)(retro_input_state_t)>(
        "retro_set_input_state");
    auto init = resolve<retro_init_t>("retro_init");
    g_deinit = resolve<retro_deinit_t>("retro_deinit");
    g_load_game = resolve<retro_load_game_t>("retro_load_game");
    g_get_system_av_info = resolve<retro_get_system_av_info_t>("retro_get_system_av_info");
    g_run = resolve<retro_run_t>("retro_run");
    g_unload_game = resolve<retro_unload_game_t>("retro_unload_game");
    g_serialize_size = resolve<retro_serialize_size_t>("retro_serialize_size");
    g_serialize = resolve<retro_serialize_t>("retro_serialize");
    g_unserialize = resolve<retro_unserialize_t>("retro_unserialize");

    if (!set_environment || !set_video_refresh || !set_audio_sample ||
        !set_audio_sample_batch || !set_input_poll || !set_input_state ||
        !init || !g_deinit || !g_load_game || !g_get_system_av_info || !g_run ||
        !g_unload_game || !g_serialize_size || !g_serialize || !g_unserialize)
    {
        std::fprintf(stderr, "ce_init: failed to resolve one or more core symbols\n");
        FreeLibrary(g_core);
        g_core = NULL;
        return 0;
    }

    // Must be called before retro_init().
    set_environment(core_environment);
    set_video_refresh(core_video_refresh);
    set_audio_sample(core_audio_sample);
    set_audio_sample_batch(core_audio_sample_batch);
    set_input_poll(core_input_poll);
    set_input_state(core_input_state);

    init();
    return 1;
}

int ce_load_game(const char *rom_path)
{
    struct retro_system_info info;
    resolve<retro_get_system_info_t>("retro_get_system_info")(&info);

    std::vector<unsigned char> rom_data;
    FILE *rom_file = std::fopen(rom_path, "rb");
    if (!rom_file)
    {
        std::fprintf(stderr, "ce_load_game: failed to open %s\n", rom_path);
        return 0;
    }
    std::fseek(rom_file, 0, SEEK_END);
    long rom_size = std::ftell(rom_file);
    std::fseek(rom_file, 0, SEEK_SET);
    rom_data.resize(static_cast<size_t>(rom_size));
    std::fread(rom_data.data(), 1, rom_data.size(), rom_file);
    std::fclose(rom_file);

    struct retro_game_info game_info = {};
    game_info.path = rom_path;
    if (!info.need_fullpath)
    {
        game_info.data = rom_data.data();
        game_info.size = rom_data.size();
    }

    if (!g_load_game(&game_info))
    {
        std::fprintf(stderr, "ce_load_game: retro_load_game() failed\n");
        return 0;
    }

    struct retro_system_av_info av_info;
    g_get_system_av_info(&av_info);
    g_game_loaded = true;
    return 1;
}

void ce_set_joypad(int port, unsigned short buttons)
{
    if (port >= 0 && port < 2)
        g_joypad[port] = buttons;
}

void ce_set_key(int keycode, int down)
{
    if (keycode < 0 || keycode >= 512)
        return;
    g_key_held[keycode] = (down != 0);
    if (g_keyboard_callback)
        g_keyboard_callback(down != 0, static_cast<unsigned>(keycode), 0, 0);
}

void ce_step(void)
{
    if (g_run)
        g_run();
}

const unsigned char *ce_get_frame_rgb(int *out_width, int *out_height)
{
    if (g_rgb.empty())
        return NULL;
    if (out_width)
        *out_width = g_rgb_width;
    if (out_height)
        *out_height = g_rgb_height;
    return g_rgb.data();
}

static std::vector<unsigned char> g_initial_snapshot;

size_t ce_snapshot_size(void)
{
    return g_serialize_size ? g_serialize_size() : 0;
}

int ce_capture_initial_state(void)
{
    size_t size = ce_snapshot_size();
    if (size == 0)
        return 0;
    g_initial_snapshot.resize(size);
    return g_serialize(g_initial_snapshot.data(), size) ? 1 : 0;
}

int ce_reset(void)
{
    if (g_initial_snapshot.empty())
        return 0;
    return g_unserialize(g_initial_snapshot.data(), g_initial_snapshot.size()) ? 1 : 0;
}

int ce_save_snapshot(void *buf, size_t len)
{
    return g_serialize(buf, len) ? 1 : 0;
}

int ce_load_snapshot(const void *buf, size_t len)
{
    return g_unserialize(buf, len) ? 1 : 0;
}

void ce_shutdown(void)
{
    if (g_game_loaded && g_unload_game)
    {
        g_unload_game();
        g_game_loaded = false;
    }
    if (g_deinit)
        g_deinit();
    if (g_core)
    {
        FreeLibrary(g_core);
        g_core = NULL;
    }
}
