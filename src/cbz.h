#ifndef CBZ_H
#define CBZ_H

#include <SDL.h>
#include <stddef.h>

#define MAX_PAGES 2000
#define MAX_FILENAME 256

// Page info (stored in memory, no image data)
typedef struct {
    char filename[MAX_FILENAME];
    unsigned long compressed_size;
    unsigned long uncompressed_size;
} PageInfo;

// Comic book handle
typedef struct {
    void *zip_handle;           // unzFile handle
    char filepath[512];
    PageInfo pages[MAX_PAGES];
    int page_count;
    int current_page;
} ComicBook;

// Open a CBZ file, read directory, sort pages
int cbz_open(ComicBook *comic, const char *filepath);

// Close and free resources
void cbz_close(ComicBook *comic);

// Get page count
int cbz_page_count(ComicBook *comic);

// Extract a single page image data (caller must free)
// Returns raw image data (JPEG/PNG bytes)
unsigned char *cbz_extract_page(ComicBook *comic, int page_index, size_t *out_size);

// Get page filename
const char *cbz_page_name(ComicBook *comic, int page_index);

#endif
