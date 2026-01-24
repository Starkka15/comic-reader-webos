#include "webdav.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char error_buffer[256] = {0};
static CURL *curl_handle = NULL;

// Buffer for receiving data
typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} ResponseBuffer;

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    ResponseBuffer *buf = (ResponseBuffer *)userp;

    // Grow buffer if needed
    while (buf->size + realsize + 1 > buf->capacity) {
        buf->capacity = buf->capacity ? buf->capacity * 2 : 4096;
        char *new_data = realloc(buf->data, buf->capacity);
        if (!new_data) {
            return 0; // Out of memory
        }
        buf->data = new_data;
    }

    memcpy(buf->data + buf->size, contents, realsize);
    buf->size += realsize;
    buf->data[buf->size] = '\0';

    return realsize;
}

// Progress callback wrapper
typedef struct {
    ProgressCallback callback;
    void *userdata;
} ProgressData;

static int progress_callback(void *clientp, double dltotal, double dlnow,
                            double ultotal, double ulnow) {
    ProgressData *pd = (ProgressData *)clientp;
    if (pd && pd->callback) {
        if (dltotal > 0) {
            pd->callback((long long)dlnow, (long long)dltotal, pd->userdata);
        } else if (ultotal > 0) {
            pd->callback((long long)ulnow, (long long)ultotal, pd->userdata);
        }
    }
    return 0; // Continue
}

// File write callback for downloads
static size_t file_write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    FILE *f = (FILE *)userp;
    return fwrite(contents, size, nmemb, f);
}

// File read callback for uploads
static size_t file_read_callback(void *ptr, size_t size, size_t nmemb, void *userp) {
    FILE *f = (FILE *)userp;
    return fread(ptr, size, nmemb, f);
}

int webdav_init(void) {
    if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
        strcpy(error_buffer, "Failed to initialize curl");
        return -1;
    }

    curl_handle = curl_easy_init();
    if (!curl_handle) {
        strcpy(error_buffer, "Failed to create curl handle");
        return -1;
    }

    return 0;
}

void webdav_cleanup(void) {
    if (curl_handle) {
        curl_easy_cleanup(curl_handle);
        curl_handle = NULL;
    }
    curl_global_cleanup();
}

static void setup_curl_common(CURL *curl, const AppConfig *config, const char *url) {
    curl_easy_reset(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERNAME, config->username);
    curl_easy_setopt(curl, CURLOPT_PASSWORD, config->password);
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);

    // SSL options - accept self-signed certs (common for home servers)
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    // Timeouts
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);

    // Follow redirects
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
}

int webdav_test_connection(const AppConfig *config) {
    char url[MAX_URL_LEN * 2];
    config_build_webdav_url(config, "/", url, sizeof(url));


    ResponseBuffer buf = {0};

    setup_curl_common(curl_handle, config, url);
    curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "PROPFIND");
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &buf);

    // Set Depth header for PROPFIND
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Depth: 0");
    headers = curl_slist_append(headers, "Content-Type: application/xml");
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl_handle);

    long http_code = 0;
    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    free(buf.data);

    if (res != CURLE_OK) {
        snprintf(error_buffer, sizeof(error_buffer), "Connection failed: %s",
                 curl_easy_strerror(res));
        return -1;
    }

    if (http_code == 401) {
        strcpy(error_buffer, "Authentication failed");
        return -1;
    }

    if (http_code < 200 || http_code >= 300) {
        snprintf(error_buffer, sizeof(error_buffer), "Server error: HTTP %ld", http_code);
        return -1;
    }

    return 0;
}

int webdav_list_directory(const AppConfig *config, const char *path, CloudFileList *list) {
    char url[MAX_URL_LEN * 2];
    config_build_webdav_url(config, path, url, sizeof(url));

    ResponseBuffer buf = {0};

    setup_curl_common(curl_handle, config, url);
    curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "PROPFIND");
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &buf);

    // Request specific properties
    const char *propfind_body =
        "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
        "<d:propfind xmlns:d=\"DAV:\">"
        "<d:prop>"
        "<d:resourcetype/>"
        "<d:getcontentlength/>"
        "<d:getlastmodified/>"
        "<d:getcontenttype/>"
        "</d:prop>"
        "</d:propfind>";

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Depth: 1");
    headers = curl_slist_append(headers, "Content-Type: application/xml");
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, propfind_body);

    CURLcode res = curl_easy_perform(curl_handle);

    curl_slist_free_all(headers);

    long http_code = 0;
    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code);

    if (res != CURLE_OK) {
        snprintf(error_buffer, sizeof(error_buffer), "Request failed: %s",
                 curl_easy_strerror(res));
        free(buf.data);
        return -1;
    }

    if (http_code < 200 || http_code >= 300) {
        snprintf(error_buffer, sizeof(error_buffer), "Server error: HTTP %ld", http_code);
        free(buf.data);
        return -1;
    }

    // Parse the response
    if (buf.data && buf.size > 0) {
        parse_webdav_response(buf.data, buf.size, list);
    }

    free(buf.data);
    return 0;
}

