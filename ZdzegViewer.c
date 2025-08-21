#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <libgen.h>
#include <zlib.h>
#include <sys/stat.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

// Forward declarations
char** get_zdzeg_files(const char* folder, int* count);
void free_file_list(char** files, int count);
SDL_Surface* load_zdzeg(const char* filepath, int* out_w, int* out_h);
SDL_Surface* rotate_surface_90_degrees(SDL_Surface* surface);
int get_levels_from_filename(const char* filename);
int get_channel_from_filename(const char* filename, const char** keywords, int num_keywords);
char** get_folder_content(const char* folder, int* subfolder_count, int* zdzeg_count);
void draw_menu(SDL_Renderer* renderer, TTF_Font* font, char** folders, int folder_count, int selected_idx);
TTF_Font* find_and_open_font(int pt_size);

// Helper function to get a value from a filename, e.g., "16"
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
    return 16;
}

// Helper function to get channel from filename
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
    return 4;
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
    int temp_count = 0;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_type == DT_REG && strstr(ent->d_name, ".zdzeg") != NULL) {
            temp_count++;
        }
    }
    closedir(dir);
    if (temp_count == 0) {
        *count = 0;
        return NULL;
    }
    files = (char**)malloc(temp_count * sizeof(char*));
    if (files == NULL) {
        *count = 0;
        fprintf(stderr, "Memory allocation failed for file list.\n");
        return NULL;
    }
    dir = opendir(folder);
    if (dir == NULL) {
        *count = 0;
        free(files);
        fprintf(stderr, "Error re-opening directory: %s\n", folder);
        return NULL;
    }
    int i = 0;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_type == DT_REG && strstr(ent->d_name, ".zdzeg") != NULL) {
            size_t path_len = strlen(folder) + strlen(ent->d_name) + 2;
            files[i] = (char*)malloc(path_len);
            if (files[i] == NULL) {
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
    if (files) {
        for (int i = 0; i < count; ++i) {
            free(files[i]);
        }
        free(files);
    }
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
    unsigned long uncompressed_size = compressed_size * 20;
    unsigned char* uncompressed_data = malloc(uncompressed_size);
    if (!uncompressed_data) {
        free(compressed_data);
        return NULL;
    }
    int z_result = uncompress(uncompressed_data, &uncompressed_size, compressed_data, compressed_size);
    if (z_result == Z_BUF_ERROR) {
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
    int w = (uncompressed_data[0] << 24) | (uncompressed_data[1] << 16) | (uncompressed_data[2] << 8) | uncompressed_data[3];
    int h = (uncompressed_data[4] << 24) | (uncompressed_data[5] << 16) | (uncompressed_data[6] << 8) | uncompressed_data[7];
    if (w <= 0 || h <= 0) {
        fprintf(stderr, "Invalid image dimensions: %dx%d\n", w, h);
        free(uncompressed_data);
        return NULL;
    }
    *out_w = w;
    *out_h = h;
    const unsigned char* raw_rle = uncompressed_data + 8;
    unsigned long raw_rle_len = uncompressed_size - 8;
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
    float* channel_values = malloc(levels_val * sizeof(float));
    if (!channel_values) {
        free(pixels_decoded);
        return NULL;
    }
    for (int i = 0; i < levels_val; ++i)
        channel_values[i] = (float)i * 255.0f / (levels_val - 1);
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 24, SDL_PIXELFORMAT_RGB24);
    if (!surface) {
        fprintf(stderr, "SDL_CreateRGBSurface failed: %s\n", SDL_GetError());
        free(pixels_decoded);
        free(channel_values);
        return NULL;
    }
    SDL_SetSurfacePalette(surface, NULL);
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
    } else {
        int c_idx = (strcmp(channels[channel_idx], "red") == 0) ? 0 : (strcmp(channels[channel_idx], "green") == 0) ? 1 : 2;
        memset(surface_pixels, 0, w*h*3);
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
    if (!surface) return NULL;
    int old_w = surface->w;
    int old_h = surface->h;
    SDL_Surface* new_surface = SDL_CreateRGBSurfaceWithFormat(0, old_h, old_w, 24, SDL_PIXELFORMAT_RGB24);
    if (!new_surface) {
        fprintf(stderr, "Failed to create new surface for rotation: %s\n", SDL_GetError());
        return NULL;
    }
    SDL_LockSurface(surface);
    SDL_LockSurface(new_surface);
    unsigned char* old_pixels = (unsigned char*)surface->pixels;
    unsigned char* new_pixels = (unsigned char*)new_surface->pixels;
    for (int y = 0; y < old_h; ++y) {
        for (int x = 0; x < old_w; ++x) {
            int old_idx = (y * old_w + x) * 3;
            int new_idx = ((old_w - 1 - x) * old_h + y) * 3;
            new_pixels[new_idx + 0] = old_pixels[old_idx + 0];
            new_pixels[new_idx + 1] = old_pixels[old_idx + 1];
            new_pixels[new_idx + 2] = old_pixels[old_idx + 2];
        }
    }
    SDL_UnlockSurface(surface);
    SDL_UnlockSurface(new_surface);
    return new_surface;
}

// Scans a folder for subdirectories and .zdzeg files
char** get_folder_content(const char* folder, int* subfolder_count, int* zdzeg_count) {
    DIR* dir;
    struct dirent* ent;
    char** subfolders = NULL;
    *subfolder_count = 0;
    *zdzeg_count = 0;
    dir = opendir(folder);
    if (dir == NULL) {
        fprintf(stderr, "Error opening directory: %s\n", folder);
        return NULL;
    }
    int temp_sub_count = 0;
    int temp_file_count = 0;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_type == DT_DIR && strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
            temp_sub_count++;
        }
        if (ent->d_type == DT_REG && strstr(ent->d_name, ".zdzeg") != NULL) {
            temp_file_count++;
        }
    }
    closedir(dir);
    *subfolder_count = temp_sub_count;
    *zdzeg_count = temp_file_count;
    if (temp_sub_count == 0) {
        return NULL;
    }
    subfolders = (char**)malloc(temp_sub_count * sizeof(char*));
    if (!subfolders) {
        fprintf(stderr, "Memory allocation failed for subfolder list.\n");
        return NULL;
    }
    dir = opendir(folder);
    if (!dir) {
        free(subfolders);
        fprintf(stderr, "Error re-opening directory: %s\n", folder);
        return NULL;
    }
    int i = 0;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_type == DT_DIR && strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
            subfolders[i] = strdup(ent->d_name);
            if (!subfolders[i]) {
                for (int j = 0; j < i; ++j) free(subfolders[j]);
                free(subfolders);
                closedir(dir);
                return NULL;
            }
            i++;
        }
    }
    closedir(dir);
    return subfolders;
}

