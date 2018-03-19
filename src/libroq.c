#include "libroq/libroq.h"
#include <string.h>
#include <math.h>

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

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
    context->render_userdata = params->render_userdata;
    context->info_callback = params->info_callback;
    context->display_callback = params->display_callback;
    
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
        context->framebuffers[0] = (uint32_t*)context->alloc(
            context->alloc_userdata,
            NULL,
            0,
            fb_size
        );

        context->framebuffers[1] = (uint32_t*)context->alloc(
            context->alloc_userdata,
            NULL,
            0,
            fb_size
        );
    }

    context->width = width;
    context->height = height;
}

static int inline clamp(int value, int min, int max) {
    if (value < min)
        value = min;
    if (value > max)
        value = max;
    return value;
}

static void multiply_v3_m3(const float* matrix, const float* vector3f, float* result) {
    result[0] = vector3f[0] * matrix[0] + vector3f[1] * matrix[4] + vector3f[2] * matrix[ 8] + matrix[12];
    result[1] = vector3f[0] * matrix[1] + vector3f[1] * matrix[5] + vector3f[2] * matrix[ 9] + matrix[13];
    result[2] = vector3f[0] * matrix[2] + vector3f[1] * matrix[6] + vector3f[2] * matrix[10] + matrix[14];
}

static inline void ycbcr_to_rgba(const uint8_t* ycbcr, uint32_t* rgba) {
    float conversion_matrix[9] = {
        1.00000f,  0.00000f,  1.40200f,
        1.00000f, -0.34414f, -0.71414f,
        1.00000f,  1.77200f,  0.00000f
    };

    uint8_t y = ycbcr[0];
    uint8_t cb = ycbcr[1];
    uint8_t cr = ycbcr[2];

    int r = (int) (y + 1.40200f * (cr - 0x80));
    int g = (int) (y - 0.34414f * (cb - 0x80) - 0.71414f * (cr - 0x80));
    int b = (int) (y + 1.77200f * (cb - 0x80));

    r = max(0, min(255, r));
    g = max(0, min(255, g));
    b = max(0, min(255, b));
    uint8_t rgba_buffer[4] = {(uint8_t)r, (uint8_t)g, (uint8_t)b, 0xFF};

    *rgba = *(uint32_t*)rgba_buffer;
}

struct ycbcr {
    uint8_t y[4];
    uint8_t cb;
    uint8_t cr;
};

static int read_codebook_chunk(
    struct roq_context* context,
    const struct roq_chunk_header* chunk,
    struct roq_codebook* codebook
) {
    if (context == NULL)
        return 1;

    uint32_t num_2x2_cells = (chunk->argument >> 4) & 0x0F;
    uint32_t num_4x4_cells = chunk->argument & 0x04;

    struct ycbcr raw_2x2_cells[256];

    /* If 0 then there are 256 cells */
    if (num_2x2_cells == 0)
        num_2x2_cells = 256;

    /* If 0 and enough bytes left in the chunk, there are 256 cells */
    size_t num_raw_2x2_bytes = sizeof(struct roq_2x2_cell) * num_2x2_cells;
    int has_enough_room = (chunk->size - num_raw_2x2_bytes) >= (sizeof(struct roq_4x4_cell) * 256);

    if ((num_4x4_cells == 0) && (has_enough_room)) {
        num_4x4_cells = 256;
    }

    codebook->num_2x2_cells = num_2x2_cells;
    codebook->num_4x4_cells = num_4x4_cells;

    size_t num_4x4_bytes = sizeof(struct roq_4x4_cell) * num_4x4_cells;

    /* Now since we know how many cells we need to read, so read them */
    context->read(context->io_userdata, raw_2x2_cells, num_raw_2x2_bytes);
    context->read(context->io_userdata, codebook->cells_4x4, num_4x4_bytes);

    /* Now convert the YCbCr codebook into an intemediary RGBA format for sanity */
    for (size_t i = 0; i < num_2x2_cells; i++) {
        for (size_t j = 0; j < 4; j++) {
            uint8_t ycbcr[4] = {raw_2x2_cells[i].y[j], raw_2x2_cells[i].cb, raw_2x2_cells[i].cr};
            ycbcr_to_rgba(ycbcr, &context->codebook.cells_2x2[i].rgba[j]);
        }
    }

    return 0;
}

static uint32_t* get_last_framebuffer(struct roq_context* context) {
    return context->framebuffers[!context->current_framebuffer];
}

static uint32_t* get_current_framebuffer(struct roq_context* context) {
    return context->framebuffers[context->current_framebuffer];
}

static int read_quad_vq_chunk(
    struct roq_context* context,
    struct roq_chunk_header* chunk
) {
    uint8_t mx = (chunk->argument >> 4) & 0xF;
    uint8_t my = chunk->argument & 0xF;
    uint16_t coding_type = 0;

    context->read(context->io_userdata, &coding_type, sizeof(uint16_t));
    
    uint32_t num_arguments = 0;
    uint32_t current_level = 8;

    uint32_t* current_fb = get_current_framebuffer(context);
    uint32_t* last_fb = get_last_framebuffer(context);

    for (size_t height_cursor = 0; height_cursor < context->height; ) {
        for (size_t width_cursor = 0; width_cursor < context->width; ) {
            for (uint16_t coding_mask = 0x03; coding_mask != 0xC0; coding_mask <<= 2) {
                uint16_t type = (coding_type & coding_mask) & 0x3;

                switch (type) {
                    case 0: /* Skip quad */
                        break;
                    case 1: /* Take pixels from last frame */
                        break;
                    case 2: /* Quad vector quantization */
                        break;
                    case 3: /* Subdivide the quadtree */
                        break;
                    default:
                        return 1;
                        break;
                }
            }
        }
    }
    

    return 0;
}

static int swap_framebuffers(struct roq_context* context) {
    if (context == NULL)
        return 1;
    context->current_framebuffer = !context->current_framebuffer;
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
                context->info_callback(context->render_userdata, width, height);
            break;
        }

        case ROQ_QUAD_CODEBOOK: {
            read_codebook_chunk(context, &chunk, &context->codebook);
            break;
        }

        case ROQ_QUAD_VQ: {
            uint32_t* fb = context->framebuffers[context->current_framebuffer];
            read_quad_vq_chunk(context, &chunk);

            if (context->display_callback)
                context->display_callback(context->render_userdata, fb);
            swap_framebuffers(context);
            break;
        }

        default:
            return 1;
            break;
    }

    return 0;
}