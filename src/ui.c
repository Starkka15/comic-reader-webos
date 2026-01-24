#include "ui.h"
#include "webdav.h"
#include <PDL.h>
#include <PDL_Sensors.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

// Cache directory for downloaded comics
#define CLOUD_CACHE_DIR "/media/internal/.comic-cache"
#define CONFIG_FILE_PATH "/media/internal/.comic-reader/config.txt"

// Colors
static SDL_Color COLOR_WHITE = {255, 255, 255, 255};
static SDL_Color COLOR_BLACK = {0, 0, 0, 255};
static SDL_Color COLOR_GRAY = {128, 128, 128, 255};
static SDL_Color COLOR_DARK_GRAY = {40, 40, 40, 255};
static SDL_Color COLOR_BLUE = {70, 130, 180, 255};
static SDL_Color COLOR_YELLOW = {255, 200, 0, 255};

static const char *FONT_PATHS[] = {
    "/usr/share/fonts/Prelude-Medium.ttf",
    "/usr/share/fonts/PreludeCondensed-Medium.ttf",
    NULL
};

// Portrait mode render surface (768x1024)
static SDL_Surface *portrait_surface = NULL;

static TTF_Font *load_font(int size) {
    for (int i = 0; FONT_PATHS[i]; i++) {
        TTF_Font *font = TTF_OpenFont(FONT_PATHS[i], size);
        if (font) return font;
    }
    return NULL;
}

// Get virtual screen dimensions based on orientation
static void get_virtual_size(UIState *ui, int *w, int *h) {
    if (ui->orientation == 0) {
        *w = SCREEN_WIDTH;   // 1024
        *h = SCREEN_HEIGHT;  // 768
    } else {
        *w = SCREEN_HEIGHT;  // 768
        *h = SCREEN_WIDTH;   // 1024
    }
}

// Get the surface to render to (screen for landscape, portrait_surface for portrait)
static SDL_Surface *get_render_surface(UIState *ui) {
    if (ui->orientation == 0) {
        return ui->screen;
    }
    // Create portrait surface if needed
    if (!portrait_surface) {
        portrait_surface = SDL_CreateRGBSurface(SDL_SWSURFACE,
            SCREEN_HEIGHT, SCREEN_WIDTH, 32,  // 768x1024
            ui->screen->format->Rmask, ui->screen->format->Gmask,
            ui->screen->format->Bmask, ui->screen->format->Amask);
    }
    return portrait_surface;
}

int ui_init(UIState *ui) {
    memset(ui, 0, sizeof(UIState));

    ui->screen = SDL_SetVideoMode(SCREEN_WIDTH, SCREEN_HEIGHT, 32, SDL_SWSURFACE);
    if (!ui->screen) {
        fprintf(stderr, "SDL_SetVideoMode failed: %s\n", SDL_GetError());
        return -1;
    }

    // Enable unicode for virtual keyboard input
    SDL_EnableUNICODE(1);

    if (TTF_Init() < 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        return -1;
    }

    ui->font = load_font(22);
    ui->font_small = load_font(16);

    if (!ui->font || !ui->font_small) {
        fprintf(stderr, "Failed to load fonts\n");
        return -1;
    }

    ui->state = SCREEN_BROWSER;
    ui->zoom = 1.0f;
    ui->orientation = 0;  // Landscape
    ui->pending_orientation = 0;
    ui->orientation_change_time = 0;
    strcpy(ui->current_dir, "/media/internal/comics");

    // Cloud browser state
    ui->browse_mode = 0;  // Start in local mode
    strcpy(ui->cloud_path, "/");
    ui->cloud_configured = 0;
    config_init(&ui->cloud_config);

    // Enable orientation sensor
    if (PDL_SensorExists(PDL_SENSOR_ORIENTATION)) {
        PDL_EnableSensor(PDL_SENSOR_ORIENTATION, PDL_TRUE);
        printf("Orientation sensor enabled\n");
    }

    // Create cache directory if needed
    mkdir(CLOUD_CACHE_DIR, 0755);
    mkdir("/media/internal/.comic-reader", 0755);

    return 0;
}

// Poll orientation sensor and update state with debounce
void ui_poll_orientation(UIState *ui) {
    PDL_SensorEvent sensor_event;

    // Check for orientation sensor events
    while (PDL_PollSensor(PDL_SENSOR_ORIENTATION, &sensor_event) == PDL_NOERROR &&
           sensor_event.type != PDL_SENSOR_NONE) {

        int raw_orientation = sensor_event.orientation.orientation;
        int new_orientation;

        // Map PDL orientation to our values
        // TouchPad PDL orientations:
        //   NORMAL (3) = portrait, home button at bottom
        //   UP_SIDE_DOWN (4) = portrait, home button at top
        //   LEFT_SIDE_DOWN (5) = landscape, home button on left
        //   RIGHT_SIDE_DOWN (6) = landscape, home button on right
        // Our values: 0=landscape (no rotation), 1=portrait-left, 2=portrait-right
        switch (raw_orientation) {
            case PDL_SENSOR_ORIENTATION_NORMAL:  // Portrait, home at bottom
                new_orientation = 2;
                break;
            case PDL_SENSOR_ORIENTATION_UP_SIDE_DOWN:  // Portrait, home at top
                new_orientation = 1;
                break;
            case PDL_SENSOR_ORIENTATION_LEFT_SIDE_DOWN:  // Landscape
            case PDL_SENSOR_ORIENTATION_RIGHT_SIDE_DOWN:  // Landscape
            default:
                new_orientation = 0;
                break;
        }

        Uint32 now = SDL_GetTicks();

        if (new_orientation != ui->orientation) {
            if (new_orientation != ui->pending_orientation) {
                // New candidate orientation - start timing
                ui->pending_orientation = new_orientation;
                ui->orientation_change_time = now;
            } else if (now - ui->orientation_change_time >= ORIENTATION_DEBOUNCE_MS) {
                // Stable for long enough - apply change
                ui->orientation = new_orientation;
                printf("Orientation: %s\n",
                       new_orientation == 0 ? "Landscape" :
                       new_orientation == 1 ? "Portrait Inverted" : "Portrait");
            }
            // else: still waiting for debounce period
        } else {
            // Current orientation confirmed - reset pending
            ui->pending_orientation = ui->orientation;
        }
    }
}

