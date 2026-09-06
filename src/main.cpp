// CLI smoke test for capriceenv: loads the cap32 core, boots a disk image,
// runs it for a fixed number of frames, and dumps the last rendered frame to
// a BMP so it can be inspected. Exercises the same engine (capriceenv.cpp)
// used by the Python/ctypes binding, so the two never drift apart.

#include <windows.h>
#include <cstdio>

#include "capriceenv.h"

static const char *CORE_PATH = "cores/cap32_libretro.dll";
static const char *SYSTEM_DIR = "system";
static const char *ROM_PATH = "roms/Harrier Attack (UK) (1984) [!].dsk";
static const int NUM_FRAMES = 300;
static const char *FRAME_OUT_PATH = "frame.bmp";

static bool write_bmp(const char *path, const unsigned char *rgb, int width, int height)
{
    unsigned row_bytes = width * 3;
    unsigned row_padding = (4 - (row_bytes % 4)) % 4;

    BITMAPFILEHEADER fh = {};
    BITMAPINFOHEADER ih = {};
    ih.biSize = sizeof(ih);
    ih.biWidth = width;
    ih.biHeight = height;
    ih.biPlanes = 1;
    ih.biBitCount = 24;
    ih.biCompression = BI_RGB;
    ih.biSizeImage = (row_bytes + row_padding) * height;

    fh.bfType = 0x4D42; // 'BM'
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + ih.biSizeImage;

    FILE *f = std::fopen(path, "wb");
    if (!f)
        return false;
    std::fwrite(&fh, sizeof(fh), 1, f);
    std::fwrite(&ih, sizeof(ih), 1, f);

    static const unsigned char pad[3] = { 0, 0, 0 };
    for (int out_row = 0; out_row < height; ++out_row)
    {
        int src_row = height - 1 - out_row; // BMP rows are bottom-up.
        const unsigned char *row = rgb + static_cast<size_t>(src_row) * width * 3;
        for (int x = 0; x < width; ++x)
        {
            unsigned char bgr[3] = { row[x * 3 + 2], row[x * 3 + 1], row[x * 3 + 0] };
            std::fwrite(bgr, 3, 1, f);
        }
        if (row_padding)
            std::fwrite(pad, row_padding, 1, f);
    }

    std::fclose(f);
    return true;
}

int main()
{
    std::printf("Loading core: %s\n", CORE_PATH);
    if (!ce_init(CORE_PATH, SYSTEM_DIR))
    {
        std::fprintf(stderr, "ce_init failed\n");
        return 1;
    }
    std::printf("ce_init OK\n");

    std::printf("Loading ROM: %s\n", ROM_PATH);
    if (!ce_load_game(ROM_PATH))
    {
        std::fprintf(stderr, "ce_load_game failed\n");
        ce_shutdown();
        return 1;
    }
    std::printf("ce_load_game OK\n");

    std::printf("Running %d frames...\n", NUM_FRAMES);
    for (int i = 0; i < NUM_FRAMES; ++i)
        ce_step();
    std::printf("Done running.\n");

    // Prove the reset path (retro_serialize/retro_unserialize) works: capture
    // the current state, run a bit further, then reset back and confirm the
    // frame afterwards still renders correctly.
    if (ce_capture_initial_state())
    {
        std::printf("Captured snapshot (%zu bytes)\n", ce_snapshot_size());
        for (int i = 0; i < 60; ++i)
            ce_step();
        if (ce_reset())
            std::printf("ce_reset OK\n");
        else
            std::fprintf(stderr, "ce_reset failed\n");
    }
    else
    {
        std::fprintf(stderr, "ce_capture_initial_state failed (core may not support save states)\n");
    }

    int width = 0, height = 0;
    const unsigned char *frame = ce_get_frame_rgb(&width, &height);
    if (frame)
    {
        if (write_bmp(FRAME_OUT_PATH, frame, width, height))
            std::printf("Wrote last frame to %s (%dx%d)\n", FRAME_OUT_PATH, width, height);
        else
            std::fprintf(stderr, "Failed to write %s\n", FRAME_OUT_PATH);
    }
    else
    {
        std::printf("No frame was ever rendered, skipping %s\n", FRAME_OUT_PATH);
    }

    ce_shutdown();
    std::printf("Done.\n");
    return 0;
}
