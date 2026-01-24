#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

#define MAX_URL_LEN 512
#define MAX_USER_LEN 128
#define MAX_PASS_LEN 256
#define MAX_PATH_LEN 1024

typedef struct {
    char server_url[MAX_URL_LEN];    // e.g., "https://cloud.example.com"
    char username[MAX_USER_LEN];
    char password[MAX_PASS_LEN];
    char current_path[MAX_PATH_LEN]; // Current browsing path
    int remember_password;           // 1 = save password
} AppConfig;

// Initialize config with defaults
void config_init(AppConfig *config);

// Load config from file
int config_load(AppConfig *config, const char *filepath);

// Save config to file
int config_save(const AppConfig *config, const char *filepath);

// Build WebDAV URL for a given path
void config_build_webdav_url(const AppConfig *config, const char *path, char *out_url, size_t out_len);

#endif /* CONFIG_H */
