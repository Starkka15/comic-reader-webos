#ifndef XML_PARSER_H
#define XML_PARSER_H

#include <stddef.h>

#define MAX_FILENAME_LEN 256
#define MAX_CLOUD_ENTRIES 256

typedef enum {
    CLOUD_ENTRY_FILE,
    CLOUD_ENTRY_DIRECTORY
} CloudEntryType;

typedef struct {
    char name[MAX_FILENAME_LEN];     // Display name
    char href[MAX_FILENAME_LEN * 2]; // Full path/href
    CloudEntryType type;
    long long size;                  // File size in bytes
    char modified[64];               // Last modified date string
    char content_type[128];          // MIME type
} CloudFileEntry;

typedef struct {
    CloudFileEntry entries[MAX_CLOUD_ENTRIES];
    int count;
} CloudFileList;

// Initialize file list
void filelist_init(CloudFileList *list);

// Parse WebDAV PROPFIND XML response
// Returns 0 on success, -1 on error
int parse_webdav_response(const char *xml, size_t xml_len, CloudFileList *list);

// Helper: URL decode a string in-place
void url_decode(char *str);

// Helper: Extract filename from path
void extract_filename(const char *path, char *filename, size_t len);

#endif /* XML_PARSER_H */