void ui_cleanup(UIState *ui) {
    // Disable orientation sensor
    if (PDL_SensorExists(PDL_SENSOR_ORIENTATION)) {
        PDL_EnableSensor(PDL_SENSOR_ORIENTATION, PDL_FALSE);
    }

    // Free portrait surface
    if (portrait_surface) {
        SDL_FreeSurface(portrait_surface);
        portrait_surface = NULL;
    }

    ui_close_comic(ui);
    if (ui->font) TTF_CloseFont(ui->font);
    if (ui->font_small) TTF_CloseFont(ui->font_small);
    TTF_Quit();
}

void ui_set_screen(UIState *ui, ScreenState state) {
    ui->state = state;
    ui->scroll_offset = 0;
}

void ui_set_message(UIState *ui, const char *message) {
    strncpy(ui->message, message, sizeof(ui->message) - 1);
}

static void draw_text(SDL_Surface *screen, TTF_Font *font, const char *text,
                      int x, int y, SDL_Color color) {
    if (!text || !text[0]) return;
    SDL_Surface *surface = TTF_RenderText_Blended(font, text, color);
    if (surface) {
        SDL_Rect dest = {x, y, 0, 0};
        SDL_BlitSurface(surface, NULL, screen, &dest);
        SDL_FreeSurface(surface);
    }
}

static void draw_rect(SDL_Surface *screen, int x, int y, int w, int h, SDL_Color color) {
    SDL_Rect rect = {x, y, w, h};
    SDL_FillRect(screen, &rect, SDL_MapRGB(screen->format, color.r, color.g, color.b));
}

// Blit portrait surface (768x1024) to screen (1024x768) with rotation
// orientation: 1 = 90° CCW, 2 = 90° CW
static void blit_portrait_to_screen(SDL_Surface *portrait, SDL_Surface *screen, int orientation) {
    if (!portrait || !screen) return;

    int src_w = portrait->w;   // 768
    int src_h = portrait->h;   // 1024
    int dst_w = screen->w;     // 1024
    int dst_h = screen->h;     // 768
    int bpp = portrait->format->BytesPerPixel;

    SDL_LockSurface(portrait);
    SDL_LockSurface(screen);

    Uint8 *src = (Uint8 *)portrait->pixels;
    Uint8 *dst = (Uint8 *)screen->pixels;

    // Portrait (768x1024) rotated 90° fits perfectly in landscape (1024x768)
    // After rotation: 1024 wide, 768 tall - exact fit!

    if (orientation == 1) {
        // 90° CCW: portrait(x,y) → screen(y, src_w-1-x)
        // Reverse: screen(dx,dy) ← portrait(src_w-1-dy, dx)
        for (int dy = 0; dy < dst_h; dy++) {
            Uint8 *dst_row = dst + dy * screen->pitch;
            for (int dx = 0; dx < dst_w; dx++) {
                int sx = src_w - 1 - dy;
                int sy = dx;
                if (sx >= 0 && sx < src_w && sy >= 0 && sy < src_h) {
                    Uint8 *src_pixel = src + sy * portrait->pitch + sx * bpp;
                    Uint8 *dst_pixel = dst_row + dx * bpp;
                    memcpy(dst_pixel, src_pixel, bpp);
                }
            }
        }
    } else if (orientation == 2) {
        // 90° CW: portrait(x,y) → screen(src_h-1-y, x)
        // Reverse: screen(dx,dy) ← portrait(dy, src_h-1-dx)
        for (int dy = 0; dy < dst_h; dy++) {
            Uint8 *dst_row = dst + dy * screen->pitch;
            for (int dx = 0; dx < dst_w; dx++) {
                int sx = dy;
                int sy = src_h - 1 - dx;
                if (sx >= 0 && sx < src_w && sy >= 0 && sy < src_h) {
                    Uint8 *src_pixel = src + sy * portrait->pitch + sx * bpp;
                    Uint8 *dst_pixel = dst_row + dx * bpp;
                    memcpy(dst_pixel, src_pixel, bpp);
                }
            }
        }
    }

    SDL_UnlockSurface(screen);
    SDL_UnlockSurface(portrait);
}

// Transform physical touch coordinates to virtual (pre-rotation) coordinates
// Physical screen: 1024x768, Virtual portrait: 768x1024
static void transform_touch(UIState *ui, int *x, int *y) {
    int px = *x;
    int py = *y;

    if (ui->orientation == 0) return;

    // Portrait surface is 768x1024, displayed rotated on 1024x768 screen
    // Perfect fit - no scaling needed, just rotation transform

    if (ui->orientation == 1) {
        // 90° CCW was applied: portrait(x,y) → screen(y, 768-1-x)
        // Reverse: screen(px,py) → portrait(768-1-py, px)
        *x = SCREEN_HEIGHT - 1 - py;  // 768-1-py
        *y = px;
    } else if (ui->orientation == 2) {
        // 90° CW was applied: portrait(x,y) → screen(1024-1-y, x)
        // Reverse: screen(px,py) → portrait(py, 1024-1-px)
        *x = py;
        *y = SCREEN_WIDTH - 1 - px;  // 1024-1-px
    }
}

// Compare for sorting files
static int compare_files(const void *a, const void *b) {
    const FileEntry *fa = (const FileEntry *)a;
    const FileEntry *fb = (const FileEntry *)b;

    // Parent dir first
    if (fa->type == ENTRY_PARENT) return -1;
    if (fb->type == ENTRY_PARENT) return 1;

    // Directories before files
    if (fa->type == ENTRY_DIRECTORY && fb->type != ENTRY_DIRECTORY) return -1;
    if (fb->type == ENTRY_DIRECTORY && fa->type != ENTRY_DIRECTORY) return 1;

    // Alphabetical
    return strcasecmp(fa->name, fb->name);
}

