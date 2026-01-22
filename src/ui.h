#ifndef UI_H
#define UI_H

#include <SDL.h>
#include <SDL_ttf.h>
#include "cbz.h"
#include "cache.h"

#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768

#define MAX_FILES 500
#define MAX_PATH_LEN 512

typedef enum {
    SCREEN_BROWSER,
    SCREEN_READER,
    SCREEN_LOADING,
    SCREEN_ERROR
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

    // Multi-touch state (for pinch zoom)
    SDL_Joystick *touchpad;
    int touch1_x, touch1_y;
    int touch2_x, touch2_y;
    int pinch_active;
    float pinch_start_dist;
    float pinch_start_zoom;

    // Zoom/pan state
    float zoom;
    float pan_x;
    float pan_y;
    int rotation;  // 0, 90, 180, 270 degrees
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

#endif
