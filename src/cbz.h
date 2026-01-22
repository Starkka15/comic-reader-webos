#ifndef CBZ_H
#define CBZ_H

#include <SDL.h>
#include <stddef.h>

#define MAX_PAGES 2000
#define MAX_FILENAME 256

typedef enum {
    COMIC_FORMAT_UNKNOWN,
    COMIC_FORMAT_CBZ,
    COMIC_FORMAT_CBR
} ComicFormat;

// Page info (stored in memory, no image data)
typedef struct {
    char filename[MAX_FILENAME];
    unsigned long compressed_size;
    unsigned long uncompressed_size;
    long long offset;  // For seeking (CBR)
} PageInfo;

// Comic book handle
typedef struct {
    void *archive_handle;       // unzFile or ar_archive
    ComicFormat format;
    char filepath[512];
    PageInfo pages[MAX_PAGES];
    int page_count;
    int current_page;
} ComicBook;

// Open a CBZ/CBR file, read directory, sort pages
int comic_open(ComicBook *comic, const char *filepath);

// Close and free resources
void comic_close(ComicBook *comic);

// Get page count
int comic_page_count(ComicBook *comic);

// Extract a single page image data (caller must free)
// Returns raw image data (JPEG/PNG bytes)
unsigned char *comic_extract_page(ComicBook *comic, int page_index, size_t *out_size);

// Get page filename
const char *comic_page_name(ComicBook *comic, int page_index);

// Legacy names for compatibility
#define cbz_open comic_open
#define cbz_close comic_close
#define cbz_page_count comic_page_count
#define cbz_extract_page comic_extract_page
#define cbz_page_name comic_page_name

#endif
