#include <SDL.h>
#include <SDL_image.h>
#include <PDL.h>
#include <stdio.h>
#include <stdlib.h>

#include "ui.h"
#include "cbz.h"
#include "cache.h"
#include "webdav.h"

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

    // Initialize WebDAV/curl for cloud support
    if (webdav_init() != 0) {
        fprintf(stderr, "webdav_init failed: %s\n", webdav_get_error());
        // Continue anyway - cloud features will be unavailable
    }

    // Initialize UI
    if (ui_init(&ui) != 0) {
        fprintf(stderr, "ui_init failed\n");
        webdav_cleanup();
        SDL_Quit();
        return 1;
    }

    // Load cloud configuration if available
    ui_load_cloud_config(&ui);

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

                case 2: // Open local comic
                    if (ui.selected_file >= 0 && ui.selected_file < ui.file_count) {
                        FileEntry *entry = &ui.files[ui.selected_file];
                        if (entry->type == ENTRY_FILE) {
                            ui_open_comic(&ui, entry->full_path);
                        }
                    }
                    break;

                case 3: // Back to browser
                    ui_close_comic(&ui);
                    if (ui.browse_mode == 1) {
                        ui_set_screen(&ui, SCREEN_CLOUD_BROWSER);
                    } else {
                        ui_set_screen(&ui, SCREEN_BROWSER);
                    }
                    break;

                case 4: // Test cloud connection
                    ui_set_screen(&ui, SCREEN_LOADING);
                    ui_set_message(&ui, "Connecting...");
                    ui_render(&ui);
                    if (webdav_test_connection(&ui.cloud_config) == 0) {
                        // Connection successful - save config and go to cloud browser
                        ui_save_cloud_config(&ui);
                        ui.cloud_configured = 1;
                        strcpy(ui.cloud_path, "/");
                        if (ui_scan_cloud_directory(&ui, "/") == 0) {
                            ui.browse_mode = 1;
                            ui_set_screen(&ui, SCREEN_CLOUD_BROWSER);
                        } else {
                            ui_set_message(&ui, "Failed to list directory");
                            ui_set_screen(&ui, SCREEN_ERROR);
                        }
                    } else {
                        ui_set_message(&ui, webdav_get_error());
                        ui_set_screen(&ui, SCREEN_ERROR);
                    }
                    break;

                case 5: // Refresh cloud directory
                    ui_set_screen(&ui, SCREEN_LOADING);
                    ui_set_message(&ui, "Loading...");
                    ui_render(&ui);
                    if (ui_scan_cloud_directory(&ui, ui.cloud_path) == 0) {
                        ui_set_screen(&ui, SCREEN_CLOUD_BROWSER);
                    } else {
                        ui_set_message(&ui, webdav_get_error());
                        ui_set_screen(&ui, SCREEN_ERROR);
                    }
                    break;

                case 6: // Open cloud comic
                    {
                        int list_offset = (strcmp(ui.cloud_path, "/") != 0) ? 1 : 0;
                        int file_index = ui.cloud_selected_file - list_offset;
                        if (file_index >= 0 && file_index < ui.cloud_files.count) {
                            CloudFileEntry *entry = &ui.cloud_files.entries[file_index];

                            // Build full remote path
                            char remote_path[1024];
                            if (strcmp(ui.cloud_path, "/") == 0) {
                                snprintf(remote_path, sizeof(remote_path), "/%s", entry->name);
                            } else {
                                snprintf(remote_path, sizeof(remote_path), "%s/%s", ui.cloud_path, entry->name);
                            }

                            ui_set_screen(&ui, SCREEN_LOADING);
                            ui_set_message(&ui, "Downloading...");
                            ui_render(&ui);

                            char local_path[512];
                            if (ui_download_comic(&ui, remote_path, local_path, sizeof(local_path)) == 0) {
                                ui.browse_mode = 1; // Remember we came from cloud
                                ui_open_comic(&ui, local_path);
                            } else {
                                ui_set_message(&ui, "Download failed");
                                ui_set_screen(&ui, SCREEN_ERROR);
                            }
                        }
                    }
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
    webdav_cleanup();
    PDL_Quit();
    SDL_Quit();

    return 0;
}
