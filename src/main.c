#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <SDL2/SDL.h>

#include <libroq/libroq.h>

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

struct render_info {
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* textures[2];
    
    size_t current_texture;

    size_t width;
    size_t height;

    int is_initialized;
    int is_dirty;
};

static void on_info_chunk(void* userdata, size_t width, size_t height) {
    struct render_info* render_info = (struct render_info*)userdata;

    if (render_info->is_initialized)
        return;

    render_info->window = SDL_CreateWindow(
        "libroq_sample",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        0
    );

    render_info->renderer = SDL_CreateRenderer(
        render_info->window,
        0,
        SDL_RENDERER_ACCELERATED
    );

    SDL_ShowWindow(render_info->window);

    render_info->width = width;
    render_info->height = height;

    for (size_t i = 0; i < 2; i++) {
        if (render_info->textures[i])
            SDL_DestroyTexture(render_info->textures[i]);
        render_info->textures[i] = SDL_CreateTexture(
            render_info->renderer,
            SDL_PIXELFORMAT_RGBA8888,
            SDL_TEXTUREACCESS_STREAMING,
            width,
            height
        );
    }

    SDL_SetRenderDrawColor(render_info->renderer, 255, 0, 255, 255);
    render_info->is_initialized = 1;
}

static void on_display(void* userdata, const uint32_t* framebuffer) {
    struct render_info* render_info = (struct render_info*)userdata;
    SDL_Texture* texture = render_info->textures[render_info->current_texture];

    if (texture) {
        size_t pitch = render_info->width * sizeof (uint32_t);
        SDL_UpdateTexture(texture, NULL, framebuffer, pitch);
        render_info->is_dirty = 1;
        render_info->current_texture = !render_info->current_texture;
    }
}

static void render_framebuffer_sdl(struct render_info* render_info, struct roq_context* context) {
    SDL_Texture* texture;
    if (context == NULL)
        return;

    if (render_info->is_dirty) {
        /* Render the last texture */
        texture = render_info->textures[!render_info->current_texture];

        SDL_RenderClear(render_info->renderer);
        if (texture)
            SDL_RenderCopy(render_info->renderer, texture, NULL, NULL);
        SDL_RenderPresent(render_info->renderer);
        render_info->is_dirty = 0;
    }
}

int main(int argc, char** argv) {
    struct roq_context_parameters params;
    struct roq_context* context;
    struct file_userdata file_ud;
    struct render_info render_info;

    SDL_Event event;

    int is_running = 1;
    int result;

    memset(&file_ud, 0, sizeof (struct file_userdata));
    memset(&render_info, 0, sizeof(struct render_info));
    memset(&params, 0, sizeof(struct roq_context_parameters));
    
    file_ud.file = fopen("bf2introseg.roq", "rb");

    if (file_ud.file == NULL)
        return -1;

    params.size = sizeof(struct roq_context_parameters);
    params.alloc_userdata = NULL;
    params.alloc = default_alloc;
    params.io_userdata = &file_ud;
    params.read_func = file_read;
    params.render_userdata = &render_info;
    params.info_callback = on_info_chunk;
    params.display_callback = on_display;

    context = roq_create_context(&params);

    while (is_running) {
        while (SDL_PollEvent(&event)) {
            switch(event.type) {
                case SDL_QUIT:
                    is_running = 0;
                    break;
            }
        }

        if (render_info.is_dirty == 0)
            roq_play(context);

        if (render_info.is_initialized)
            render_framebuffer_sdl(&render_info, context);
    }

    if (file_ud.file)
        fclose(file_ud.file);
    return 0;
}