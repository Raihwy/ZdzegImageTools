#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <zlib.h>
#include <SDL2/SDL.h>

// Forward declarations for helper functions
char** get_zdzeg_files(const char* folder, int* count);
void free_file_list(char** files, int count);
SDL_Surface* load_zdzeg(const char* filepath, int* out_w, int* out_h);
SDL_Surface* rotate_surface_90_degrees(SDL_Surface* surface);

// A simple utility to get a value from a filename, e.g., "red", "bw", "16"
// This version makes a local copy to avoid modifying the input string.
int get_levels_from_filename(const char* filename) {
    char temp_filename[256];
    strncpy(temp_filename, filename, sizeof(temp_filename) - 1);
    temp_filename[sizeof(temp_filename) - 1] = '\0';

    char* ext_pos = strrchr(temp_filename, '.');
    if (ext_pos) {
        *ext_pos = '\0';
    }

    char* token = strtok(temp_filename, "_");
    while (token != NULL) {
        int val = atoi(token);
        if (val != 0) {
            return val;
        }
        token = strtok(NULL, "_");
    }
    return 16; // default levels
}

// Function to get the channel string from a filename
int get_channel_from_filename(const char* filename, const char** keywords, int num_keywords) {
    char temp_filename[256];
    strncpy(temp_filename, filename, sizeof(temp_filename) - 1);
    temp_filename[sizeof(temp_filename) - 1] = '\0';

    char* ext_pos = strrchr(temp_filename, '.');
    if (ext_pos) {
        *ext_pos = '\0';
    }

    char* token = strtok(temp_filename, "_");
    while (token != NULL) {
        for (int i = 0; i < num_keywords; ++i) {
            if (strcmp(token, keywords[i]) == 0) {
                return i;
            }
        }
        token = strtok(NULL, "_");
    }
    return 4; // default to "bw" channel
}

// Function to get a list of all .zdzeg files in a directory
char** get_zdzeg_files(const char* folder, int* count) {
    DIR* dir;
    struct dirent* ent;
    char** files = NULL;
    *count = 0;

    dir = opendir(folder);
    if (dir == NULL) {
        fprintf(stderr, "Error opening directory: %s\n", folder);
        return NULL;
    }

    // Pass 1: Count files
    int temp_count = 0;
    while ((ent = readdir(dir)) != NULL) {
        if (strstr(ent->d_name, ".zdzeg") != NULL) {
            temp_count++;
        }
    }
    closedir(dir);

    if (temp_count == 0) {
        *count = 0;
        return NULL;
    }

    // Pass 2: Store file paths
    files = (char**)malloc(temp_count * sizeof(char*));
    if (files == NULL) {
        *count = 0;
        fprintf(stderr, "Memory allocation failed for file list.\n");
        return NULL;
    }

    dir = opendir(folder);
    if (dir == NULL) {
        *count = 0;
        free(files); // Free the newly allocated memory
        fprintf(stderr, "Error re-opening directory: %s\n", folder);
        return NULL;
    }

    int i = 0;
    while ((ent = readdir(dir)) != NULL) {
        if (strstr(ent->d_name, ".zdzeg") != NULL) {
            size_t path_len = strlen(folder) + strlen(ent->d_name) + 2;
            files[i] = (char*)malloc(path_len);
            if (files[i] == NULL) {
                // Cleanup and return NULL
                for (int j = 0; j < i; ++j) free(files[j]);
                free(files);
                closedir(dir);
                *count = 0;
                return NULL;
            }
            sprintf(files[i], "%s/%s", folder, ent->d_name);
            i++;
        }
    }
    closedir(dir);

    *count = temp_count;
    return files;
}

// Helper to free the memory for the file list
void free_file_list(char** files, int count) {
    for (int i = 0; i < count; ++i) {
        free(files[i]);
    }
    free(files);
}

