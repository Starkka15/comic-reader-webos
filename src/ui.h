#ifndef UI_H
#define UI_H

#include <SDL.h>
#include <SDL_ttf.h>
#include "cbz.h"
#include "cache.h"
#include "config.h"
#include "xml_parser.h"

#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768

#define MAX_FILES 500
#ifndef MAX_PATH_LEN
#define MAX_PATH_LEN 512
#endif

// Orientation debounce time in milliseconds
#define ORIENTATION_DEBOUNCE_MS 400

typedef enum {
    SCREEN_BROWSER,
    SCREEN_READER,
    SCREEN_LOADING,
    SCREEN_ERROR,
    SCREEN_CLOUD_BROWSER,
    SCREEN_CLOUD_CONFIG
} ScreenState;

typedef enum {
    ENTRY_FILE,
    ENTRY_DIRECTORY,
    ENTRY_PARENT
} FileEntryType;

typedef struct {
    char name[256];
    char full_path[MAX_PATH_LEN];
    FileEntryType type;
} FileEntry;

typedef struct {
    SDL_Surface *screen;
    TTF_Font *font;
    TTF_Font *font_small;

    ScreenState state;
    char message[256];

    // File browser
    FileEntry files[MAX_FILES];
    int file_count;
    int selected_file;
    int scroll_offset;
    char current_dir[MAX_PATH_LEN];

    // Reader
    ComicBook comic;
    PageCache cache;
    int current_page;

    // Touch state
    int touch_start_x;
    int touch_start_y;
    int touch_moved;

    // Zoom/pan state
    float zoom;
    float pan_x;
    float pan_y;

    // Orientation (0=landscape, 1=portrait-left, 2=portrait-right)
    int orientation;
    int pending_orientation;         // Orientation we're considering switching to
    Uint32 orientation_change_time;  // When pending_orientation was first detected

    // Cloud browser state
    int browse_mode;                 // 0=local, 1=cloud
    char cloud_path[MAX_PATH_LEN];
    CloudFileList cloud_files;       // From xml_parser.h
    AppConfig cloud_config;          // From config.h
    int cloud_selected_file;
    int cloud_scroll_offset;
    int cloud_configured;            // 1 if config loaded successfully

    // Config screen input state
    int config_input_field;          // 0=server, 1=username, 2=password
    char input_server[512];
    char input_username[128];
    char input_password[256];
} UIState;

// Initialize/cleanup
int ui_init(UIState *ui);
void ui_cleanup(UIState *ui);

// Event handling - returns action code
int ui_handle_event(UIState *ui, SDL_Event *event);

// Rendering
void ui_render(UIState *ui);

// File browser
int ui_scan_directory(UIState *ui, const char *path);

// Reader
int ui_open_comic(UIState *ui, const char *filepath);
void ui_close_comic(UIState *ui);
void ui_next_page(UIState *ui);
void ui_prev_page(UIState *ui);
void ui_goto_page(UIState *ui, int page);

// Screen management
void ui_set_screen(UIState *ui, ScreenState state);
void ui_set_message(UIState *ui, const char *message);

// Orientation
void ui_poll_orientation(UIState *ui);

// Cloud browser
int ui_scan_cloud_directory(UIState *ui, const char *path);
int ui_download_comic(UIState *ui, const char *remote_path, char *local_path, size_t local_path_len);
void ui_load_cloud_config(UIState *ui);
void ui_save_cloud_config(UIState *ui);

#endif
