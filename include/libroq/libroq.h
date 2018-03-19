#pragma once

#include <stdint.h>
#include <stddef.h>

typedef int (*roq_io_read_func) (void* userdata, void* buffer, uint32_t size);
typedef void* (*roq_allocate_func) (void* userdata, void* old, size_t old_size, size_t new_size);
typedef void (*roq_on_info_chunk_func) (void* userdata, size_t width, size_t height);
typedef void (*roq_on_display_func) (void* userdata, const uint32_t* pixels);

struct roq_chunk_header {
    uint16_t id;
    uint32_t size;
    uint16_t argument;
};

struct roq_2x2_cell {
    uint32_t rgba[4];
};

struct roq_4x4_cell {
    uint8_t v[4];
};

struct roq_codebook {
    struct roq_2x2_cell cells_2x2[256];   /* This is pretty mildy infuriating */
    struct roq_4x4_cell cells_4x4[256];
    size_t num_2x2_cells;
    size_t num_4x4_cells;
};

struct roq_context {
    uint32_t width;
    uint32_t height;
    uint32_t playback_rate;

    struct roq_codebook codebook;

    int has_signature;

    void* alloc_userdata;
    void* io_userdata;
    void* render_userdata;
    roq_allocate_func alloc;
    roq_io_read_func read;
    roq_on_info_chunk_func info_callback;
    roq_on_display_func display_callback;

    uint32_t* framebuffers[2];
    uint32_t current_framebuffer;
};

struct roq_context_parameters {
    size_t size;
    void* io_userdata;
    void* alloc_userdata;
    void* render_userdata;
    roq_io_read_func read_func;
    roq_allocate_func alloc;
    roq_on_info_chunk_func info_callback;
    roq_on_display_func display_callback;
};

struct roq_context* roq_create_context(const struct roq_context_parameters* params);
int roq_play(struct roq_context* context);