// Loads and decodes a .zdzeg file into an SDL_Surface
SDL_Surface* load_zdzeg(const char* filepath, int* out_w, int* out_h) {
    FILE* f = fopen(filepath, "rb");
    if (!f) {
        fprintf(stderr, "Could not open file: %s\n", filepath);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long compressed_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* compressed_data = malloc(compressed_size);
    if (!compressed_data) {
        fclose(f);
        return NULL;
    }
    fread(compressed_data, 1, compressed_size, f);
    fclose(f);

    // Guess uncompressed size
    unsigned long uncompressed_size = compressed_size * 20;
    unsigned char* uncompressed_data = malloc(uncompressed_size);
    if (!uncompressed_data) {
        free(compressed_data);
        return NULL;
    }

    int z_result = uncompress(uncompressed_data, &uncompressed_size, compressed_data, compressed_size);
    if (z_result == Z_BUF_ERROR) {
        // reallocate exactly
        free(uncompressed_data);
        uncompressed_data = malloc(uncompressed_size);
        if (!uncompressed_data) {
            free(compressed_data);
            return NULL;
        }
        z_result = uncompress(uncompressed_data, &uncompressed_size, compressed_data, compressed_size);
    }
    free(compressed_data);

    if (z_result != Z_OK) {
        fprintf(stderr, "Decompression failed for %s (code %d)\n", filepath, z_result);
        free(uncompressed_data);
        return NULL;
    }

    if (uncompressed_size < 8) {
        fprintf(stderr, "File too small to contain header: %s\n", filepath);
        free(uncompressed_data);
        return NULL;
    }

    // Header
    int w = (uncompressed_data[0] << 24) | (uncompressed_data[1] << 16) |
    (uncompressed_data[2] << 8) | uncompressed_data[3];
    int h = (uncompressed_data[4] << 24) | (uncompressed_data[5] << 16) |
    (uncompressed_data[6] << 8) | uncompressed_data[7];

    if (w <= 0 || h <= 0) {
        fprintf(stderr, "Invalid image dimensions: %dx%d\n", w, h);
        free(uncompressed_data);
        return NULL;
    }
    *out_w = w;
    *out_h = h;

    const unsigned char* raw_rle = uncompressed_data + 8;
    unsigned long raw_rle_len = uncompressed_size - 8;

    // Determine channel type from filename
    char* filename = strrchr(filepath, '/') ? strrchr(filepath, '/') + 1 : (char*)filepath;
    const char* channels[] = {"red", "green", "blue", "full", "bw"};
    int channel_idx = get_channel_from_filename(filename, channels, 5);
    int levels_val = get_levels_from_filename(filename);

    int num_channels = (strcmp(channels[channel_idx], "full") == 0) ? 3 : 1;

    unsigned char* pixels_decoded = malloc(w * h * num_channels);
    if (!pixels_decoded) {
        free(uncompressed_data);
        return NULL;
    }

    // RLE decoding
    int pixels_idx = 0;
    for (unsigned long i = 0; i + 2 < raw_rle_len; i += 3) {
        unsigned char val = raw_rle[i];
        unsigned short count = (raw_rle[i+1] << 8) | raw_rle[i+2];

        if ((unsigned long)(pixels_idx + count) > (unsigned long)(w * h * num_channels)) {
            fprintf(stderr, "RLE count exceeds buffer, corrupt file: %s\n", filepath);
            free(uncompressed_data);
            free(pixels_decoded);
            return NULL;
        }

        for (int k = 0; k < count; ++k) {
            pixels_decoded[pixels_idx++] = val;
        }
    }

    free(uncompressed_data);

    // Map values to 0-255
    float* channel_values = malloc(levels_val * sizeof(float));
    if (!channel_values) {
        free(pixels_decoded);
        return NULL;
    }
    for (int i = 0; i < levels_val; ++i)
        channel_values[i] = (float)i * 255.0f / (levels_val - 1);

    // Create SDL surface
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 24, SDL_PIXELFORMAT_RGB24);
    if (!surface) {
        fprintf(stderr, "SDL_CreateRGBSurface failed: %s\n", SDL_GetError());
        free(pixels_decoded);
        free(channel_values);
        return NULL;
    }
    SDL_SetSurfacePalette(surface, NULL); // Clear palette if any

    unsigned char* surface_pixels = (unsigned char*)surface->pixels;
    if (strcmp(channels[channel_idx], "full") == 0) {
        for (int i = 0; i < w * h; ++i) {
            surface_pixels[i*3 + 0] = (unsigned char)channel_values[pixels_decoded[i*3+0]];
            surface_pixels[i*3 + 1] = (unsigned char)channel_values[pixels_decoded[i*3+1]];
            surface_pixels[i*3 + 2] = (unsigned char)channel_values[pixels_decoded[i*3+2]];
        }
    } else if (strcmp(channels[channel_idx], "bw") == 0) {
        for (int i = 0; i < w * h; ++i) {
            unsigned char val = (unsigned char)channel_values[pixels_decoded[i]];
            surface_pixels[i*3 + 0] = val;
            surface_pixels[i*3 + 1] = val;
            surface_pixels[i*3 + 2] = val;
        }
    } else { // single channel
        int c_idx = (strcmp(channels[channel_idx], "red") == 0) ? 0 :
        (strcmp(channels[channel_idx], "green") == 0) ? 1 : 2;
        memset(surface_pixels, 0, w*h*3); // Clear the surface first
        for (int i = 0; i < w * h; ++i) {
            surface_pixels[i*3 + c_idx] = (unsigned char)channel_values[pixels_decoded[i]];
        }
    }

    free(pixels_decoded);
    free(channel_values);
    return surface;
}

