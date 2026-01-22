#include "cbz.h"
#include "unzip.h"
#include "unarr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Check if filename is an image
static int is_image_file(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext) return 0;

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

// Compare function for sorting pages (natural sort)
static int compare_pages(const void *a, const void *b) {
    const PageInfo *pa = (const PageInfo *)a;
    const PageInfo *pb = (const PageInfo *)b;

    const char *name_a = get_basename(pa->filename);
    const char *name_b = get_basename(pb->filename);

    while (*name_a && *name_b) {
        if (isdigit(*name_a) && isdigit(*name_b)) {
            long num_a = strtol(name_a, (char**)&name_a, 10);
            long num_b = strtol(name_b, (char**)&name_b, 10);
            if (num_a != num_b) return (num_a < num_b) ? -1 : 1;
        } else {
            int diff = tolower(*name_a) - tolower(*name_b);
            if (diff != 0) return diff;
            name_a++;
            name_b++;
        }
    }

    return strlen(name_a) - strlen(name_b);
}

// Detect format from file extension
static ComicFormat detect_format(const char *filepath) {
    const char *ext = strrchr(filepath, '.');
    if (!ext) return COMIC_FORMAT_UNKNOWN;

    char lower[16] = {0};
    for (int i = 0; ext[i] && i < 15; i++) {
        lower[i] = tolower((unsigned char)ext[i]);
    }

    if (strcmp(lower, ".cbz") == 0 || strcmp(lower, ".zip") == 0) {
        return COMIC_FORMAT_CBZ;
    }
    if (strcmp(lower, ".cbr") == 0 || strcmp(lower, ".rar") == 0) {
        return COMIC_FORMAT_CBR;
    }

    return COMIC_FORMAT_UNKNOWN;
}

// ============== CBZ (ZIP) Functions ==============

static int cbz_open_internal(ComicBook *comic, const char *filepath) {
    unzFile zip = unzOpen(filepath);
    if (!zip) {
        fprintf(stderr, "Failed to open CBZ: %s\n", filepath);
        return -1;
    }

    comic->archive_handle = zip;
    comic->page_count = 0;

    if (unzGoToFirstFile(zip) != UNZ_OK) {
        fprintf(stderr, "Empty or invalid CBZ file\n");
        unzClose(zip);
        comic->archive_handle = NULL;
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

        // Skip hidden files
        const char *basename = get_basename(filename);
        if (basename[0] == '.') continue;
        if (strstr(filename, "__MACOSX") != NULL) continue;

        if (comic->page_count >= MAX_PAGES) break;

        PageInfo *page = &comic->pages[comic->page_count];
        strncpy(page->filename, filename, MAX_FILENAME - 1);
        page->compressed_size = file_info.compressed_size;
        page->uncompressed_size = file_info.uncompressed_size;
        page->offset = 0;
        comic->page_count++;

    } while (unzGoToNextFile(zip) == UNZ_OK);

    return (comic->page_count > 0) ? 0 : -1;
}

static unsigned char *cbz_extract_internal(ComicBook *comic, int page_index, size_t *out_size) {
    PageInfo *page = &comic->pages[page_index];
    unzFile zip = (unzFile)comic->archive_handle;

    if (unzLocateFile(zip, page->filename, 0) != UNZ_OK) {
        fprintf(stderr, "Failed to locate page: %s\n", page->filename);
        return NULL;
    }

    if (unzOpenCurrentFile(zip) != UNZ_OK) {
        fprintf(stderr, "Failed to open page: %s\n", page->filename);
        return NULL;
    }

    size_t size = page->uncompressed_size;
    unsigned char *data = (unsigned char *)malloc(size);
    if (!data) {
        unzCloseCurrentFile(zip);
        return NULL;
    }

    int bytes_read = unzReadCurrentFile(zip, data, size);
    unzCloseCurrentFile(zip);

    if (bytes_read < 0 || (size_t)bytes_read != size) {
        free(data);
        return NULL;
    }

    *out_size = size;
    return data;
}

static void cbz_close_internal(ComicBook *comic) {
    if (comic->archive_handle) {
        unzClose((unzFile)comic->archive_handle);
    }
}

// ============== CBR (RAR) Functions ==============

