#include "ui.h"
#include <PDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <math.h>

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

static TTF_Font *load_font(int size) {
    for (int i = 0; FONT_PATHS[i]; i++) {
        TTF_Font *font = TTF_OpenFont(FONT_PATHS[i], size);
        if (font) return font;
    }
    return NULL;
}

int ui_init(UIState *ui) {
    memset(ui, 0, sizeof(UIState));

    ui->screen = SDL_SetVideoMode(SCREEN_WIDTH, SCREEN_HEIGHT, 32, SDL_SWSURFACE);
    if (!ui->screen) {
        fprintf(stderr, "SDL_SetVideoMode failed: %s\n", SDL_GetError());
        return -1;
    }

    if (TTF_Init() < 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        return -1;
    }

    // Initialize joystick for multi-touch
    SDL_JoystickEventState(SDL_ENABLE);
    if (SDL_NumJoysticks() > 0) {
        ui->touchpad = SDL_JoystickOpen(0);
        if (ui->touchpad) {
            printf("Touchpad opened: %d axes\n", SDL_JoystickNumAxes(ui->touchpad));
        }
    }

    ui->font = load_font(22);
    ui->font_small = load_font(16);

    if (!ui->font || !ui->font_small) {
        fprintf(stderr, "Failed to load fonts\n");
        return -1;
    }

    ui->state = SCREEN_BROWSER;
    ui->zoom = 1.0f;
    ui->rotation = 0;
    strcpy(ui->current_dir, "/media/internal");

    return 0;
}