// Draws the folder selection menu
void draw_menu(SDL_Renderer* renderer, TTF_Font* font, char** folders, int folder_count, int selected_idx) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    if (!font) {
        fprintf(stderr, "Font not loaded, cannot draw menu.\n");
        SDL_RenderPresent(renderer);
        return;
    }
    SDL_Color white = {255, 255, 255, 255};
    SDL_Color yellow = {255, 255, 0, 255};
    int y_pos = 50;
    for (int i = 0; i < folder_count; ++i) {
        SDL_Color color = (i == selected_idx) ? yellow : white;
        SDL_Surface* text_surface = TTF_RenderText_Solid(font, folders[i], color);
        if (text_surface) {
            SDL_Texture* text_texture = SDL_CreateTextureFromSurface(renderer, text_surface);
            SDL_Rect dest_rect = {50, y_pos, text_surface->w, text_surface->h};
            SDL_RenderCopy(renderer, text_texture, NULL, &dest_rect);
            SDL_DestroyTexture(text_texture);
            SDL_FreeSurface(text_surface);
            y_pos += 30;
        }
    }
    SDL_RenderPresent(renderer);
}

// Finds and opens a font
TTF_Font* find_and_open_font(int pt_size) {
    TTF_Font* font = NULL;
    font = TTF_OpenFont("monospace", pt_size);
    if (font) return font;
    const char* common_fonts[] = {
        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/truetype/ubuntu-font-family/UbuntuMono-R.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf"
    };
    for (int i = 0; i < sizeof(common_fonts) / sizeof(common_fonts[0]); ++i) {
        font = TTF_OpenFont(common_fonts[i], pt_size);
        if (font) {
            fprintf(stdout, "Found font at: %s\n", common_fonts[i]);
            return font;
        }
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s /path/to/folder_or_file.zdzeg\n", argv[0]);
        return 1;
    }
    if (SDL_Init(SDL_INIT_VIDEO) < 0 || TTF_Init() < 0) {
        fprintf(stderr, "SDL/TTF could not initialize! SDL Error: %s\n", SDL_GetError());
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

    TTF_Font* font = find_and_open_font(24);
    if (!font) {
        fprintf(stderr, "Failed to load any font.\n");
        TTF_Quit();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    char* current_path = NULL;
    int in_menu = 0;
    int menu_selection_idx = 0;
    int subfolder_count = 0;
    int file_count = 0;
    char** subfolders = NULL;
    char** files = NULL;
    int current_idx = 0;
    int img_w = 0, img_h = 0;
    SDL_Surface* pil_image = NULL;

    struct stat path_stat;
    if (stat(argv[1], &path_stat) != 0) {
        fprintf(stderr, "Error accessing path: %s\n", argv[1]);
        return 1;
    }

    if (S_ISDIR(path_stat.st_mode)) {
        current_path = strdup(argv[1]);
        if (!current_path) {
            fprintf(stderr, "Path allocation failed.\n");
            return 1;
        }
        subfolders = get_folder_content(current_path, &subfolder_count, &file_count);
        if (subfolder_count > 0 || file_count > 0) {
            in_menu = 1;
        } else {
            fprintf(stderr, "The specified directory is empty.\n");
            return 1;
        }
    } else if (S_ISREG(path_stat.st_mode)) {
        in_menu = 0;
        pil_image = load_zdzeg(argv[1], &img_w, &img_h);
        if (!pil_image) {
            fprintf(stderr, "Failed to load the specified file: %s\n", argv[1]);
            return 1;
        }
        char* parent_path_temp = strdup(argv[1]);
        current_path = strdup(dirname(parent_path_temp));
        free(parent_path_temp);
        files = get_zdzeg_files(current_path, &file_count);
        if (files) {
            for (int i = 0; i < file_count; ++i) {
                if (strcmp(files[i], argv[1]) == 0) {
                    current_idx = i;
                    break;
                }
            }
        }
    } else {
        fprintf(stderr, "Path is not a valid directory or file.\n");
        return 1;
    }

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
                if (in_menu) {
                    switch (event.key.keysym.sym) {
                        case SDLK_UP:
                            menu_selection_idx = (menu_selection_idx - 1 + subfolder_count) % subfolder_count;
                            break;
                        case SDLK_DOWN:
                            menu_selection_idx = (menu_selection_idx + 1) % subfolder_count;
                            break;
                        case SDLK_RETURN:
                        case SDLK_KP_ENTER: {
                            char* new_path_temp = (char*)malloc(strlen(current_path) + strlen(subfolders[menu_selection_idx]) + 2);
                            if (new_path_temp) {
                                sprintf(new_path_temp, "%s/%s", current_path, subfolders[menu_selection_idx]);
                                free(current_path);
                                current_path = new_path_temp;
                                free_file_list(subfolders, subfolder_count);
                                subfolders = NULL;
                                files = get_zdzeg_files(current_path, &file_count);
                                if (file_count > 0) {
                                    in_menu = 0;
                                    current_idx = 0;
                                    if (pil_image) SDL_FreeSurface(pil_image);
                                    pil_image = load_zdzeg(files[current_idx], &img_w, &img_h);
                                    zoom = 1.0f;
                                    scroll_x = 0;
                                    scroll_y = 0;
                                } else {
                                    subfolders = get_folder_content(current_path, &subfolder_count, &file_count);
                                    if (subfolder_count == 0) {
                                        fprintf(stderr, "No images or subfolders found in the selected folder.\n");
                                        in_menu = 1;
                                    } else {
                                        in_menu = 1;
                                        menu_selection_idx = 0;
                                    }
                                }
                            }
                            break;
                        }
                        case SDLK_x: {
                            char* parent_path = strdup(current_path);
                            char* dir = dirname(parent_path);
                            if (strcmp(dir, current_path) != 0) {
                                free(current_path);
                                current_path = strdup(dir);
                                free_file_list(subfolders, subfolder_count);
                                subfolders = get_folder_content(current_path, &subfolder_count, &file_count);
                                menu_selection_idx = 0;
                            }
                            free(parent_path);
                            break;
                        }
                    }
                } else {
                    switch (event.key.keysym.sym) {
                        case SDLK_RIGHT: {
                            int prev_idx = current_idx;
                            current_idx = (current_idx + 1) % file_count;
                            if (pil_image) SDL_FreeSurface(pil_image);
                            pil_image = load_zdzeg(files[current_idx], &img_w, &img_h);
                            if (!pil_image) {
                                fprintf(stderr, "Failed to load next image, staying on current one.\n");
                                current_idx = prev_idx;
                                pil_image = load_zdzeg(files[current_idx], &img_w, &img_h);
                            }
                            zoom = 1.0f;
                            scroll_x = 0;
                            scroll_y = 0;
                            break;
                        }
                        case SDLK_LEFT: {
                            int prev_idx = current_idx;
                            current_idx = (current_idx - 1 + file_count) % file_count;
                            if (pil_image) SDL_FreeSurface(pil_image);
                            pil_image = load_zdzeg(files[current_idx], &img_w, &img_h);
                            if (!pil_image) {
                                fprintf(stderr, "Failed to load previous image, staying on current one.\n");
                                current_idx = prev_idx;
                                pil_image = load_zdzeg(files[current_idx], &img_w, &img_h);
                            }
                            zoom = 1.0f;
                            scroll_x = 0;
                            scroll_y = 0;
                            break;
                        }
                        case SDLK_r: {
                            SDL_Surface* new_pil_image = rotate_surface_90_degrees(pil_image);
                            if (new_pil_image) {
                                SDL_FreeSurface(pil_image);
                                pil_image = new_pil_image;
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
                        case SDLK_x: {
                            if (pil_image) SDL_FreeSurface(pil_image);
                            free_file_list(files, file_count);
                            files = NULL;
                            char* parent_path = strdup(current_path);
                            free(current_path);
                            current_path = strdup(dirname(parent_path));
                            free(parent_path);
                            subfolders = get_folder_content(current_path, &subfolder_count, &file_count);
                            in_menu = 1;
                            menu_selection_idx = 0;
                            break;
                        }
                    }
                }
            }
        }
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        if (in_menu) {
            draw_menu(renderer, font, subfolders, subfolder_count, menu_selection_idx);
        } else {
            const Uint8* state = SDL_GetKeyboardState(NULL);
            if (!fit_screen) {
                if (state[SDL_SCANCODE_W]) scroll_y -= scroll_speed;
                if (state[SDL_SCANCODE_S]) scroll_y += scroll_speed;
                if (state[SDL_SCANCODE_A]) scroll_x -= scroll_speed;
                if (state[SDL_SCANCODE_D]) scroll_x += scroll_speed;
            }
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
    }
    if (pil_image) SDL_FreeSurface(pil_image);
    free_file_list(files, file_count);
    free_file_list(subfolders, subfolder_count);
    free(current_path);
    TTF_CloseFont(font);
    TTF_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
