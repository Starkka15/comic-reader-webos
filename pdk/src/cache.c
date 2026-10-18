#include "cache.h"
#include <SDL_image.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cache_init(PageCache *cache, ComicBook *comic) {
    memset(cache, 0, sizeof(PageCache));
    cache->comic = comic;
    cache->access_counter = 0;

    for (int i = 0; i < CACHE_SIZE; i++) {
        cache->entries[i].page_index = -1;
        cache->entries[i].surface = NULL;
        cache->entries[i].last_used = 0;
    }
}

void cache_clear(PageCache *cache) {
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache->entries[i].surface) {
            SDL_FreeSurface(cache->entries[i].surface);
            cache->entries[i].surface = NULL;
        }
        cache->entries[i].page_index = -1;
        cache->entries[i].last_used = 0;
    }
}

// Scale surface to fit within max dimensions while maintaining aspect ratio
static SDL_Surface *scale_surface(SDL_Surface *src, int max_width, int max_height) {
    if (!src) return NULL;

    int src_w = src->w;
    int src_h = src->h;

    // Calculate scale factor to fit within bounds
    float scale_x = (float)max_width / src_w;
    float scale_y = (float)max_height / src_h;
    float scale = (scale_x < scale_y) ? scale_x : scale_y;

    // Don't upscale small images
    if (scale > 1.0f) scale = 1.0f;

    int dst_w = (int)(src_w * scale);
    int dst_h = (int)(src_h * scale);

    // If no scaling needed, just copy
    if (dst_w == src_w && dst_h == src_h) {
        SDL_Surface *copy = SDL_ConvertSurface(src, src->format, src->flags);
        return copy;
    }

    // Create destination surface
    SDL_Surface *dst = SDL_CreateRGBSurface(
        SDL_SWSURFACE, dst_w, dst_h,
        src->format->BitsPerPixel,
        src->format->Rmask,
        src->format->Gmask,
        src->format->Bmask,
        src->format->Amask
    );

    if (!dst) {
        fprintf(stderr, "Failed to create scaled surface\n");
        return NULL;
    }

    // Simple bilinear scaling
    SDL_LockSurface(src);
    SDL_LockSurface(dst);

    Uint8 *src_pixels = (Uint8 *)src->pixels;
    Uint8 *dst_pixels = (Uint8 *)dst->pixels;
    int src_pitch = src->pitch;
    int dst_pitch = dst->pitch;
    int bpp = src->format->BytesPerPixel;

    for (int y = 0; y < dst_h; y++) {
        float src_y = y / scale;
        int src_y_int = (int)src_y;
        if (src_y_int >= src_h - 1) src_y_int = src_h - 2;

        for (int x = 0; x < dst_w; x++) {
            float src_x = x / scale;
            int src_x_int = (int)src_x;
            if (src_x_int >= src_w - 1) src_x_int = src_w - 2;

            // Simple nearest neighbor for speed
            Uint8 *src_pixel = src_pixels + src_y_int * src_pitch + src_x_int * bpp;
            Uint8 *dst_pixel = dst_pixels + y * dst_pitch + x * bpp;

            memcpy(dst_pixel, src_pixel, bpp);
        }
    }

    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(src);

    return dst;
}

// Load and scale a page
static SDL_Surface *load_page(PageCache *cache, int page_index) {
    size_t data_size;
    unsigned char *data = cbz_extract_page(cache->comic, page_index, &data_size);

    if (!data) {
        fprintf(stderr, "Failed to extract page %d\n", page_index);
        return NULL;
    }

    // Load image from memory
    SDL_RWops *rw = SDL_RWFromMem(data, data_size);
    if (!rw) {
        fprintf(stderr, "Failed to create RWops for page %d\n", page_index);
        free(data);
        return NULL;
    }

    SDL_Surface *original = IMG_Load_RW(rw, 1); // 1 = auto-close RWops
    free(data); // Free compressed data, no longer needed

    if (!original) {
        fprintf(stderr, "Failed to decode image for page %d: %s\n", page_index, IMG_GetError());
        return NULL;
    }

    printf("Loaded page %d: %dx%d\n", page_index, original->w, original->h);

    // Scale to cache size (larger than screen for zoom quality)
    SDL_Surface *scaled = scale_surface(original, CACHE_WIDTH, CACHE_HEIGHT);
    SDL_FreeSurface(original); // Free original, keep only scaled

    if (!scaled) {
        return NULL;
    }

    printf("Scaled page %d to %dx%d\n", page_index, scaled->w, scaled->h);

    // Convert to display format for proper colors
    SDL_Surface *display = SDL_DisplayFormat(scaled);
    SDL_FreeSurface(scaled);

    if (!display) {
        fprintf(stderr, "Failed to convert to display format\n");
        return scaled; // Fall back to unconverted
    }

    return display;
}

// Find LRU entry to evict
static int find_lru_entry(PageCache *cache) {
    int oldest_idx = 0;
    unsigned int oldest_time = cache->entries[0].last_used;

    for (int i = 1; i < CACHE_SIZE; i++) {
        // Prefer empty slots
        if (cache->entries[i].page_index < 0) {
            return i;
        }
        if (cache->entries[i].last_used < oldest_time) {
            oldest_time = cache->entries[i].last_used;
            oldest_idx = i;
        }
    }

    return oldest_idx;
}

SDL_Surface *cache_get_page(PageCache *cache, int page_index) {
    if (page_index < 0 || page_index >= cache->comic->page_count) {
        return NULL;
    }

    cache->access_counter++;

    // Check if already cached
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache->entries[i].page_index == page_index) {
            cache->entries[i].last_used = cache->access_counter;
            return cache->entries[i].surface;
        }
    }

    // Not cached, need to load
    SDL_Surface *surface = load_page(cache, page_index);
    if (!surface) {
        return NULL;
    }

    // Find slot (empty or LRU)
    int slot = find_lru_entry(cache);

    // Evict old entry if needed
    if (cache->entries[slot].surface) {
        printf("Evicting page %d from cache\n", cache->entries[slot].page_index);
        SDL_FreeSurface(cache->entries[slot].surface);
    }

    // Store new entry
    cache->entries[slot].page_index = page_index;
    cache->entries[slot].surface = surface;
    cache->entries[slot].last_used = cache->access_counter;

    return surface;
}

void cache_preload_adjacent(PageCache *cache, int current_page) {
    // Preload next page
    if (current_page + 1 < cache->comic->page_count) {
        cache_get_page(cache, current_page + 1);
    }

    // Preload previous page
    if (current_page - 1 >= 0) {
        cache_get_page(cache, current_page - 1);
    }
}