void ui_cleanup(UIState *ui) {
    ui_close_comic(ui);
    if (ui->touchpad) SDL_JoystickClose(ui->touchpad);
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
    ui->rotation = 0;
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

static void render_browser(UIState *ui) {
    // Header
    draw_rect(ui->screen, 0, 0, SCREEN_WIDTH, 50, COLOR_BLUE);
    draw_text(ui->screen, ui->font, "Comic Reader", 20, 12, COLOR_WHITE);

    // Current path
    char path_display[64];
    snprintf(path_display, sizeof(path_display), "%.60s", ui->current_dir);
    draw_text(ui->screen, ui->font_small, path_display, 200, 16, COLOR_WHITE);

    // File list
    int y = 60 - ui->scroll_offset;
    int item_height = 50;

    for (int i = 0; i < ui->file_count; i++) {
        if (y + item_height < 50) {
            y += item_height;
            continue;
        }
        if (y > SCREEN_HEIGHT) break;

        FileEntry *entry = &ui->files[i];

        // Selection highlight
        if (i == ui->selected_file) {
            draw_rect(ui->screen, 0, y, SCREEN_WIDTH, item_height - 2, COLOR_DARK_GRAY);
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

        draw_text(ui->screen, ui->font, icon, 15, y + 12, COLOR_GRAY);
        draw_text(ui->screen, ui->font, entry->name, 70, y + 12, name_color);

        y += item_height;
    }

    // Scroll indicator
    if (ui->file_count > 12) {
        int total_height = ui->file_count * item_height;
        int visible_height = SCREEN_HEIGHT - 50;
        int bar_height = (visible_height * visible_height) / total_height;
        if (bar_height < 30) bar_height = 30;
        int bar_y = 50 + (ui->scroll_offset * (visible_height - bar_height)) / (total_height - visible_height);
        draw_rect(ui->screen, SCREEN_WIDTH - 8, bar_y, 6, bar_height, COLOR_GRAY);
    }

    // Instructions
    draw_rect(ui->screen, 0, SCREEN_HEIGHT - 30, SCREEN_WIDTH, 30, COLOR_DARK_GRAY);
    draw_text(ui->screen, ui->font_small, "Tap to select | Swipe to scroll", 20, SCREEN_HEIGHT - 24, COLOR_GRAY);
}

// Rotate a surface by 90 degrees clockwise
static SDL_Surface *rotate_surface_90(SDL_Surface *src) {
    SDL_Surface *dst = SDL_CreateRGBSurface(SDL_SWSURFACE, src->h, src->w,
        src->format->BitsPerPixel,
        src->format->Rmask, src->format->Gmask, src->format->Bmask, src->format->Amask);
    if (!dst) return NULL;

    SDL_LockSurface(src);
    SDL_LockSurface(dst);

    int bpp = src->format->BytesPerPixel;
    for (int y = 0; y < src->h; y++) {
        for (int x = 0; x < src->w; x++) {
            Uint8 *src_pixel = (Uint8 *)src->pixels + y * src->pitch + x * bpp;
            Uint8 *dst_pixel = (Uint8 *)dst->pixels + x * dst->pitch + (src->h - 1 - y) * bpp;
            memcpy(dst_pixel, src_pixel, bpp);
        }
    }

    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(src);
    return dst;
}

static void render_reader(UIState *ui) {
    // Get current page
    SDL_Surface *page = cache_get_page(&ui->cache, ui->current_page);

    if (page) {
        SDL_Surface *display_page = page;
        SDL_Surface *rotated = NULL;

        // Apply rotation if needed
        if (ui->rotation == 90) {
            rotated = rotate_surface_90(page);
            display_page = rotated;
        } else if (ui->rotation == 180) {
            SDL_Surface *tmp = rotate_surface_90(page);
            rotated = rotate_surface_90(tmp);
            SDL_FreeSurface(tmp);
            display_page = rotated;
        } else if (ui->rotation == 270) {
            SDL_Surface *tmp1 = rotate_surface_90(page);
            SDL_Surface *tmp2 = rotate_surface_90(tmp1);
            SDL_FreeSurface(tmp1);
            rotated = rotate_surface_90(tmp2);
            SDL_FreeSurface(tmp2);
            display_page = rotated;
        }

        // Calculate zoomed dimensions
        int zoomed_w = (int)(display_page->w * ui->zoom);
        int zoomed_h = (int)(display_page->h * ui->zoom);

        // Center position with pan offset
        int x = (SCREEN_WIDTH - zoomed_w) / 2 + (int)ui->pan_x;
        int y = (SCREEN_HEIGHT - zoomed_h) / 2 + (int)ui->pan_y;

        if (ui->zoom == 1.0f) {
            // No zoom - direct blit
            SDL_Rect dest = {x, y, 0, 0};
            SDL_BlitSurface(display_page, NULL, ui->screen, &dest);
        } else {
            // Zoomed - use software stretch
            SDL_Rect src_rect = {0, 0, display_page->w, display_page->h};
            SDL_Rect dst_rect = {x, y, zoomed_w, zoomed_h};

            // Clip to screen bounds
            if (dst_rect.x < 0) {
                src_rect.x = (int)((-dst_rect.x) / ui->zoom);
                src_rect.w -= src_rect.x;
                dst_rect.w += dst_rect.x;
                dst_rect.x = 0;
            }
            if (dst_rect.y < 0) {
                src_rect.y = (int)((-dst_rect.y) / ui->zoom);
                src_rect.h -= src_rect.y;
                dst_rect.h += dst_rect.y;
                dst_rect.y = 0;
            }
            if (dst_rect.x + dst_rect.w > SCREEN_WIDTH) {
                int overflow = dst_rect.x + dst_rect.w - SCREEN_WIDTH;
                dst_rect.w -= overflow;
                src_rect.w = (int)(dst_rect.w / ui->zoom);
            }
            if (dst_rect.y + dst_rect.h > SCREEN_HEIGHT - 40) {
                int overflow = dst_rect.y + dst_rect.h - (SCREEN_HEIGHT - 40);
                dst_rect.h -= overflow;
                src_rect.h = (int)(dst_rect.h / ui->zoom);
            }

            if (dst_rect.w > 0 && dst_rect.h > 0 && src_rect.w > 0 && src_rect.h > 0) {
                SDL_SoftStretch(display_page, &src_rect, ui->screen, &dst_rect);
            }
        }

        if (rotated) {
            SDL_FreeSurface(rotated);
        }

        // Preload adjacent pages
        cache_preload_adjacent(&ui->cache, ui->current_page);
    } else {
        draw_text(ui->screen, ui->font, "Loading page...", 400, 380, COLOR_WHITE);
    }

    // Page indicator (semi-transparent bar at bottom)
    draw_rect(ui->screen, 0, SCREEN_HEIGHT - 40, SCREEN_WIDTH, 40, COLOR_DARK_GRAY);

    char page_info[64];
    snprintf(page_info, sizeof(page_info), "Page %d / %d  (%.0f%%)",
             ui->current_page + 1, ui->comic.page_count, ui->zoom * 100);
    draw_text(ui->screen, ui->font, page_info, 20, SCREEN_HEIGHT - 32, COLOR_WHITE);

    // Navigation hints and buttons
    draw_text(ui->screen, ui->font_small, "< Prev", 400, SCREEN_HEIGHT - 30, COLOR_GRAY);
    draw_text(ui->screen, ui->font_small, "Next >", 520, SCREEN_HEIGHT - 30, COLOR_GRAY);
    draw_text(ui->screen, ui->font_small, "[Rotate]", SCREEN_WIDTH - 180, SCREEN_HEIGHT - 30, COLOR_BLUE);
    draw_text(ui->screen, ui->font_small, "[Back]", SCREEN_WIDTH - 80, SCREEN_HEIGHT - 30, COLOR_YELLOW);
}

static void render_loading(UIState *ui) {
    draw_text(ui->screen, ui->font, ui->message, 400, 380, COLOR_WHITE);
}

static void render_error(UIState *ui) {
    draw_text(ui->screen, ui->font, "Error", 480, 340, COLOR_YELLOW);
    draw_text(ui->screen, ui->font, ui->message, 300, 400, COLOR_WHITE);
    draw_text(ui->screen, ui->font_small, "Tap to go back", 440, 480, COLOR_GRAY);
}

void ui_render(UIState *ui) {
    // Clear screen
    SDL_FillRect(ui->screen, NULL, SDL_MapRGB(ui->screen->format, 20, 20, 25));

    switch (ui->state) {
        case SCREEN_BROWSER:
            render_browser(ui);
            break;
        case SCREEN_READER:
            render_reader(ui);
            break;
        case SCREEN_LOADING:
            render_loading(ui);
            break;
        case SCREEN_ERROR:
            render_error(ui);
            break;
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

    // Touch tracking
    if (event->type == SDL_MOUSEBUTTONDOWN) {
        ui->touch_start_x = event->button.x;
        ui->touch_start_y = event->button.y;
        ui->touch_moved = 0;
    }

    if (event->type == SDL_MOUSEMOTION && (event->motion.state & SDL_BUTTON(1))) {
        int dx = event->motion.x - ui->touch_start_x;
        int dy = event->motion.y - ui->touch_start_y;

        if (abs(dx) > 15 || abs(dy) > 15) {
            ui->touch_moved = 1;
        }

        // Scrolling in browser
        if (ui->state == SCREEN_BROWSER && abs(dy) > 10) {
            ui->scroll_offset -= (event->motion.y - ui->touch_start_y);
            ui->touch_start_y = event->motion.y;

            // Clamp scroll
            int max_scroll = ui->file_count * 50 - (SCREEN_HEIGHT - 80);
            if (max_scroll < 0) max_scroll = 0;
            if (ui->scroll_offset < 0) ui->scroll_offset = 0;
            if (ui->scroll_offset > max_scroll) ui->scroll_offset = max_scroll;
        }
    }

    if (event->type == SDL_MOUSEBUTTONUP) {
        int x = event->button.x;
        int y = event->button.y;

        if (ui->state == SCREEN_BROWSER && !ui->touch_moved) {
            // File selection
            if (y > 50 && y < SCREEN_HEIGHT - 30) {
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
            if (!ui->touch_moved) {
                // Tap navigation
                if (y > SCREEN_HEIGHT - 50) {
                    // Bottom bar buttons
                    if (x > SCREEN_WIDTH - 100) {
                        return 3; // Back to browser
                    } else if (x > SCREEN_WIDTH - 200 && x < SCREEN_WIDTH - 110) {
                        // Rotate button
                        ui->rotation = (ui->rotation + 90) % 360;
                    }
                } else if (x < SCREEN_WIDTH / 3) {
                    // Left third - previous page
                    ui_prev_page(ui);
                } else if (x > SCREEN_WIDTH * 2 / 3) {
                    // Right third - next page
                    ui_next_page(ui);
                }
            } else {
                // Swipe navigation
                int dx = x - ui->touch_start_x;
                if (dx > 100) {
                    ui_prev_page(ui);
                } else if (dx < -100) {
                    ui_next_page(ui);
                }
            }
        }
        else if (ui->state == SCREEN_ERROR) {
            return 3; // Back to browser
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
        }
    }

    // Multi-touch handling via joystick (webOS TouchPad)
    if (ui->state == SCREEN_READER && ui->touchpad) {
        if (event->type == SDL_JOYAXISMOTION) {
            // Axes 0,1 = touch1 x,y; axes 2,3 = touch2 x,y
            // Values are -32768 to 32767, map to screen coords
            int axis = event->jaxis.axis;
            int value = event->jaxis.value;

            // Convert axis value to screen coordinate
            int coord = (value + 32768) * ((axis % 2 == 0) ? SCREEN_WIDTH : SCREEN_HEIGHT) / 65536;

            switch (axis) {
                case 0: ui->touch1_x = coord; break;
                case 1: ui->touch1_y = coord; break;
                case 2: ui->touch2_x = coord; break;
                case 3: ui->touch2_y = coord; break;
            }

            // Check if both touches are active (non-zero positions)
            if (ui->touch1_x > 0 && ui->touch1_y > 0 &&
                ui->touch2_x > 0 && ui->touch2_y > 0) {

                // Calculate distance between touches
                float dx = ui->touch2_x - ui->touch1_x;
                float dy = ui->touch2_y - ui->touch1_y;
                float dist = sqrtf(dx * dx + dy * dy);

                if (!ui->pinch_active) {
                    // Start pinch
                    ui->pinch_active = 1;
                    ui->pinch_start_dist = dist;
                    ui->pinch_start_zoom = ui->zoom;
                } else {
                    // Update zoom based on pinch
                    if (ui->pinch_start_dist > 10) {
                        float scale = dist / ui->pinch_start_dist;
                        ui->zoom = ui->pinch_start_zoom * scale;

                        // Clamp zoom
                        if (ui->zoom < 0.5f) ui->zoom = 0.5f;
                        if (ui->zoom > 4.0f) ui->zoom = 4.0f;
                    }
                }
            }
        }

        // Reset pinch when touch released
        if (event->type == SDL_JOYBUTTONUP) {
            ui->pinch_active = 0;
            ui->touch1_x = ui->touch1_y = 0;
            ui->touch2_x = ui->touch2_y = 0;
        }
    }

    // Pan when zoomed (drag with single touch)
    if (ui->state == SCREEN_READER && ui->zoom > 1.0f && !ui->pinch_active) {
        if (event->type == SDL_MOUSEMOTION && (event->motion.state & SDL_BUTTON(1))) {
            ui->pan_x += event->motion.xrel;
            ui->pan_y += event->motion.yrel;
            ui->touch_moved = 1;
        }
    }

    // Double-tap to reset zoom (detect quick successive taps)
    static Uint32 last_tap_time = 0;
    static int last_tap_x = 0, last_tap_y = 0;

    if (ui->state == SCREEN_READER && event->type == SDL_MOUSEBUTTONDOWN) {
        Uint32 now = SDL_GetTicks();
        int x = event->button.x;
        int y = event->button.y;

        if (now - last_tap_time < 300 &&
            abs(x - last_tap_x) < 50 && abs(y - last_tap_y) < 50) {
            // Double tap detected - toggle zoom
            if (ui->zoom > 1.1f) {
                ui->zoom = 1.0f;
                ui->pan_x = 0;
                ui->pan_y = 0;
            } else {
                ui->zoom = 2.0f;
                // Center zoom on tap location
                ui->pan_x = (SCREEN_WIDTH / 2 - x);
                ui->pan_y = (SCREEN_HEIGHT / 2 - y);
            }
        }
        last_tap_time = now;
        last_tap_x = x;
        last_tap_y = y;
    }

    // Rotation via keyboard (R key)
    if (ui->state == SCREEN_READER && event->type == SDL_KEYDOWN) {
        if (event->key.keysym.sym == SDLK_r) {
            ui->rotation = (ui->rotation + 90) % 360;
        } else if (event->key.keysym.sym == SDLK_0) {
            // Reset zoom and pan
            ui->zoom = 1.0f;
            ui->pan_x = 0;
            ui->pan_y = 0;
            ui->rotation = 0;
        }
    }

    return 0;
}
