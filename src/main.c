#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef int (*roq_io_read_func) (void* userdata, void* buffer, uint32_t size);
typedef void* (*roq_allocate_func) (void* userdata, void* old, size_t old_size, size_t new_size);

uint8_t ROQ_IDENTIFIER[8] = { 0x84, 0x10, 0xFF, 0xFF, 0xFF, 0xFF, 0x1E, 0x00 };

struct roq_vector3f {
    float x, y, z;
};

struct roq_matrix3x3f {
    float m[9];
};

struct roq_context {
    uint32_t width;
    uint32_t height;
    uint32_t playback_rate;

    int has_signature;

    void* alloc_userdata;
    void* io_userdata;
    roq_allocate_func alloc;
    roq_io_read_func read;
    uint32_t* framebuffer[2];
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

struct roq_context* roq_create_context(
    void* io_userdata,
    roq_io_read_func read_func,
    void* alloc_userdata,
    roq_allocate_func alloc
) {
    struct roq_context* context;

    context = (struct roq_context*)alloc(
        alloc_userdata,
        NULL,
        0,
        sizeof(struct roq_context)
    );

    if (context == NULL)
        return NULL;

    memset(context, 0, sizeof(struct roq_context));

    context->io_userdata = io_userdata;
    context->alloc_userdata = alloc_userdata;
    context->read = read_func;
    context->alloc = alloc;
    
    return context;
}

enum roq_chunk_id {
    ROQ_SIGNATURE = 0x1084,
    ROQ_INFO = 0x1001,
    ROQ_QUAD_CODEBOOK = 0x1002,
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

int roq_play(struct roq_context* context) {
    uint16_t chunk_id = 0;
    uint32_t chunk_size = 0;
    uint16_t chunk_argument = 0;

    if (context == NULL)
        return 1;

    context->read(context->io_userdata, &chunk_id, sizeof(uint16_t));
    context->read(context->io_userdata, &chunk_size, sizeof(uint32_t));
    context->read(context->io_userdata, &chunk_argument, sizeof(uint16_t));

    switch (chunk_id) {
        case ROQ_SIGNATURE: {
            context->playback_rate = chunk_argument;
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
            break;
        }

        case ROQ_QUAD_CODEBOOK:

            break;

        default:
            return 1;
            break;
    }

    return 0;
}

int main(int argc, char** argv) {
    struct roq_context* context;
    struct file_userdata file_ud;

    memset(&file_ud, 0, sizeof (struct file_userdata));
    file_ud.file = fopen("bf2introseg.roq", "rb");

    context = roq_create_context(&file_ud, file_read, NULL, default_alloc);

    for(;;) {
        roq_play(context);
    }

    puts("Hello, world\n");

    if (file_ud.file)
        fclose(file_ud.file);
    return 0;
}