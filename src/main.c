#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <SDL2/SDL.h>

typedef int (*roq_io_read_func) (void* userdata, void* buffer, uint32_t size);
typedef void* (*roq_allocate_func) (void* userdata, void* old, size_t old_size, size_t new_size);
typedef void (*roq_on_info_chunk_func) (void* userdata, size_t width, size_t height);

uint8_t ROQ_IDENTIFIER[8] = { 0x84, 0x10, 0xFF, 0xFF, 0xFF, 0xFF, 0x1E, 0x00 };

struct roq_vector3f {
    float x, y, z;
};

struct roq_matrix3x3f {
    float m[9];
};

struct roq_chunk_header {
    uint16_t id;
    uint32_t size;
    uint16_t argument;
};

struct roq_2x2_cell {
    uint8_t y[4];
    uint8_t cb;
    uint8_t cr;
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

struct roq_frame {
    uint16_t coding_type[2];
};

struct roq_context {
    uint32_t width;
    uint32_t height;
    uint32_t playback_rate;

    struct roq_codebook codebook;

    int has_signature;

    void* alloc_userdata;
    void* io_userdata;
    void* info_userdata;
    roq_allocate_func alloc;
    roq_io_read_func read;
    roq_on_info_chunk_func info_callback;

    uint32_t* framebuffer[2];
    uint32_t current_framebuffer;
};

struct roq_context_parameters {
    size_t size;
    void* io_userdata;
    void* alloc_userdata;
    void* info_userdata;
    roq_io_read_func read_func;
    roq_allocate_func alloc;
    roq_on_info_chunk_func info_callback;
};

struct file_userdata {
    FILE* file;
};

static int file_read(void* userdata, void* buffer, uint32_t size) {
    struct file_userdata* ud = (struct file_userdata*)userdata;

    return fread(buffer, 1, size, ud->file);
}

static void* default_alloc(void* userdata, void* old, size_t old_size, size_t new_size) {
    (void)userdata;
    (void)old_size;

    if (new_size == 0) {
        free(old);
        return NULL;
    }

    return realloc(old, new_size);
}

void matrix3x3f_mul_vec3f(const float* m, const float* v, float* out) {

}

struct roq_context* roq_create_context(const struct roq_context_parameters* params) {
    struct roq_context* context;

    if ((params == NULL) || (params->size != sizeof(struct roq_context_parameters))) 
        return NULL;

    context = (struct roq_context*)params->alloc(
        params->alloc_userdata,
        NULL,
        0,
        sizeof(struct roq_context)
    );

    if (context == NULL)
        return NULL;

    memset(context, 0, sizeof(struct roq_context));

    context->io_userdata = params->io_userdata;
    context->alloc_userdata = params->alloc_userdata;
    context->read = params->read_func;
    context->alloc = params->alloc;
    context->info_userdata = params->info_userdata;
    context->info_callback = params->info_callback;
    
