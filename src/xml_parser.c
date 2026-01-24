#include "xml_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void filelist_init(CloudFileList *list) {
    memset(list, 0, sizeof(CloudFileList));
    list->count = 0;
}

void url_decode(char *str) {
    char *src = str;
    char *dst = str;
    char hex[3] = {0};

    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            hex[0] = src[1];
            hex[1] = src[2];
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

void extract_filename(const char *path, char *filename, size_t len) {
    // Find last non-trailing slash
    size_t path_len = strlen(path);
    const char *end = path + path_len;

    // Skip trailing slashes
    while (end > path && *(end - 1) == '/') {
        end--;
    }

    // Find start of filename
    const char *start = end;
    while (start > path && *(start - 1) != '/') {
        start--;
    }

    size_t name_len = end - start;
    if (name_len >= len) {
        name_len = len - 1;
    }

    strncpy(filename, start, name_len);
    filename[name_len] = '\0';

    url_decode(filename);
}

// Simple tag finder - finds content between <tag> and </tag>
// Returns pointer to content start, sets content_len
static const char *find_tag_content(const char *xml, const char *tag, size_t *content_len) {
    char open_tag[128];
    char close_tag[128];

    // Handle namespaced tags (d:href, D:href, etc.)
    snprintf(open_tag, sizeof(open_tag), "<%s>", tag);
    snprintf(close_tag, sizeof(close_tag), "</%s>", tag);

    const char *start = strstr(xml, open_tag);
    if (!start) {
        // Try with different case/namespace
        return NULL;
    }

    start += strlen(open_tag);
    const char *end = strstr(start, close_tag);
    if (!end) {
        return NULL;
    }

    *content_len = end - start;
    return start;
}

// Check if a response element represents a collection (directory)
static int is_collection(const char *response_xml, size_t len) {
    // Look for <d:collection or <D:collection or resourcetype containing collection
    char temp[8192];
    if (len >= sizeof(temp)) len = sizeof(temp) - 1;
    strncpy(temp, response_xml, len);
    temp[len] = '\0';

    // Convert to lowercase for easier matching
    for (char *p = temp; *p; p++) {
        *p = tolower(*p);
    }

    return (strstr(temp, "collection") != NULL);
}

// Extract content length from response
static long long get_content_length(const char *response_xml, size_t len) {
    const char *patterns[] = {
        "<d:getcontentlength>",
        "<D:getcontentlength>",
        "<getcontentlength>",
        NULL
    };

    for (int i = 0; patterns[i]; i++) {
        const char *start = strstr(response_xml, patterns[i]);
        if (start && start < response_xml + len) {
            start += strlen(patterns[i]);
            return atoll(start);
        }
    }

    return 0;
}

// Extract href from response
static int get_href(const char *response_xml, size_t len, char *href, size_t href_len) {
    const char *patterns[] = {
        "<d:href>", "<D:href>", "<href>", NULL
    };
    const char *end_patterns[] = {
        "</d:href>", "</D:href>", "</href>", NULL
    };

    for (int i = 0; patterns[i]; i++) {
        const char *start = strstr(response_xml, patterns[i]);
        if (start && start < response_xml + len) {
            start += strlen(patterns[i]);
            const char *end = strstr(start, end_patterns[i]);
            if (end && end < response_xml + len) {
                size_t copy_len = end - start;
                if (copy_len >= href_len) copy_len = href_len - 1;
                strncpy(href, start, copy_len);
                href[copy_len] = '\0';
                return 0;
            }
        }
    }

    return -1;
}

// Extract last modified from response
static void get_last_modified(const char *response_xml, size_t len, char *modified, size_t mod_len) {
    const char *patterns[] = {
        "<d:getlastmodified>", "<D:getlastmodified>", "<getlastmodified>", NULL
    };

    modified[0] = '\0';

    for (int i = 0; patterns[i]; i++) {
        const char *start = strstr(response_xml, patterns[i]);
        if (start && start < response_xml + len) {
            start += strlen(patterns[i]);
            const char *end = strchr(start, '<');
            if (end && end < response_xml + len) {
                size_t copy_len = end - start;
                if (copy_len >= mod_len) copy_len = mod_len - 1;
                strncpy(modified, start, copy_len);
                modified[copy_len] = '\0';
                return;
            }
        }
    }
}

// Extract content type from response
static void get_content_type(const char *response_xml, size_t len, char *ctype, size_t ctype_len) {
    const char *patterns[] = {
        "<d:getcontenttype>", "<D:getcontenttype>", "<getcontenttype>", NULL
    };

    ctype[0] = '\0';

    for (int i = 0; patterns[i]; i++) {
        const char *start = strstr(response_xml, patterns[i]);
        if (start && start < response_xml + len) {
            start += strlen(patterns[i]);
            const char *end = strchr(start, '<');
            if (end && end < response_xml + len) {
                size_t copy_len = end - start;
                if (copy_len >= ctype_len) copy_len = ctype_len - 1;
                strncpy(ctype, start, copy_len);
                ctype[copy_len] = '\0';
                return;
            }
        }
    }
}

int parse_webdav_response(const char *xml, size_t xml_len, CloudFileList *list) {
    filelist_init(list);

    const char *pos = xml;
    const char *end = xml + xml_len;

    // Find each <d:response> or <D:response> element
    while (pos < end && list->count < MAX_CLOUD_ENTRIES) {
        const char *resp_start = strstr(pos, "<d:response>");
        if (!resp_start) {
            resp_start = strstr(pos, "<D:response>");
        }
        if (!resp_start || resp_start >= end) {
            break;
        }

        const char *resp_end = strstr(resp_start, "</d:response>");
        if (!resp_end) {
            resp_end = strstr(resp_start, "</D:response>");
        }
        if (!resp_end || resp_end >= end) {
            break;
        }

        size_t resp_len = resp_end - resp_start;

        CloudFileEntry *entry = &list->entries[list->count];
        memset(entry, 0, sizeof(CloudFileEntry));

        // Get href
        if (get_href(resp_start, resp_len, entry->href, sizeof(entry->href)) != 0) {
            pos = resp_end + 1;
            continue;
        }

        // Extract filename from href
        extract_filename(entry->href, entry->name, sizeof(entry->name));

        // Skip if empty name (root directory usually)
        if (entry->name[0] == '\0') {
            pos = resp_end + 1;
            continue;
        }

        // Determine if directory or file
        entry->type = is_collection(resp_start, resp_len) ? CLOUD_ENTRY_DIRECTORY : CLOUD_ENTRY_FILE;

        // Get file size (only for files)
        if (entry->type == CLOUD_ENTRY_FILE) {
            entry->size = get_content_length(resp_start, resp_len);
        }

        // Get last modified
        get_last_modified(resp_start, resp_len, entry->modified, sizeof(entry->modified));

        // Get content type
        get_content_type(resp_start, resp_len, entry->content_type, sizeof(entry->content_type));

        list->count++;
        pos = resp_end + 1;
    }

    return 0;
}
