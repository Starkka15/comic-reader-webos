#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Obfuscation key - not true security, just hides from casual viewing
static const char *OBF_KEY = "WebOS-Comic-Reader-2024";

// XOR obfuscate/deobfuscate (symmetric)
static void xor_obfuscate(const char *input, char *output, size_t len) {
    size_t key_len = strlen(OBF_KEY);
    for (size_t i = 0; i < len; i++) {
        output[i] = input[i] ^ OBF_KEY[i % key_len];
    }
}

// Base64 encoding table
static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Base64 encode
static void base64_encode(const unsigned char *input, size_t len, char *output) {
    size_t i, j;
    for (i = 0, j = 0; i < len; i += 3) {
        unsigned int val = input[i] << 16;
        if (i + 1 < len) val |= input[i + 1] << 8;
        if (i + 2 < len) val |= input[i + 2];

        output[j++] = b64_table[(val >> 18) & 0x3F];
        output[j++] = b64_table[(val >> 12) & 0x3F];
        output[j++] = (i + 1 < len) ? b64_table[(val >> 6) & 0x3F] : '=';
        output[j++] = (i + 2 < len) ? b64_table[val & 0x3F] : '=';
    }
    output[j] = '\0';
}

// Base64 decode
static int base64_decode(const char *input, unsigned char *output, size_t *out_len) {
    size_t len = strlen(input);
    if (len % 4 != 0) return -1;

    *out_len = len / 4 * 3;
    if (input[len - 1] == '=') (*out_len)--;
    if (input[len - 2] == '=') (*out_len)--;

    size_t i, j;
    for (i = 0, j = 0; i < len; i += 4) {
        int v[4];
        for (int k = 0; k < 4; k++) {
            char c = input[i + k];
            if (c >= 'A' && c <= 'Z') v[k] = c - 'A';
            else if (c >= 'a' && c <= 'z') v[k] = c - 'a' + 26;
            else if (c >= '0' && c <= '9') v[k] = c - '0' + 52;
            else if (c == '+') v[k] = 62;
            else if (c == '/') v[k] = 63;
            else if (c == '=') v[k] = 0;
            else return -1;
        }

        unsigned int val = (v[0] << 18) | (v[1] << 12) | (v[2] << 6) | v[3];
        if (j < *out_len) output[j++] = (val >> 16) & 0xFF;
        if (j < *out_len) output[j++] = (val >> 8) & 0xFF;
        if (j < *out_len) output[j++] = val & 0xFF;
    }
    return 0;
}

// Encode password for storage
static void encode_password(const char *plain, char *encoded, size_t encoded_len) {
    size_t len = strlen(plain);
    char *xored = malloc(len + 1);
    if (!xored) {
        encoded[0] = '\0';
        return;
    }
    xor_obfuscate(plain, xored, len);
    xored[len] = '\0';
    base64_encode((unsigned char *)xored, len, encoded);
    free(xored);
}

// Decode password from storage
static void decode_password(const char *encoded, char *plain, size_t plain_len) {
    size_t decoded_len;
    unsigned char *decoded = malloc(strlen(encoded));
    if (!decoded) {
        plain[0] = '\0';
        return;
    }

    if (base64_decode(encoded, decoded, &decoded_len) != 0) {
        plain[0] = '\0';
        free(decoded);
        return;
    }

    if (decoded_len >= plain_len) decoded_len = plain_len - 1;
    xor_obfuscate((char *)decoded, plain, decoded_len);
    plain[decoded_len] = '\0';
    free(decoded);
}

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
        } else if (strcmp(key, "password_enc") == 0) {
            // New encoded format
            decode_password(value, config->password, MAX_PASS_LEN);
        } else if (strcmp(key, "password") == 0) {
            // Legacy plain text format (for backward compatibility)
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
        // Encode password before saving
        char encoded_pass[MAX_PASS_LEN * 2];
        encode_password(config->password, encoded_pass, sizeof(encoded_pass));
        fprintf(f, "password_enc=%s\n", encoded_pass);
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