    return context;
}

enum roq_chunk_id {
    ROQ_SIGNATURE = 0x1084,
    ROQ_INFO = 0x1001,
    ROQ_QUAD_CODEBOOK = 0x1002,
    ROQ_QUAD_VQ = 1011,
};

static void alloc_frame_buffers(struct roq_context* context, uint32_t width, uint32_t height) {
    size_t fb_size = width * height * sizeof(uint32_t);
    if (fb_size && context->alloc) {
        context->framebuffer[0] = (uint32_t*)context->alloc(
            context->alloc_userdata,
            NULL,
            0,
            fb_size
        );

        context->framebuffer[1] = (uint32_t*)context->alloc(
            context->alloc_userdata,
            NULL,
            0,
            fb_size
        );
    }

    context->width = width;
    context->height = height;
}

void ycbcr_to_rgb(const float* ycbcr, float* rgb) {
    const float conversion_matrx[9] = {
        1.00000f,  0.00000f,  1.40200f,
        1.00000f, -0.34414f, -0.71414f,
        1.00000f,  1.77200f,  0.00000f,
    };


}

static int read_codebook_chunk(
    struct roq_context* context,
    const struct roq_chunk_header* chunk,
    struct roq_codebook* codebook
) {
    if (context == NULL)
        return 1;

    uint32_t num_2x2_cells = (chunk->argument >> 4) & 0x0F;
    uint32_t num_4x4_cells = chunk->argument & 0x04;

    /* If 0 then there are 256 cells */
    if (num_2x2_cells == 0)
        num_2x2_cells = 256;

    /* If 0 and enough bytes left in the chunk, there are 256 cells */
    size_t num_2x2_bytes = sizeof(struct roq_2x2_cell) * num_2x2_cells;
    int has_enough_room = (chunk->size - num_2x2_bytes) >= (sizeof(struct roq_4x4_cell) * 256);

    if ((num_4x4_cells == 0) && (has_enough_room)) {
        num_4x4_cells = 256;
    }

    codebook->num_2x2_cells = num_2x2_cells;
    codebook->num_4x4_cells = num_4x4_cells;

    size_t num_4x4_bytes = sizeof(struct roq_4x4_cell) * num_4x4_cells;

    /* Now since we know how many cells we need to read, so read them */
    context->read(context->io_userdata, codebook->cells_2x2, num_2x2_bytes);
    context->read(context->io_userdata, codebook->cells_4x4, num_4x4_bytes);

    return 0;
}

int roq_play(struct roq_context* context) {
    struct roq_chunk_header chunk;

    if (context == NULL)
        return 1;

    context->read(context->io_userdata, &chunk.id, sizeof(uint16_t));
    context->read(context->io_userdata, &chunk.size, sizeof(uint32_t));
    context->read(context->io_userdata, &chunk.argument, sizeof(uint16_t));

    switch (chunk.id) {
        case ROQ_SIGNATURE: {
            context->playback_rate = chunk.argument;
            context->has_signature = 1;
            break;
        }
        case ROQ_INFO: {
            uint16_t width = 0;
            uint16_t height = 0;
            uint16_t unused[2];
            context->read(context->io_userdata, &width, sizeof(uint16_t));
            context->read(context->io_userdata, &height, sizeof(uint16_t));
            context->read(context->io_userdata, &unused[0], sizeof(uint16_t) * 2);
            alloc_frame_buffers(context, width, height);

            if (context->info_callback)
                context->info_callback(context->info_userdata, width, height);
            break;
        }

        case ROQ_QUAD_CODEBOOK: {
            read_codebook_chunk(context, &chunk, &context->codebook);
            break;
        }

        case ROQ_QUAD_VQ: {
            uint8_t mx = (chunk.argument >> 4) & 0xF;
            uint8_t my = chunk.argument & 0xF;
            break;
        }

        default:
            return 1;
            break;
    }

    return 0;
}

struct render_info {
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
};

static void on_info_chunk(void* userdata, size_t width, size_t height) {
    (void)userdata;

    struct render_info* render_info = (struct render_info*)userdata;

    SDL_SetWindowSize(render_info->window, width, height);
}

int main(int argc, char** argv) {
    struct roq_context_parameters params;
    struct roq_context* context;
    struct file_userdata file_ud;
    struct render_info render_info;

    int is_running = 1;
    int result;
    
    result = SDL_CreateWindowAndRenderer(
        640, 
        480,
        0,
        &render_info.window,
        &render_info.renderer
    );

    SDL_Event event;
    SDL_ShowWindow(render_info.window);

    memset(&file_ud, 0, sizeof (struct file_userdata));
    file_ud.file = fopen("bf2introseg.roq", "rb");

    params.size = sizeof(struct roq_context_parameters);
    params.alloc_userdata = NULL;
    params.alloc = default_alloc;
    params.io_userdata = &file_ud;
    params.read_func = file_read;
    params.info_userdata = &render_info;
    params.info_callback = on_info_chunk;

    context = roq_create_context(&params);

    while (is_running) {
        while (SDL_PollEvent(&event)) {
            switch(event.type) {
                case SDL_QUIT:
                    is_running = 0;
                    break;
            }
        }
        roq_play(context);
    }

    puts("Hello, world\n");

    if (file_ud.file)
        fclose(file_ud.file);
    return 0;
}