static int cbr_open_internal(ComicBook *comic, const char *filepath) {
    ar_stream *stream = ar_open_file(filepath);
    if (!stream) {
        fprintf(stderr, "Failed to open file: %s\n", filepath);
        return -1;
    }

    ar_archive *ar = ar_open_rar_archive(stream);
    if (!ar) {
        fprintf(stderr, "Failed to open RAR archive: %s\n", filepath);
        ar_close(stream);
        return -1;
    }

    comic->archive_handle = ar;
    comic->page_count = 0;

    // Scan all entries
    while (ar_parse_entry(ar)) {
        const char *name = ar_entry_get_name(ar);
        if (!name) continue;

        // Skip non-images
        if (!is_image_file(name)) continue;

        // Skip hidden files
        const char *basename = get_basename(name);
        if (basename[0] == '.') continue;

        if (comic->page_count >= MAX_PAGES) break;

        PageInfo *page = &comic->pages[comic->page_count];
        strncpy(page->filename, name, MAX_FILENAME - 1);
        page->uncompressed_size = ar_entry_get_size(ar);
        page->offset = ar_entry_get_offset(ar);
        comic->page_count++;
    }

    if (comic->page_count == 0) {
        ar_close_archive(ar);
        ar_close(stream);
        comic->archive_handle = NULL;
        return -1;
    }

    return 0;
}

static unsigned char *cbr_extract_internal(ComicBook *comic, int page_index, size_t *out_size) {
    PageInfo *page = &comic->pages[page_index];
    ar_archive *ar = (ar_archive *)comic->archive_handle;

    // Seek to the page's offset
    if (!ar_parse_entry_at(ar, page->offset)) {
        fprintf(stderr, "Failed to seek to page: %s\n", page->filename);
        return NULL;
    }

    size_t size = ar_entry_get_size(ar);
    unsigned char *data = (unsigned char *)malloc(size);
    if (!data) {
        return NULL;
    }

    // Extract the data
    if (!ar_entry_uncompress(ar, data, size)) {
        fprintf(stderr, "Failed to extract page: %s\n", page->filename);
        free(data);
        return NULL;
    }

    *out_size = size;
    return data;
}

static void cbr_close_internal(ComicBook *comic) {
    if (comic->archive_handle) {
        ar_archive *ar = (ar_archive *)comic->archive_handle;
        // Get the stream before closing archive
        ar_close_archive(ar);
        // Note: stream is closed internally by unarr
    }
}

// ============== Public API ==============

int comic_open(ComicBook *comic, const char *filepath) {
    memset(comic, 0, sizeof(ComicBook));
    strncpy(comic->filepath, filepath, sizeof(comic->filepath) - 1);

    comic->format = detect_format(filepath);

    int result;
    switch (comic->format) {
        case COMIC_FORMAT_CBZ:
            result = cbz_open_internal(comic, filepath);
            break;
        case COMIC_FORMAT_CBR:
            result = cbr_open_internal(comic, filepath);
            break;
        default:
            fprintf(stderr, "Unknown comic format: %s\n", filepath);
            return -1;
    }

    if (result != 0) {
        return result;
    }

    // Sort pages naturally
    qsort(comic->pages, comic->page_count, sizeof(PageInfo), compare_pages);

    printf("Opened comic: %s (%d pages, format: %s)\n",
           filepath, comic->page_count,
           comic->format == COMIC_FORMAT_CBZ ? "CBZ" : "CBR");

    return 0;
}

void comic_close(ComicBook *comic) {
    switch (comic->format) {
        case COMIC_FORMAT_CBZ:
            cbz_close_internal(comic);
            break;
        case COMIC_FORMAT_CBR:
            cbr_close_internal(comic);
            break;
        default:
            break;
    }
    comic->archive_handle = NULL;
    comic->page_count = 0;
}

int comic_page_count(ComicBook *comic) {
    return comic->page_count;
}

unsigned char *comic_extract_page(ComicBook *comic, int page_index, size_t *out_size) {
    if (!comic->archive_handle || page_index < 0 || page_index >= comic->page_count) {
        return NULL;
    }

    switch (comic->format) {
        case COMIC_FORMAT_CBZ:
            return cbz_extract_internal(comic, page_index, out_size);
        case COMIC_FORMAT_CBR:
            return cbr_extract_internal(comic, page_index, out_size);
        default:
            return NULL;
    }
}

const char *comic_page_name(ComicBook *comic, int page_index) {
    if (page_index < 0 || page_index >= comic->page_count) {
        return NULL;
    }
    return comic->pages[page_index].filename;
}
