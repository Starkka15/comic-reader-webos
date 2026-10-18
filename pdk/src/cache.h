#ifndef CACHE_H
#define CACHE_H

#include <SDL.h>
#include "cbz.h"

#define CACHE_SIZE 3  // Keep 3 pages in memory

// Screen dimensions for scaling
#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768

// Cache at higher resolution for zoom quality (1.5x screen)
#define CACHE_WIDTH 1536
#define CACHE_HEIGHT 1152

// Cached page entry
typedef struct {
    int page_index;         // -1 if unused
    SDL_Surface *surface;   // Scaled image
    unsigned int last_used; // For LRU eviction
} CacheEntry;

// Page cache
typedef struct {
    CacheEntry entries[CACHE_SIZE];
    unsigned int access_counter;
    ComicBook *comic;       // Reference to comic book
} PageCache;

// Initialize cache
void cache_init(PageCache *cache, ComicBook *comic);

// Free all cached surfaces
void cache_clear(PageCache *cache);

// Get a page surface (loads and caches if needed)
// Returns scaled SDL_Surface, or NULL on error
SDL_Surface *cache_get_page(PageCache *cache, int page_index);

// Preload adjacent pages in background (call after getting current page)
void cache_preload_adjacent(PageCache *cache, int current_page);

#endif