int webdav_download_file(const AppConfig *config, const char *remote_path,
                         const char *local_path, ProgressCallback progress, void *userdata) {
    char url[MAX_URL_LEN * 2];
    config_build_webdav_url(config, remote_path, url, sizeof(url));

    FILE *f = fopen(local_path, "wb");
    if (!f) {
        snprintf(error_buffer, sizeof(error_buffer), "Cannot create file: %s", local_path);
        return -1;
    }

    ProgressData pd = {progress, userdata};

    setup_curl_common(curl_handle, config, url);
    curl_easy_setopt(curl_handle, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, file_write_callback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, f);

    if (progress) {
        curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl_handle, CURLOPT_PROGRESSFUNCTION, progress_callback);
        curl_easy_setopt(curl_handle, CURLOPT_PROGRESSDATA, &pd);
    }

    CURLcode res = curl_easy_perform(curl_handle);
    fclose(f);

    if (res != CURLE_OK) {
        snprintf(error_buffer, sizeof(error_buffer), "Download failed: %s",
                 curl_easy_strerror(res));
        remove(local_path);
        return -1;
    }

    long http_code = 0;
    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code);

    if (http_code < 200 || http_code >= 300) {
        snprintf(error_buffer, sizeof(error_buffer), "Download failed: HTTP %ld", http_code);
        remove(local_path);
        return -1;
    }

    return 0;
}

int webdav_upload_file(const AppConfig *config, const char *local_path,
                       const char *remote_path, ProgressCallback progress, void *userdata) {
    char url[MAX_URL_LEN * 2];
    config_build_webdav_url(config, remote_path, url, sizeof(url));

    FILE *f = fopen(local_path, "rb");
    if (!f) {
        snprintf(error_buffer, sizeof(error_buffer), "Cannot open file: %s", local_path);
        return -1;
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    ProgressData pd = {progress, userdata};

    setup_curl_common(curl_handle, config, url);
    curl_easy_setopt(curl_handle, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_READFUNCTION, file_read_callback);
    curl_easy_setopt(curl_handle, CURLOPT_READDATA, f);
    curl_easy_setopt(curl_handle, CURLOPT_INFILESIZE_LARGE, (curl_off_t)file_size);

    if (progress) {
        curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl_handle, CURLOPT_PROGRESSFUNCTION, progress_callback);
        curl_easy_setopt(curl_handle, CURLOPT_PROGRESSDATA, &pd);
    }

    CURLcode res = curl_easy_perform(curl_handle);
    fclose(f);

    if (res != CURLE_OK) {
        snprintf(error_buffer, sizeof(error_buffer), "Upload failed: %s",
                 curl_easy_strerror(res));
        return -1;
    }

    long http_code = 0;
    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code);

    if (http_code < 200 || http_code >= 300) {
        snprintf(error_buffer, sizeof(error_buffer), "Upload failed: HTTP %ld", http_code);
        return -1;
    }

    return 0;
}

int webdav_create_directory(const AppConfig *config, const char *path) {
    char url[MAX_URL_LEN * 2];
    config_build_webdav_url(config, path, url, sizeof(url));

    setup_curl_common(curl_handle, config, url);
    curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "MKCOL");

    CURLcode res = curl_easy_perform(curl_handle);

    if (res != CURLE_OK) {
        snprintf(error_buffer, sizeof(error_buffer), "Create directory failed: %s",
                 curl_easy_strerror(res));
        return -1;
    }

    long http_code = 0;
    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code);

    if (http_code < 200 || http_code >= 300) {
        snprintf(error_buffer, sizeof(error_buffer), "Create directory failed: HTTP %ld", http_code);
        return -1;
    }

    return 0;
}

int webdav_delete(const AppConfig *config, const char *path) {
    char url[MAX_URL_LEN * 2];
    config_build_webdav_url(config, path, url, sizeof(url));

    setup_curl_common(curl_handle, config, url);
    curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "DELETE");

    CURLcode res = curl_easy_perform(curl_handle);

    if (res != CURLE_OK) {
        snprintf(error_buffer, sizeof(error_buffer), "Delete failed: %s",
                 curl_easy_strerror(res));
        return -1;
    }

    long http_code = 0;
    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code);

    if (http_code < 200 || http_code >= 300) {
        snprintf(error_buffer, sizeof(error_buffer), "Delete failed: HTTP %ld", http_code);
        return -1;
    }

    return 0;
}

const char *webdav_get_error(void) {
    return error_buffer;
}
