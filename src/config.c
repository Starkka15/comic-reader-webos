#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// URL-encode a string (for username with @ etc)
static void url_encode(const char *src, char *dest, size_t dest_len) {
    const char *hex = "0123456789ABCDEF";
    size_t j = 0;

    for (size_t i = 0; src[i] && j < dest_len - 3; i++) {
        unsigned char c = (unsigned char)src[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            dest[j++] = c;
        } else {
            dest[j++] = '%';
            dest[j++] = hex[(c >> 4) & 0xF];
            dest[j++] = hex[c & 0xF];
        }
    }
    dest[j] = '\0';
}

// URL-encode a path, preserving slashes
static void url_encode_path(const char *src, char *dest, size_t dest_len) {
    const char *hex = "0123456789ABCDEF";
    size_t j = 0;

    for (size_t i = 0; src[i] && j < dest_len - 3; i++) {
        unsigned char c = (unsigned char)src[i];
        // Keep slashes, alphanumeric, and safe chars
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
            dest[j++] = c;
        } else {
            dest[j++] = '%';
            dest[j++] = hex[(c >> 4) & 0xF];
            dest[j++] = hex[c & 0xF];
        }
    }
    dest[j] = '\0';
}

void config_init(AppConfig *config) {
    memset(config, 0, sizeof(AppConfig));
    strcpy(config->current_path, "/");
    config->remember_password = 0;
}

int config_load(AppConfig *config, const char *filepath) {
    FILE *f = fopen(filepath, "r");
    if (!f) {
        return -1;
    }

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        // Remove newline
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        // Parse key=value
        char *eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = line;
        char *value = eq + 1;

        if (strcmp(key, "server_url") == 0) {
            strncpy(config->server_url, value, MAX_URL_LEN - 1);
        } else if (strcmp(key, "username") == 0) {
            strncpy(config->username, value, MAX_USER_LEN - 1);
        } else if (strcmp(key, "password") == 0) {
            strncpy(config->password, value, MAX_PASS_LEN - 1);
        } else if (strcmp(key, "remember_password") == 0) {
            config->remember_password = atoi(value);
        }
    }

    fclose(f);
    return 0;
}

int config_save(const AppConfig *config, const char *filepath) {
    FILE *f = fopen(filepath, "w");
    if (!f) {
        return -1;
    }

    fprintf(f, "server_url=%s\n", config->server_url);
    fprintf(f, "username=%s\n", config->username);

    if (config->remember_password) {
        fprintf(f, "password=%s\n", config->password);
        fprintf(f, "remember_password=1\n");
    } else {
        fprintf(f, "remember_password=0\n");
    }

    fclose(f);
    return 0;
}

void config_build_webdav_url(const AppConfig *config, const char *path, char *out_url, size_t out_len) {
    // Nextcloud WebDAV endpoint: /remote.php/dav/files/USERNAME/path
    // URL-encode username (handles @ in email addresses)
    char encoded_user[MAX_USER_LEN * 3];
    url_encode(config->username, encoded_user, sizeof(encoded_user));

    // URL-encode path (handles spaces, parentheses, brackets, etc.)
    char encoded_path[MAX_PATH_LEN * 3];
    url_encode_path(path, encoded_path, sizeof(encoded_path));

    snprintf(out_url, out_len, "%s/remote.php/dav/files/%s%s",
             config->server_url,
             encoded_user,
             encoded_path);
}
