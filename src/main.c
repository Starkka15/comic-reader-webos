#include <SDL.h>
#include <SDL_image.h>
#include <PDL.h>
#include <stdio.h>
#include <stdlib.h>

#include "ui.h"
#include "cbz.h"
#include "cache.h"

#define COMICS_DIR "/media/internal/comics"
#define DEFAULT_DIR "/media/internal"

static UIState ui;

int main(int argc, char *argv[]) {
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // SDL_image doesn't need explicit init in older versions

    // Initialize PDL
    PDL_Init(0);

    // Initialize UI
    if (ui_init(&ui) != 0) {
        fprintf(stderr, "ui_init failed\n");
        SDL_Quit();
        return 1;
    }

    // Start in comics directory if it exists, otherwise default
    if (ui_scan_directory(&ui, COMICS_DIR) != 0) {
        ui_scan_directory(&ui, DEFAULT_DIR);
    }

    // Main loop
    int running = 1;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            int result = ui_handle_event(&ui, &event);

            switch (result) {
                case 1: // Quit
                    running = 0;
                    break;

                case 2: // Open comic
                    if (ui.selected_file >= 0 && ui.selected_file < ui.file_count) {
                        FileEntry *entry = &ui.files[ui.selected_file];
                        if (entry->type == ENTRY_FILE) {
                            ui_open_comic(&ui, entry->full_path);
                        }
                    }
                    break;

                case 3: // Back to browser
                    ui_close_comic(&ui);
                    ui_set_screen(&ui, SCREEN_BROWSER);
                    break;
            }
        }

        // Poll orientation sensor (updates ui.orientation)
        ui_poll_orientation(&ui);

        ui_render(&ui);
        SDL_Delay(16); // ~60 FPS
    }

    // Cleanup
    ui_cleanup(&ui);
    PDL_Quit();
    SDL_Quit();

    return 0;
}
