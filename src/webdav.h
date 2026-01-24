#ifndef WEBDAV_H
#define WEBDAV_H

#include "config.h"
#include "xml_parser.h"

// Initialize WebDAV/curl subsystem
int webdav_init(void);

// Cleanup WebDAV/curl subsystem
void webdav_cleanup(void);

// Test connection and authentication
// Returns 0 on success, -1 on failure
int webdav_test_connection(const AppConfig *config);

// List directory contents
// Returns 0 on success, -1 on failure
int webdav_list_directory(const AppConfig *config, const char *path, CloudFileList *list);

// Download a file
// Returns 0 on success, -1 on failure
// progress_callback can be NULL; called with (downloaded, total)
typedef void (*ProgressCallback)(long long downloaded, long long total, void *userdata);
int webdav_download_file(const AppConfig *config, const char *remote_path,
                         const char *local_path, ProgressCallback progress, void *userdata);

// Upload a file
// Returns 0 on success, -1 on failure
int webdav_upload_file(const AppConfig *config, const char *local_path,
                       const char *remote_path, ProgressCallback progress, void *userdata);

// Create directory
// Returns 0 on success, -1 on failure
int webdav_create_directory(const AppConfig *config, const char *path);

// Delete file or directory
// Returns 0 on success, -1 on failure
int webdav_delete(const AppConfig *config, const char *path);

// Get last error message
const char *webdav_get_error(void);

#endif /* WEBDAV_H */
