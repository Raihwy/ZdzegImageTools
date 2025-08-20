#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

// Helper macro to get the pixel value from an SDL surface
// This is for 24-bit RGB, assuming the format is correct.
#define GET_PIXEL(surf, x, y) ((unsigned char*)surf->pixels + y * surf->pitch + x * 3)

// Function to encode an image into the custom .zdzeg format
int zdzeg_encode(const char* input_path, int levels, const char* channel_name) {
    // --- 1. Validate parameters ---
    const char* valid_channels[] = {"red", "green", "blue", "full", "bw"};
    int channel_idx = -1;
    for (int i = 0; i < 5; ++i) {
        if (strcmp(channel_name, valid_channels[i]) == 0) {
            channel_idx = i;
            break;
        }
    }
    if (channel_idx == -1) {
        fprintf(stderr, "Error: Invalid channel '%s'. Must be one of: red, green, blue, full, bw.\n", channel_name);
        return 1;
    }
    if (levels < 4 || levels > 32) {
        fprintf(stderr, "Error: Levels must be between 4 and 32.\n");
        return 1;
    }

    // --- 2. Load Image with SDL_image ---
    SDL_Surface* img_surface = IMG_Load(input_path);
    if (!img_surface) {
        fprintf(stderr, "IMG_Load failed: %s\n", IMG_GetError());
        return 1;
    }

    // Convert to a specific pixel format (RGB24) for easier access
    SDL_Surface* formatted_surface = SDL_ConvertSurfaceFormat(img_surface, SDL_PIXELFORMAT_RGB24, 0);
    SDL_FreeSurface(img_surface);
    if (!formatted_surface) {
        fprintf(stderr, "SDL_ConvertSurfaceFormat failed: %s\n", SDL_GetError());
        return 1;
    }

    int w = formatted_surface->w;
    int h = formatted_surface->h;
    unsigned char* pixels = (unsigned char*)formatted_surface->pixels;

    // --- 3. Quantize the data ---
    int num_channels = 0;
    if (strcmp(channel_name, "full") == 0) num_channels = 3;
    else if (strcmp(channel_name, "bw") == 0) num_channels = 1;
    else num_channels = 1;

    unsigned long pixel_count = (unsigned long)w * h * num_channels;
    unsigned char* quantized_data = (unsigned char*)malloc(pixel_count);
    if (!quantized_data) {
        fprintf(stderr, "Memory allocation for quantized data failed.\n");
        SDL_FreeSurface(formatted_surface);
        return 1;
    }

    if (strcmp(channel_name, "full") == 0) {
        for (int i = 0; i < w * h; ++i) {
            unsigned char* p = GET_PIXEL(formatted_surface, i % w, i / w);
            quantized_data[i * 3 + 0] = (unsigned char)(((int)p[0] * levels) / 256);
            quantized_data[i * 3 + 1] = (unsigned char)(((int)p[1] * levels) / 256);
            quantized_data[i * 3 + 2] = (unsigned char)(((int)p[2] * levels) / 256);
        }
    } else if (strcmp(channel_name, "bw") == 0) {
        for (int i = 0; i < w * h; ++i) {
            unsigned char* p = GET_PIXEL(formatted_surface, i % w, i / w);
            // Convert to grayscale using a simple average
            unsigned char avg = (p[0] + p[1] + p[2]) / 3;
            quantized_data[i] = (unsigned char)(((int)avg * levels) / 256);
        }
    } else { // Single channel (red, green, or blue)
        int ch_offset = channel_idx; // 0 for red, 1 for green, 2 for blue
        for (int i = 0; i < w * h; ++i) {
            unsigned char* p = GET_PIXEL(formatted_surface, i % w, i / w);
            quantized_data[i] = (unsigned char)(((int)p[ch_offset] * levels) / 256);
        }
    }
    SDL_FreeSurface(formatted_surface);

    // --- 4. Run-length encode (RLE) the quantized data ---
    // A dynamic array is needed, so we'll use a larger initial size and realloc as needed.
    size_t rle_capacity = pixel_count * 3 / 2; // A guess for initial size
    unsigned char* rle_data = (unsigned char*)malloc(rle_capacity);
    if (!rle_data) {
        fprintf(stderr, "Memory allocation for RLE data failed.\n");
        free(quantized_data);
        return 1;
    }
    size_t rle_size = 0;

    if (pixel_count > 0) {
        unsigned char current_val = quantized_data[0];
        unsigned short count = 1;
        for (unsigned long i = 1; i < pixel_count; ++i) {
            if (quantized_data[i] == current_val && count < 65535) {
                count++;
            } else {
                // Ensure there's space for the next run
                if (rle_size + 3 > rle_capacity) {
                    rle_capacity *= 2;
                    unsigned char* temp = (unsigned char*)realloc(rle_data, rle_capacity);
                    if (!temp) {
                        fprintf(stderr, "Realloc for RLE data failed.\n");
                        free(quantized_data);
                        free(rle_data);
                        return 1;
                    }
                    rle_data = temp;
                }
                rle_data[rle_size++] = current_val;
                rle_data[rle_size++] = (count >> 8) & 0xFF;
                rle_data[rle_size++] = count & 0xFF;
                current_val = quantized_data[i];
                count = 1;
            }
        }
        // Write the last run
        if (rle_size + 3 > rle_capacity) {
            rle_capacity += 3; // Enough for the last run
            unsigned char* temp = (unsigned char*)realloc(rle_data, rle_capacity);
            if (!temp) {
                fprintf(stderr, "Realloc for final RLE run failed.\n");
                free(quantized_data);
                free(rle_data);
                return 1;
            }
            rle_data = temp;
        }
        rle_data[rle_size++] = current_val;
        rle_data[rle_size++] = (count >> 8) & 0xFF;
        rle_data[rle_size++] = count & 0xFF;
    }

    free(quantized_data);

    // --- 5. Create header and combine with RLE data ---
    unsigned char header[8];
    header[0] = (w >> 24) & 0xFF;
    header[1] = (w >> 16) & 0xFF;
    header[2] = (w >> 8) & 0xFF;
    header[3] = w & 0xFF;
    header[4] = (h >> 24) & 0xFF;
    header[5] = (h >> 16) & 0xFF;
    header[6] = (h >> 8) & 0xFF;
    header[7] = h & 0xFF;

    // --- 6. Compress with zlib ---
    unsigned long source_size = 8 + rle_size;
    unsigned long compressed_size = compressBound(source_size);
    unsigned char* compressed_data = (unsigned char*)malloc(compressed_size);
    if (!compressed_data) {
        fprintf(stderr, "Memory allocation for compressed data failed.\n");
        free(rle_data);
        return 1;
    }

    unsigned char* source_data = (unsigned char*)malloc(source_size);
    if (!source_data) {
        fprintf(stderr, "Memory allocation for source data failed.\n");
        free(compressed_data);
        free(rle_data);
        return 1;
    }
    memcpy(source_data, header, 8);
    memcpy(source_data + 8, rle_data, rle_size);
    free(rle_data);

    int z_result = compress(compressed_data, &compressed_size, source_data, source_size);
    free(source_data);
    if (z_result != Z_OK) {
        fprintf(stderr, "zlib compression failed with error code %d.\n", z_result);
        free(compressed_data);
        return 1;
    }

    // --- 7. Save to file ---
    char output_path[256];
    char* dot = strrchr(input_path, '.');
    if (!dot) dot = (char*)input_path + strlen(input_path);
    int basename_len = dot - input_path;
    snprintf(output_path, sizeof(output_path), "%.*s_%d_%s.zdzeg", basename_len, input_path, levels, channel_name);

    FILE* f = fopen(output_path, "wb");
    if (!f) {
        fprintf(stderr, "Could not open output file: %s\n", output_path);
        free(compressed_data);
        return 1;
    }
    fwrite(compressed_data, 1, compressed_size, f);
    fclose(f);
    free(compressed_data);

    printf("Successfully encoded %s -> %s\n", input_path, output_path);
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <input_path> <levels> <channel>\n", argv[0]);
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

    if (IMG_Init(IMG_INIT_PNG) == 0) { // Or other formats as needed
        fprintf(stderr, "IMG_Init failed: %s\n", IMG_GetError());
        SDL_Quit();
        return 1;
    }

    int levels = atoi(argv[2]);
    int result = zdzeg_encode(argv[1], levels, argv[3]);

    IMG_Quit();
    SDL_Quit();
    return result;
}
