#include "cbz.h"
#include "../minizip/unzip.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Check if filename is an image
static int is_image_file(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext) return 0;

    // Convert to lowercase for comparison
    char lower[16] = {0};
    for (int i = 0; ext[i] && i < 15; i++) {
        lower[i] = tolower((unsigned char)ext[i]);
    }

    return (strcmp(lower, ".jpg") == 0 ||
            strcmp(lower, ".jpeg") == 0 ||
            strcmp(lower, ".png") == 0 ||
            strcmp(lower, ".gif") == 0 ||
            strcmp(lower, ".bmp") == 0 ||
            strcmp(lower, ".webp") == 0);
}

// Get just the filename from a path
static const char *get_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    if (slash) return slash + 1;
    slash = strrchr(path, '\\');
    if (slash) return slash + 1;
    return path;
}

// Compare function for sorting pages
static int compare_pages(const void *a, const void *b) {
    const PageInfo *pa = (const PageInfo *)a;
    const PageInfo *pb = (const PageInfo *)b;

    // Natural sort - compare basenames
    const char *name_a = get_basename(pa->filename);
    const char *name_b = get_basename(pb->filename);

    // Try to extract numbers for natural sorting
    // This handles "page1.jpg" vs "page10.jpg" correctly
    while (*name_a && *name_b) {
        if (isdigit(*name_a) && isdigit(*name_b)) {
            // Compare numbers
            long num_a = strtol(name_a, (char**)&name_a, 10);
            long num_b = strtol(name_b, (char**)&name_b, 10);
            if (num_a != num_b) return (num_a < num_b) ? -1 : 1;
        } else {
            // Compare characters
            int diff = tolower(*name_a) - tolower(*name_b);
            if (diff != 0) return diff;
            name_a++;
            name_b++;
        }
    }

    return strlen(name_a) - strlen(name_b);
}

int cbz_open(ComicBook *comic, const char *filepath) {
    memset(comic, 0, sizeof(ComicBook));
    strncpy(comic->filepath, filepath, sizeof(comic->filepath) - 1);

    unzFile zip = unzOpen(filepath);
    if (!zip) {
        fprintf(stderr, "Failed to open CBZ: %s\n", filepath);
        return -1;
    }

    comic->zip_handle = zip;
    comic->page_count = 0;

    // Read the central directory
    if (unzGoToFirstFile(zip) != UNZ_OK) {
        fprintf(stderr, "Empty or invalid CBZ file\n");
        unzClose(zip);
        comic->zip_handle = NULL;
        return -1;
    }

    do {
        unz_file_info file_info;
        char filename[MAX_FILENAME];

        if (unzGetCurrentFileInfo(zip, &file_info, filename, sizeof(filename),
                                   NULL, 0, NULL, 0) != UNZ_OK) {
            continue;
        }

        // Skip directories and non-image files
        if (filename[strlen(filename) - 1] == '/') continue;
        if (!is_image_file(filename)) continue;

        // Skip hidden files (starting with . or in __MACOSX)
        const char *basename = get_basename(filename);
        if (basename[0] == '.') continue;
        if (strstr(filename, "__MACOSX") != NULL) continue;
        if (strstr(filename, ".DS_Store") != NULL) continue;

        if (comic->page_count >= MAX_PAGES) {
            fprintf(stderr, "Warning: Comic has more than %d pages\n", MAX_PAGES);
            break;
        }

        PageInfo *page = &comic->pages[comic->page_count];
        strncpy(page->filename, filename, MAX_FILENAME - 1);
        page->compressed_size = file_info.compressed_size;
        page->uncompressed_size = file_info.uncompressed_size;
        comic->page_count++;

    } while (unzGoToNextFile(zip) == UNZ_OK);

    if (comic->page_count == 0) {
        fprintf(stderr, "No image files found in CBZ\n");
        unzClose(zip);
        comic->zip_handle = NULL;
        return -1;
    }

    // Sort pages naturally
    qsort(comic->pages, comic->page_count, sizeof(PageInfo), compare_pages);

    printf("Opened CBZ: %s (%d pages)\n", filepath, comic->page_count);
    return 0;
}

void cbz_close(ComicBook *comic) {
    if (comic->zip_handle) {
        unzClose((unzFile)comic->zip_handle);
        comic->zip_handle = NULL;
    }
    comic->page_count = 0;
}

int cbz_page_count(ComicBook *comic) {
    return comic->page_count;
}

unsigned char *cbz_extract_page(ComicBook *comic, int page_index, size_t *out_size) {
    if (!comic->zip_handle || page_index < 0 || page_index >= comic->page_count) {
        return NULL;
    }

    PageInfo *page = &comic->pages[page_index];
    unzFile zip = (unzFile)comic->zip_handle;

    // Locate the file in the archive
    if (unzLocateFile(zip, page->filename, 0) != UNZ_OK) {
        fprintf(stderr, "Failed to locate page: %s\n", page->filename);
        return NULL;
    }

    // Open the file for reading
    if (unzOpenCurrentFile(zip) != UNZ_OK) {
        fprintf(stderr, "Failed to open page: %s\n", page->filename);
        return NULL;
    }

    // Allocate buffer for uncompressed data
    size_t size = page->uncompressed_size;
    unsigned char *data = (unsigned char *)malloc(size);
    if (!data) {
        fprintf(stderr, "Failed to allocate %zu bytes for page\n", size);
        unzCloseCurrentFile(zip);
        return NULL;
    }

    // Read the data
    int bytes_read = unzReadCurrentFile(zip, data, size);
    unzCloseCurrentFile(zip);

    if (bytes_read < 0 || (size_t)bytes_read != size) {
        fprintf(stderr, "Failed to read page data: expected %zu, got %d\n", size, bytes_read);
        free(data);
        return NULL;
    }

    *out_size = size;
    return data;
}

const char *cbz_page_name(ComicBook *comic, int page_index) {
    if (page_index < 0 || page_index >= comic->page_count) {
        return NULL;
    }
    return comic->pages[page_index].filename;
}