// Function to rotate an SDL_Surface 90 degrees clockwise
SDL_Surface* rotate_surface_90_degrees(SDL_Surface* surface) {
    if (!surface) {
        return NULL;
    }

    // Get old dimensions
    int old_w = surface->w;
    int old_h = surface->h;

    // Create a new surface with swapped dimensions
    SDL_Surface* new_surface = SDL_CreateRGBSurfaceWithFormat(0, old_h, old_w, 24, SDL_PIXELFORMAT_RGB24);
    if (!new_surface) {
        fprintf(stderr, "Failed to create new surface for rotation: %s\n", SDL_GetError());
        return NULL;
    }

    // Lock both surfaces to access pixel data
    SDL_LockSurface(surface);
    SDL_LockSurface(new_surface);

    unsigned char* old_pixels = (unsigned char*)surface->pixels;
    unsigned char* new_pixels = (unsigned char*)new_surface->pixels;

    for (int y = 0; y < old_h; ++y) {
        for (int x = 0; x < old_w; ++x) {
            // Calculate pixel index in old and new surface
            int old_idx = (y * old_w + x) * 3;
            int new_idx = ((old_w - 1 - x) * old_h + y) * 3;

            // Copy RGB values
            new_pixels[new_idx + 0] = old_pixels[old_idx + 0];
            new_pixels[new_idx + 1] = old_pixels[old_idx + 1];
            new_pixels[new_idx + 2] = old_pixels[old_idx + 2];
        }
    }

    // Unlock surfaces
    SDL_UnlockSurface(surface);
    SDL_UnlockSurface(new_surface);

    return new_surface;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s /path/to/folder\n", argv[0]);
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL could not initialize! SDL Error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Zdzeg Viewer (C)", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 800, 600, SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf(stderr, "Window could not be created! SDL Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        fprintf(stderr, "Renderer could not be created! SDL Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    int file_count = 0;
    char** files = get_zdzeg_files(argv[1], &file_count);
    if (file_count == 0) {
        fprintf(stderr, "No .zdzeg files found in folder!\n");
        free(files);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    int current_idx = 0;
    int img_w, img_h;
    SDL_Surface* pil_image = load_zdzeg(files[current_idx], &img_w, &img_h);
    if (!pil_image) {
        fprintf(stderr, "Initial image load failed. Exiting.\n");
        free_file_list(files, file_count);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Main loop variables
    int running = 1;
    int fullscreen = 0;
    int fit_screen = 0;
    float zoom = 1.0f;
    int scroll_x = 0;
    int scroll_y = 0;
    float scroll_speed = 10.0f;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            } else if (event.type == SDL_KEYDOWN) {
                switch (event.key.keysym.sym) {
                    case SDLK_r: {
                        SDL_Surface* new_pil_image = rotate_surface_90_degrees(pil_image);
                        if (new_pil_image) {
                            SDL_FreeSurface(pil_image);
                            pil_image = new_pil_image;
                            // Swap dimensions
                            int temp_w = img_w;
                            img_w = img_h;
                            img_h = temp_w;

                            zoom = 1.0f;
                            scroll_x = 0;
                            scroll_y = 0;
                        } else {
                            fprintf(stderr, "Failed to rotate image.\n");
                        }
                        break;
                    }
                    case SDLK_RIGHT: {
                        int prev_idx = current_idx;
                        current_idx = (current_idx + 1) % file_count;
                        SDL_Surface* new_pil_image = load_zdzeg(files[current_idx], &img_w, &img_h);
                        if (new_pil_image) {
                            SDL_FreeSurface(pil_image);
                            pil_image = new_pil_image;
                            zoom = 1.0f;
                            scroll_x = 0;
                            scroll_y = 0;
                        } else {
                            fprintf(stderr, "Failed to load next image, staying on current one.\n");
                            current_idx = prev_idx;
                        }
                        break;
                    }
                    case SDLK_LEFT: {
                        int prev_idx = current_idx;
                        current_idx = (current_idx - 1 + file_count) % file_count;
                        SDL_Surface* new_pil_image = load_zdzeg(files[current_idx], &img_w, &img_h);
                        if (new_pil_image) {
                            SDL_FreeSurface(pil_image);
                            pil_image = new_pil_image;
                            zoom = 1.0f;
                            scroll_x = 0;
                            scroll_y = 0;
                        } else {
                            fprintf(stderr, "Failed to load previous image, staying on current one.\n");
                            current_idx = prev_idx;
                        }
                        break;
                    }
                    case SDLK_f:
                        fullscreen = !fullscreen;
                        SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                        break;
                    case SDLK_ESCAPE:
                        if (fullscreen) {
                            fullscreen = 0;
                            SDL_SetWindowFullscreen(window, 0);
                        }
                        break;
                    case SDLK_h:
                        fit_screen = !fit_screen;
                        zoom = 1.0f;
                        scroll_x = 0;
                        scroll_y = 0;
                        break;
                    case SDLK_UP:
                        if (!fit_screen) zoom *= 1.1f;
                        break;
                    case SDLK_DOWN:
                        if (!fit_screen) zoom /= 1.1f;
                        break;
                    case SDLK_PAGEDOWN:
                        scroll_y += 50;
                        break;
                    case SDLK_PAGEUP:
                        scroll_y -= 50;
                        break;
                    default:
                        break;
                }
            }
        }

        // Continuous panning
        const Uint8* state = SDL_GetKeyboardState(NULL);
        if (!fit_screen) {
            if (state[SDL_SCANCODE_W]) scroll_y -= scroll_speed;
            if (state[SDL_SCANCODE_S]) scroll_y += scroll_speed;
            if (state[SDL_SCANCODE_A]) scroll_x -= scroll_speed;
            if (state[SDL_SCANCODE_D]) scroll_x += scroll_speed;
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        // Get window size for rendering
        int win_w, win_h;
        SDL_GetWindowSize(window, &win_w, &win_h);

        SDL_Rect dest_rect;

        if (fit_screen) {
            float ratio_w = (float)win_w / img_w;
            float ratio_h = (float)win_h / img_h;
            float ratio = ratio_w < ratio_h ? ratio_w : ratio_h;
            dest_rect.w = (int)(img_w * ratio);
            dest_rect.h = (int)(img_h * ratio);
            dest_rect.x = (win_w - dest_rect.w) / 2;
            dest_rect.y = (win_h - dest_rect.h) / 2;
        } else {
            dest_rect.w = (int)(img_w * zoom);
            dest_rect.h = (int)(img_h * zoom);
            dest_rect.x = (win_w - dest_rect.w) / 2 - scroll_x;
            dest_rect.y = (win_h - dest_rect.h) / 2 - scroll_y;
        }

        // Create texture from the surface and render it
        if (pil_image) {
            SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, pil_image);
            if (texture) {
                SDL_RenderCopy(renderer, texture, NULL, &dest_rect);
                SDL_DestroyTexture(texture);
            } else {
                fprintf(stderr, "Texture could not be created! SDL Error: %s\n", SDL_GetError());
            }
        }

        SDL_RenderPresent(renderer);
    }

    // Cleanup
    if (pil_image) SDL_FreeSurface(pil_image);
    free_file_list(files, file_count);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