static int is_comic_file(const char *name) {
    const char *ext = strrchr(name, '.');
    if (!ext) return 0;
    return (strcasecmp(ext, ".cbz") == 0 || strcasecmp(ext, ".zip") == 0 ||
            strcasecmp(ext, ".cbr") == 0 || strcasecmp(ext, ".rar") == 0);
}

int ui_scan_directory(UIState *ui, const char *path) {
    DIR *dir = opendir(path);
    if (!dir) {
        fprintf(stderr, "Cannot open directory: %s\n", path);
        return -1;
    }

    strncpy(ui->current_dir, path, sizeof(ui->current_dir) - 1);
    ui->file_count = 0;
    ui->selected_file = 0;
    ui->scroll_offset = 0;

    // Add parent directory entry if not at root
    if (strcmp(path, "/") != 0) {
        FileEntry *entry = &ui->files[ui->file_count++];
        strcpy(entry->name, "..");
        entry->type = ENTRY_PARENT;

        // Calculate parent path
        strcpy(entry->full_path, path);
        char *last_slash = strrchr(entry->full_path, '/');
        if (last_slash && last_slash != entry->full_path) {
            *last_slash = '\0';
        } else {
            strcpy(entry->full_path, "/");
        }
    }

    struct dirent *de;
    while ((de = readdir(dir)) && ui->file_count < MAX_FILES) {
        // Skip hidden files
        if (de->d_name[0] == '.') continue;

        char full_path[MAX_PATH_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, de->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        FileEntry *entry = &ui->files[ui->file_count];
        strncpy(entry->name, de->d_name, sizeof(entry->name) - 1);
        strncpy(entry->full_path, full_path, sizeof(entry->full_path) - 1);

        if (S_ISDIR(st.st_mode)) {
            entry->type = ENTRY_DIRECTORY;
            ui->file_count++;
        } else if (is_comic_file(de->d_name)) {
            entry->type = ENTRY_FILE;
            ui->file_count++;
        }
        // Skip non-comic files
    }

    closedir(dir);

    // Sort: parent, directories, files
    qsort(ui->files, ui->file_count, sizeof(FileEntry), compare_files);

    return 0;
}

int ui_open_comic(UIState *ui, const char *filepath) {
    ui_set_screen(ui, SCREEN_LOADING);
    ui_set_message(ui, "Opening comic...");
    ui_render(ui);

    if (cbz_open(&ui->comic, filepath) != 0) {
        ui_set_message(ui, "Failed to open comic");
        ui_set_screen(ui, SCREEN_ERROR);
        return -1;
    }

    cache_init(&ui->cache, &ui->comic);
    ui->current_page = 0;
    ui->zoom = 1.0f;
    ui->pan_x = 0;
    ui->pan_y = 0;
    ui_set_screen(ui, SCREEN_READER);

    return 0;
}

void ui_close_comic(UIState *ui) {
    cache_clear(&ui->cache);
    cbz_close(&ui->comic);
    ui->current_page = 0;
}

static void reset_view(UIState *ui) {
    ui->zoom = 1.0f;
    ui->pan_x = 0;
    ui->pan_y = 0;
    // Keep rotation - user might want consistent rotation for whole comic
}

void ui_next_page(UIState *ui) {
    if (ui->current_page < ui->comic.page_count - 1) {
        ui->current_page++;
        reset_view(ui);
    }
}

void ui_prev_page(UIState *ui) {
    if (ui->current_page > 0) {
        ui->current_page--;
        reset_view(ui);
    }
}

void ui_goto_page(UIState *ui, int page) {
    if (page >= 0 && page < ui->comic.page_count) {
        ui->current_page = page;
    }
}

static void render_browser(UIState *ui, SDL_Surface *surface, int vw, int vh) {
    // Header
    draw_rect(surface, 0, 0, vw, 50, COLOR_BLUE);
    draw_text(surface, ui->font, "Comic Reader", 20, 12, COLOR_WHITE);

    // Current path
    char path_display[64];
    snprintf(path_display, sizeof(path_display), "%.45s", ui->current_dir);
    draw_text(surface, ui->font_small, path_display, 180, 16, COLOR_WHITE);

    // Cloud button
    draw_rect(surface, vw - 90, 8, 80, 34, COLOR_DARK_GRAY);
    draw_text(surface, ui->font_small, "Cloud", vw - 75, 14, COLOR_YELLOW);

    // File list
    int y = 60 - ui->scroll_offset;
    int item_height = 50;

    for (int i = 0; i < ui->file_count; i++) {
        if (y + item_height < 50) {
            y += item_height;
            continue;
        }
        if (y > vh) break;

        FileEntry *entry = &ui->files[i];

        // Selection highlight
        if (i == ui->selected_file) {
            draw_rect(surface, 0, y, vw, item_height - 2, COLOR_DARK_GRAY);
        }

        // Icon/indicator
        const char *icon = "";
        SDL_Color name_color = COLOR_WHITE;

        if (entry->type == ENTRY_PARENT) {
            icon = "[..]";
            name_color = COLOR_YELLOW;
        } else if (entry->type == ENTRY_DIRECTORY) {
            icon = "[D]";
            name_color = COLOR_YELLOW;
        } else {
            icon = "[C]";
            name_color = COLOR_WHITE;
        }

        draw_text(surface, ui->font, icon, 15, y + 12, COLOR_GRAY);
        draw_text(surface, ui->font, entry->name, 70, y + 12, name_color);

        y += item_height;
    }

    // Scroll indicator
    if (ui->file_count > 12) {
        int total_height = ui->file_count * item_height;
        int visible_height = vh - 50;
        int bar_height = (visible_height * visible_height) / total_height;
        if (bar_height < 30) bar_height = 30;
        int bar_y = 50 + (ui->scroll_offset * (visible_height - bar_height)) / (total_height - visible_height);
        draw_rect(surface, vw - 8, bar_y, 6, bar_height, COLOR_GRAY);
    }

    // Instructions
    draw_rect(surface, 0, vh - 30, vw, 30, COLOR_DARK_GRAY);
    draw_text(surface, ui->font_small, "Tap to select | Swipe to scroll", 20, vh - 24, COLOR_GRAY);
}

// Scaled blit helper (no rotation - rotation handled by whole-screen rotate)
static void blit_scaled(SDL_Surface *src, SDL_Surface *dst,
                        int dst_x, int dst_y, int dst_w, int dst_h) {
    SDL_LockSurface(src);
    SDL_LockSurface(dst);

    int bpp = src->format->BytesPerPixel;
    int dst_bpp = dst->format->BytesPerPixel;
    float scale_x = (float)src->w / dst_w;
    float scale_y = (float)src->h / dst_h;

    for (int dy = 0; dy < dst_h; dy++) {
        int sy = (int)(dy * scale_y);
        if (sy >= src->h) sy = src->h - 1;
        Uint8 *src_row = (Uint8 *)src->pixels + sy * src->pitch;
        Uint8 *dst_row = (Uint8 *)dst->pixels + (dst_y + dy) * dst->pitch;

        for (int dx = 0; dx < dst_w; dx++) {
            int sx = (int)(dx * scale_x);
            if (sx >= src->w) sx = src->w - 1;
            Uint8 *src_pixel = src_row + sx * bpp;
            Uint8 *dst_pixel = dst_row + (dst_x + dx) * dst_bpp;

            if (bpp == 4 && dst_bpp == 4) {
                *(Uint32 *)dst_pixel = *(Uint32 *)src_pixel;
            } else if (bpp >= 3) {
                dst_pixel[0] = src_pixel[0];
                dst_pixel[1] = src_pixel[1];
                dst_pixel[2] = src_pixel[2];
            }
        }
    }

    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(src);
}

static void render_reader(UIState *ui, SDL_Surface *surface, int vw, int vh) {
    // Get current page
    SDL_Surface *page = cache_get_page(&ui->cache, ui->current_page);

    if (page) {
        int view_w = vw;
        int view_h = vh - 40;  // Account for status bar

        if (ui->zoom <= 1.01f) {
            // Normal view - scale to fit screen
            float scale_x = (float)view_w / page->w;
            float scale_y = (float)view_h / page->h;
            float scale = (scale_x < scale_y) ? scale_x : scale_y;
            if (scale > 1.0f) scale = 1.0f;  // Don't upscale

            int dst_w = (int)(page->w * scale);
            int dst_h = (int)(page->h * scale);
            int dst_x = (view_w - dst_w) / 2;
            int dst_y = (view_h - dst_h) / 2;

            if (scale >= 0.99f) {
                // No scaling needed - direct blit
                SDL_Rect dest = {dst_x, dst_y, 0, 0};
                SDL_BlitSurface(page, NULL, surface, &dest);
            } else {
                // Scale down
                blit_scaled(page, surface, dst_x, dst_y, dst_w, dst_h);
            }
        } else {
            // Zoomed view - scale based on zoom level
            // Cache is at 1.5x screen res, so we need to account for that
            float cache_scale = 1.5f;
            float effective_zoom = ui->zoom / cache_scale;  // How much to scale the cached image

            // Source view size (how much of the cached image we show)
            int src_view_w = (int)(view_w / effective_zoom);
            int src_view_h = (int)(view_h / effective_zoom);

            // Clamp to page bounds
            if (src_view_w > page->w) src_view_w = page->w;
            if (src_view_h > page->h) src_view_h = page->h;

            // Pan offset determines which part of source to show
            int src_x = (page->w - src_view_w) / 2 - (int)(ui->pan_x / effective_zoom);
            int src_y = (page->h - src_view_h) / 2 - (int)(ui->pan_y / effective_zoom);

            // Clamp source position
            if (src_x < 0) src_x = 0;
            if (src_y < 0) src_y = 0;
            if (src_x + src_view_w > page->w) src_x = page->w - src_view_w;
            if (src_y + src_view_h > page->h) src_y = page->h - src_view_h;
            if (src_x < 0) src_x = 0;
            if (src_y < 0) src_y = 0;

            // Destination size (fill the view)
            int dst_w = view_w;
            int dst_h = view_h;

            // If source is smaller than what we need, adjust destination
            if (src_view_w < (int)(view_w / effective_zoom)) {
                dst_w = (int)(src_view_w * effective_zoom);
            }
            if (src_view_h < (int)(view_h / effective_zoom)) {
                dst_h = (int)(src_view_h * effective_zoom);
            }

            int dst_x = (view_w - dst_w) / 2;
            int dst_y = (view_h - dst_h) / 2;

            // Scale and blit the visible portion
            SDL_LockSurface(page);
            SDL_LockSurface(surface);

            int bpp = page->format->BytesPerPixel;
            int dst_bpp = surface->format->BytesPerPixel;

            for (int dy = 0; dy < dst_h; dy++) {
                int sy = src_y + (int)(dy * src_view_h / dst_h);
                if (sy >= page->h) sy = page->h - 1;
                Uint8 *src_row = (Uint8 *)page->pixels + sy * page->pitch;
                Uint8 *dst_row = (Uint8 *)surface->pixels + (dst_y + dy) * surface->pitch;

                for (int dx = 0; dx < dst_w; dx++) {
                    int sx = src_x + (int)(dx * src_view_w / dst_w);
                    if (sx >= page->w) sx = page->w - 1;
                    Uint8 *src_pixel = src_row + sx * bpp;
                    Uint8 *dst_pixel = dst_row + (dst_x + dx) * dst_bpp;

                    if (bpp == 4 && dst_bpp == 4) {
                        *(Uint32 *)dst_pixel = *(Uint32 *)src_pixel;
                    } else if (bpp >= 3) {
                        dst_pixel[0] = src_pixel[0];
                        dst_pixel[1] = src_pixel[1];
                        dst_pixel[2] = src_pixel[2];
                    }
                }
            }

            SDL_UnlockSurface(surface);
            SDL_UnlockSurface(page);
        }

        // Preload adjacent pages
        cache_preload_adjacent(&ui->cache, ui->current_page);
    } else {
        draw_text(surface, ui->font, "Loading page...", vw/2 - 60, vh/2, COLOR_WHITE);
    }

    // Page indicator bar at bottom
    draw_rect(surface, 0, vh - 40, vw, 40, COLOR_DARK_GRAY);

    char page_info[64];
    if (ui->zoom > 1.01f) {
        snprintf(page_info, sizeof(page_info), "Page %d / %d  [%.1fx]",
                 ui->current_page + 1, ui->comic.page_count, ui->zoom);
    } else {
        snprintf(page_info, sizeof(page_info), "Page %d / %d",
                 ui->current_page + 1, ui->comic.page_count);
    }
    draw_text(surface, ui->font, page_info, 20, vh - 32, COLOR_WHITE);

    // Navigation hint - show zoom levels when not zoomed
    if (ui->zoom <= 1.01f) {
        draw_text(surface, ui->font_small, "Swipe edge: page | Tap: zoom", vw/2 - 100, vh - 30, COLOR_GRAY);
    } else {
        draw_text(surface, ui->font_small, "Tap: next zoom | Pan to move", vw/2 - 100, vh - 30, COLOR_GRAY);
    }
    draw_text(surface, ui->font_small, "[Back]", vw - 80, vh - 30, COLOR_YELLOW);
}

// Check if a filename is a comic file
static int is_cloud_comic_file(const char *name) {
    const char *ext = strrchr(name, '.');
    if (!ext) return 0;
    return (strcasecmp(ext, ".cbz") == 0 || strcasecmp(ext, ".zip") == 0 ||
            strcasecmp(ext, ".cbr") == 0 || strcasecmp(ext, ".rar") == 0);
}

static void render_cloud_browser(UIState *ui, SDL_Surface *surface, int vw, int vh) {
    // Header
    draw_rect(surface, 0, 0, vw, 50, COLOR_BLUE);
    draw_text(surface, ui->font, "Nextcloud Comics", 20, 12, COLOR_WHITE);

    // Current cloud path
    char path_display[64];
    snprintf(path_display, sizeof(path_display), "%.50s", ui->cloud_path);
    draw_text(surface, ui->font_small, path_display, 220, 16, COLOR_WHITE);

    // Local button
    draw_rect(surface, vw - 90, 8, 80, 34, COLOR_DARK_GRAY);
    draw_text(surface, ui->font_small, "Local", vw - 75, 14, COLOR_YELLOW);

    // File list
    int y = 60 - ui->cloud_scroll_offset;
    int item_height = 50;

    // Add parent directory if not at root
    if (strcmp(ui->cloud_path, "/") != 0 && y + item_height >= 50) {
        if (0 == ui->cloud_selected_file) {
            draw_rect(surface, 0, y, vw, item_height - 2, COLOR_DARK_GRAY);
        }
        draw_text(surface, ui->font, "[..]", 15, y + 12, COLOR_GRAY);
        draw_text(surface, ui->font, "..", 70, y + 12, COLOR_YELLOW);
        y += item_height;
    }

    int list_offset = (strcmp(ui->cloud_path, "/") != 0) ? 1 : 0;

    for (int i = 0; i < ui->cloud_files.count; i++) {
        if (y + item_height < 50) {
            y += item_height;
            continue;
        }
        if (y > vh) break;

        CloudFileEntry *entry = &ui->cloud_files.entries[i];

        // Skip non-comic files (but show directories)
        if (entry->type == CLOUD_ENTRY_FILE && !is_cloud_comic_file(entry->name)) {
            continue;
        }

        // Selection highlight
        if (i + list_offset == ui->cloud_selected_file) {
            draw_rect(surface, 0, y, vw, item_height - 2, COLOR_DARK_GRAY);
        }

        // Icon/indicator
        const char *icon = "";
        SDL_Color name_color = COLOR_WHITE;

        if (entry->type == CLOUD_ENTRY_DIRECTORY) {
            icon = "[D]";
            name_color = COLOR_YELLOW;
        } else {
            icon = "[C]";
            name_color = COLOR_WHITE;
        }

        draw_text(surface, ui->font, icon, 15, y + 12, COLOR_GRAY);
        draw_text(surface, ui->font, entry->name, 70, y + 12, name_color);

        y += item_height;
    }

    // Scroll indicator
    int total_items = ui->cloud_files.count + list_offset;
    if (total_items > 12) {
        int total_height = total_items * item_height;
        int visible_height = vh - 50;
        int bar_height = (visible_height * visible_height) / total_height;
        if (bar_height < 30) bar_height = 30;
        int bar_y = 50 + (ui->cloud_scroll_offset * (visible_height - bar_height)) / (total_height - visible_height);
        draw_rect(surface, vw - 8, bar_y, 6, bar_height, COLOR_GRAY);
    }

    // Instructions
    draw_rect(surface, 0, vh - 30, vw, 30, COLOR_DARK_GRAY);
    draw_text(surface, ui->font_small, "Tap to select | Swipe to scroll", 20, vh - 24, COLOR_GRAY);
}

static void render_cloud_config(UIState *ui, SDL_Surface *surface, int vw, int vh) {
    // Title
    draw_text(surface, ui->font, "Nextcloud Setup", vw/2 - 80, 60, COLOR_WHITE);

    int field_width = 400;
    int field_x = (vw - field_width) / 2;
    int start_y = 140;

    // Server URL field
    draw_text(surface, ui->font_small, "Server URL:", field_x, start_y, COLOR_GRAY);
    draw_rect(surface, field_x, start_y + 25, field_width, 40,
              ui->config_input_field == 0 ? COLOR_BLUE : COLOR_DARK_GRAY);
    draw_text(surface, ui->font, ui->input_server[0] ? ui->input_server : "https://...", field_x + 10, start_y + 32, COLOR_WHITE);

    // Username field
    start_y += 90;
    draw_text(surface, ui->font_small, "Username:", field_x, start_y, COLOR_GRAY);
    draw_rect(surface, field_x, start_y + 25, field_width, 40,
              ui->config_input_field == 1 ? COLOR_BLUE : COLOR_DARK_GRAY);
    draw_text(surface, ui->font, ui->input_username, field_x + 10, start_y + 32, COLOR_WHITE);

    // Password field
    start_y += 90;
    draw_text(surface, ui->font_small, "Password:", field_x, start_y, COLOR_GRAY);
    draw_rect(surface, field_x, start_y + 25, field_width, 40,
              ui->config_input_field == 2 ? COLOR_BLUE : COLOR_DARK_GRAY);
    // Mask password
    char masked[256];
    int pass_len = strlen(ui->input_password);
    if (pass_len > 255) pass_len = 255;
    memset(masked, '*', pass_len);
    masked[pass_len] = '\0';
    draw_text(surface, ui->font, masked, field_x + 10, start_y + 32, COLOR_WHITE);

    // Buttons
    start_y += 100;

    // Connect button
    draw_rect(surface, field_x, start_y, (field_width - 20) / 2, 50, COLOR_BLUE);
    draw_text(surface, ui->font, "Connect", field_x + 50, start_y + 12, COLOR_WHITE);

    // Cancel button
    draw_rect(surface, field_x + (field_width + 20) / 2, start_y, (field_width - 20) / 2, 50, COLOR_DARK_GRAY);
    draw_text(surface, ui->font, "Cancel", field_x + (field_width + 20) / 2 + 50, start_y + 12, COLOR_WHITE);

    // Instructions
    draw_text(surface, ui->font_small, "Tap field to edit, use keyboard to type", vw/2 - 140, vh - 80, COLOR_GRAY);
    draw_text(surface, ui->font_small, "Example: https://cloud.example.com", vw/2 - 130, vh - 50, COLOR_GRAY);
}

static void render_loading(UIState *ui, SDL_Surface *surface, int vw, int vh) {
    draw_text(surface, ui->font, ui->message, vw/2 - 60, vh/2, COLOR_WHITE);
}

static void render_error(UIState *ui, SDL_Surface *surface, int vw, int vh) {
    draw_text(surface, ui->font, "Error", vw/2 - 30, vh/2 - 40, COLOR_YELLOW);
    draw_text(surface, ui->font, ui->message, vw/2 - 100, vh/2 + 20, COLOR_WHITE);
    draw_text(surface, ui->font_small, "Tap to go back", vw/2 - 50, vh/2 + 80, COLOR_GRAY);
}

void ui_render(UIState *ui) {
    // Get virtual dimensions and render surface
    int vw, vh;
    get_virtual_size(ui, &vw, &vh);
    SDL_Surface *surface = get_render_surface(ui);

    // Clear render surface
    SDL_FillRect(surface, NULL, SDL_MapRGB(surface->format, 20, 20, 25));

    switch (ui->state) {
        case SCREEN_BROWSER:
            render_browser(ui, surface, vw, vh);
            break;
        case SCREEN_READER:
            render_reader(ui, surface, vw, vh);
            break;
        case SCREEN_LOADING:
            render_loading(ui, surface, vw, vh);
            break;
        case SCREEN_ERROR:
            render_error(ui, surface, vw, vh);
            break;
        case SCREEN_CLOUD_BROWSER:
            render_cloud_browser(ui, surface, vw, vh);
            break;
        case SCREEN_CLOUD_CONFIG:
            render_cloud_config(ui, surface, vw, vh);
            break;
    }

    // For portrait modes, blit the rotated portrait surface to the screen
    if (ui->orientation != 0 && portrait_surface) {
        blit_portrait_to_screen(portrait_surface, ui->screen, ui->orientation);
    }

    SDL_Flip(ui->screen);
}

static int point_in_rect(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

int ui_handle_event(UIState *ui, SDL_Event *event) {
    if (event->type == SDL_QUIT) {
        return 1; // Quit
    }

    // Touch tracking - transform coordinates for rotation
    if (event->type == SDL_MOUSEBUTTONDOWN) {
        int tx = event->button.x;
        int ty = event->button.y;
        transform_touch(ui, &tx, &ty);
        ui->touch_start_x = tx;
        ui->touch_start_y = ty;
        ui->touch_moved = 0;
    }

    if (event->type == SDL_MOUSEMOTION && (event->motion.state & SDL_BUTTON(1))) {
        int tx = event->motion.x;
        int ty = event->motion.y;
        transform_touch(ui, &tx, &ty);

        int dx = tx - ui->touch_start_x;
        int dy = ty - ui->touch_start_y;

        if (abs(dx) > 15 || abs(dy) > 15) {
            ui->touch_moved = 1;
        }

        // Scrolling in browser (local or cloud)
        if ((ui->state == SCREEN_BROWSER || ui->state == SCREEN_CLOUD_BROWSER) && abs(dy) > 10) {
            int *scroll_ptr = (ui->state == SCREEN_BROWSER) ? &ui->scroll_offset : &ui->cloud_scroll_offset;
            int item_count = (ui->state == SCREEN_BROWSER) ? ui->file_count : ui->cloud_files.count;

            *scroll_ptr -= dy;
            ui->touch_start_x = tx;
            ui->touch_start_y = ty;

            // Clamp scroll - use virtual height
            int vw, vh;
            get_virtual_size(ui, &vw, &vh);
            int max_scroll = item_count * 50 - (vh - 80);
            if (max_scroll < 0) max_scroll = 0;
            if (*scroll_ptr < 0) *scroll_ptr = 0;
            if (*scroll_ptr > max_scroll) *scroll_ptr = max_scroll;
        }

        // Panning when zoomed in reader
        if (ui->state == SCREEN_READER && ui->zoom > 1.0f) {
            if (abs(dx) > 2 || abs(dy) > 2) {
                ui->pan_x += dx;
                ui->pan_y += dy;
                ui->touch_start_x = tx;
                ui->touch_start_y = ty;
                ui->touch_moved = 1;  // Always mark as moved when panning
            }
        }
    }

    if (event->type == SDL_MOUSEBUTTONUP) {
        int x = event->button.x;
        int y = event->button.y;
        transform_touch(ui, &x, &y);

        // Get virtual dimensions for hit testing
        int vw, vh;
        get_virtual_size(ui, &vw, &vh);

        if (ui->state == SCREEN_BROWSER && !ui->touch_moved) {
            // Check Cloud button in header
            if (y < 50 && x > vw - 90) {
                // Cloud button tapped
                if (ui->cloud_configured) {
                    ui->state = SCREEN_CLOUD_BROWSER;
                } else {
                    // Show config screen
                    strcpy(ui->input_server, ui->cloud_config.server_url);
                    strcpy(ui->input_username, ui->cloud_config.username);
                    strcpy(ui->input_password, ui->cloud_config.password);
                    ui->config_input_field = 0;
                    ui->state = SCREEN_CLOUD_CONFIG;
                }
                return 0;
            }

            // File selection
            if (y > 50 && y < vh - 30) {
                int clicked_index = (y - 60 + ui->scroll_offset) / 50;
                if (clicked_index >= 0 && clicked_index < ui->file_count) {
                    FileEntry *entry = &ui->files[clicked_index];
                    ui->selected_file = clicked_index;

                    if (entry->type == ENTRY_PARENT || entry->type == ENTRY_DIRECTORY) {
                        ui_scan_directory(ui, entry->full_path);
                    } else {
                        return 2; // Open comic (handled by main)
                    }
                }
            }
        }
        else if (ui->state == SCREEN_READER) {
            int start_x = ui->touch_start_x;
            int start_y = ui->touch_start_y;
            int dx = x - start_x;

            // Edge zone for page turn swipes (50 pixels from edge)
            int edge_zone = 50;
            int started_at_left_edge = (start_x < edge_zone);
            int started_at_right_edge = (start_x > vw - edge_zone);

            if (!ui->touch_moved) {
                // Tap (no movement)
                if (y > vh - 50) {
                    // Bottom bar - back button
                    if (x > vw - 100) {
                        return 3; // Back to browser
                    }
                } else {
                    // Tap middle area = cycle zoom levels: 1x → 1.5x → 2x → 3x → 1x
                    if (ui->zoom < 1.1f) {
                        ui->zoom = 1.5f;
                    } else if (ui->zoom < 1.6f) {
                        ui->zoom = 2.0f;
                    } else if (ui->zoom < 2.5f) {
                        ui->zoom = 3.0f;
                    } else {
                        ui->zoom = 1.0f;
                        ui->pan_x = 0;
                        ui->pan_y = 0;
                    }
                }
            } else {
                // Swipe/drag
                if (started_at_right_edge && dx < -80) {
                    // Swipe left from right edge = next page
                    ui_next_page(ui);
                } else if (started_at_left_edge && dx > 80) {
                    // Swipe right from left edge = prev page
                    ui_prev_page(ui);
                }
                // Otherwise it was just panning (handled in MOUSEMOTION)
            }
        }
        else if (ui->state == SCREEN_ERROR) {
            return 3; // Back to browser
        }
        else if (ui->state == SCREEN_CLOUD_BROWSER && !ui->touch_moved) {
            // Check Local button in header
            if (y < 50 && x > vw - 90) {
                ui->state = SCREEN_BROWSER;
                return 0;
            }

            // File selection
            if (y > 50 && y < vh - 30) {
                int list_offset = (strcmp(ui->cloud_path, "/") != 0) ? 1 : 0;
                int clicked_index = (y - 60 + ui->cloud_scroll_offset) / 50;

                // Check if parent directory was clicked
                if (list_offset > 0 && clicked_index == 0) {
                    // Go to parent directory
                    char *last_slash = strrchr(ui->cloud_path, '/');
                    if (last_slash && last_slash != ui->cloud_path) {
                        *last_slash = '\0';
                    } else {
                        strcpy(ui->cloud_path, "/");
                    }
                    return 5; // Signal to refresh cloud directory
                }

                // Adjust for parent directory offset
                int file_index = clicked_index - list_offset;
                if (file_index >= 0 && file_index < ui->cloud_files.count) {
                    CloudFileEntry *entry = &ui->cloud_files.entries[file_index];
                    ui->cloud_selected_file = clicked_index;

                    if (entry->type == CLOUD_ENTRY_DIRECTORY) {
                        // Navigate into directory
                        if (strcmp(ui->cloud_path, "/") == 0) {
                            snprintf(ui->cloud_path, sizeof(ui->cloud_path), "/%s", entry->name);
                        } else {
                            char temp[MAX_PATH_LEN];
                            snprintf(temp, sizeof(temp), "%s/%s", ui->cloud_path, entry->name);
                            strncpy(ui->cloud_path, temp, sizeof(ui->cloud_path) - 1);
                        }
                        return 5; // Signal to refresh cloud directory
                    } else if (is_cloud_comic_file(entry->name)) {
                        return 6; // Open cloud comic (handled by main)
                    }
                }
            }
        }
        else if (ui->state == SCREEN_CLOUD_CONFIG && !ui->touch_moved) {
            int field_width = 400;
            int field_x = (vw - field_width) / 2;
            int start_y = 140;

            // Server URL field
            if (x >= field_x && x <= field_x + field_width &&
                y >= start_y + 25 && y <= start_y + 65) {
                ui->config_input_field = 0;
                PDL_SetKeyboardState(PDL_TRUE);
                return 0;
            }

            // Username field
            start_y += 90;
            if (x >= field_x && x <= field_x + field_width &&
                y >= start_y + 25 && y <= start_y + 65) {
                ui->config_input_field = 1;
                PDL_SetKeyboardState(PDL_TRUE);
                return 0;
            }

            // Password field
            start_y += 90;
            if (x >= field_x && x <= field_x + field_width &&
                y >= start_y + 25 && y <= start_y + 65) {
                ui->config_input_field = 2;
                PDL_SetKeyboardState(PDL_TRUE);
                return 0;
            }

            // Connect button
            start_y += 100;
            if (x >= field_x && x <= field_x + (field_width - 20) / 2 &&
                y >= start_y && y <= start_y + 50) {
                PDL_SetKeyboardState(PDL_FALSE);
                // Copy input to config and signal connection attempt
                strcpy(ui->cloud_config.server_url, ui->input_server);
                strcpy(ui->cloud_config.username, ui->input_username);
                strcpy(ui->cloud_config.password, ui->input_password);
                return 4; // Signal to test connection
            }

            // Cancel button
            if (x >= field_x + (field_width + 20) / 2 && x <= field_x + field_width &&
                y >= start_y && y <= start_y + 50) {
                PDL_SetKeyboardState(PDL_FALSE);
                ui->state = SCREEN_BROWSER;
                return 0;
            }

            // Tap outside fields dismisses keyboard
            PDL_SetKeyboardState(PDL_FALSE);
        }
    }

    // Keyboard (for testing/hardware keyboard)
    if (event->type == SDL_KEYDOWN) {
        if (ui->state == SCREEN_READER) {
            if (event->key.keysym.sym == SDLK_LEFT) {
                ui_prev_page(ui);
            } else if (event->key.keysym.sym == SDLK_RIGHT) {
                ui_next_page(ui);
            } else if (event->key.keysym.sym == SDLK_ESCAPE) {
                return 3; // Back
            }
        } else if (ui->state == SCREEN_BROWSER) {
            if (event->key.keysym.sym == SDLK_ESCAPE) {
                return 1; // Quit
            }
        } else if (ui->state == SCREEN_CLOUD_BROWSER) {
            if (event->key.keysym.sym == SDLK_ESCAPE) {
                ui->state = SCREEN_BROWSER;
            }
        } else if (ui->state == SCREEN_CLOUD_CONFIG) {
            char *target = NULL;
            int max_len = 0;

            switch (ui->config_input_field) {
                case 0: target = ui->input_server; max_len = sizeof(ui->input_server) - 1; break;
                case 1: target = ui->input_username; max_len = sizeof(ui->input_username) - 1; break;
                case 2: target = ui->input_password; max_len = sizeof(ui->input_password) - 1; break;
            }

            if (target) {
                int len = strlen(target);
                SDLKey key = event->key.keysym.sym;

                if (key == SDLK_BACKSPACE && len > 0) {
                    target[len - 1] = '\0';
                }
                else if (key == SDLK_RETURN) {
                    if (ui->config_input_field == 2) {
                        // On password field, dismiss keyboard
                        PDL_SetKeyboardState(PDL_FALSE);
                    } else {
                        // Move to next field
                        ui->config_input_field = (ui->config_input_field + 1) % 3;
                    }
                }
                else if (key == SDLK_TAB) {
                    ui->config_input_field = (ui->config_input_field + 1) % 3;
                }
                else if (key == SDLK_ESCAPE) {
                    PDL_SetKeyboardState(PDL_FALSE);
                    ui->state = SCREEN_BROWSER;
                }
                else {
                    // Use unicode value for proper virtual keyboard support
                    Uint16 unicode = event->key.keysym.unicode;
                    if (unicode >= 32 && unicode < 127 && len < max_len) {
                        target[len] = (char)unicode;
                        target[len + 1] = '\0';
                    }
                }
            }
        }
    }

    return 0;
}

// Cloud browser functions

int ui_scan_cloud_directory(UIState *ui, const char *path) {
    filelist_init(&ui->cloud_files);
    ui->cloud_scroll_offset = 0;
    ui->cloud_selected_file = 0;

    if (webdav_list_directory(&ui->cloud_config, path, &ui->cloud_files) != 0) {
        fprintf(stderr, "Failed to list cloud directory: %s\n", webdav_get_error());
        return -1;
    }

    return 0;
}

int ui_download_comic(UIState *ui, const char *remote_path, char *local_path, size_t local_path_len) {
    // Extract filename from remote path
    const char *filename = strrchr(remote_path, '/');
    if (filename) {
        filename++; // Skip the slash
    } else {
        filename = remote_path;
    }

    // Build local cache path
    snprintf(local_path, local_path_len, "%s/%s", CLOUD_CACHE_DIR, filename);

    // Check if already cached
    struct stat st;
    if (stat(local_path, &st) == 0) {
        printf("Using cached comic: %s\n", local_path);
        return 0; // Already exists
    }

    // Download the file
    printf("Downloading: %s -> %s\n", remote_path, local_path);
    if (webdav_download_file(&ui->cloud_config, remote_path, local_path, NULL, NULL) != 0) {
        fprintf(stderr, "Download failed: %s\n", webdav_get_error());
        return -1;
    }

    return 0;
}

void ui_load_cloud_config(UIState *ui) {
    if (config_load(&ui->cloud_config, CONFIG_FILE_PATH) == 0) {
        // Config loaded successfully
        if (ui->cloud_config.server_url[0] != '\0' &&
            ui->cloud_config.username[0] != '\0') {
            ui->cloud_configured = 1;
            printf("Loaded cloud config: %s@%s\n",
                   ui->cloud_config.username, ui->cloud_config.server_url);
        }
    }
}

void ui_save_cloud_config(UIState *ui) {
    ui->cloud_config.remember_password = 1;
    if (config_save(&ui->cloud_config, CONFIG_FILE_PATH) == 0) {
        printf("Cloud config saved\n");
    } else {
        fprintf(stderr, "Failed to save cloud config\n");
    }
}
