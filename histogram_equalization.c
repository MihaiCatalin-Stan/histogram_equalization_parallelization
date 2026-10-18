#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#define GRAY_LEVELS 256

typedef struct {
    int width;
    int height;
    int max_value;
    uint8_t *pixels;
} GrayImage;

void histogram_equalization(
    const GrayImage input,
    GrayImage *output,
    int width,
    int height
) {
    long long int histogram[GRAY_LEVELS] = {0};
    long long int cdf[GRAY_LEVELS] = {0};
    uint8_t mapping[GRAY_LEVELS];

    long long int total_pixels = width * height;

    output->pixels = malloc(total_pixels * sizeof(uint8_t));

    if (!output->pixels) {
        printf("Malloc failed for output->pixels\n");
        return;
    }

    output->height = input.height;
    output->width = input.width;
    output->max_value = 255;

    // 1. Calculate the histogram - Fully parallel
    for (int i = 0; i < total_pixels; i++) {
        histogram[input.pixels[i]]++;
    }

    // Barrier

    // 2. Calculate the cumulative distribution function - Non-Parallel (requires precedent value)
    cdf[0] = histogram[0];

    for (int i = 1; i < GRAY_LEVELS; i++) {
        cdf[i] = cdf[i - 1] + histogram[i];
    }

    // 3. Find the first nonzero CDF value - Can probably be done at the same time with 2?
    int cdf_min = 0;

    for (int i = 0; i < GRAY_LEVELS; i++) {
        if (cdf[i] > 0) {
            cdf_min = cdf[i];
            break;
        }
    }

    // 4. Build the intensity mapping
    if (total_pixels == cdf_min) {
        // All pixels have the same intensity. - Fully parallel, probably also skippable
        // No contrast can be created by equalization.
        for (int i = 0; i < total_pixels; i++) {
            output->pixels[i] = input.pixels[i];
        }
        return;
    }

    // Parallel by up to 64 threads or sth?
    for (int i = 0; i < GRAY_LEVELS; i++) {
        double value =
            (double)(cdf[i] - cdf_min) * 255.0
            / (total_pixels - cdf_min);

        if (value < 0.0) value = 0.0;
        if (value > 255.0) value = 255.0;

        mapping[i] = (uint8_t)round(value);
    }

    // Barrier, all threads need the mapping done

    // 5. Apply the mapping to every pixel - Fully parallel
    for (int i = 0; i < total_pixels; i++) {
        output->pixels[i] = mapping[input.pixels[i]];
    }

    return;
}

static int read_pgm_int(FILE *file, int *value)
{
    int c;

    /* Skip whitespace and comments. */
    for (;;) {
        c = fgetc(file);

        if (c == EOF)
            return 0;

        if (isspace((unsigned char)c))
            continue;

        if (c == '#') {
            while ((c = fgetc(file)) != '\n' && c != EOF)
                ;
            continue;
        }

        break;
    }

    /* Parse a non-negative decimal integer. */
    if (c < '0' || c > '9')
        return 0;

    int result = 0;

    do {
        int digit = c - '0';

        if (result > (INT_MAX - digit) / 10)
            return 0;

        result = result * 10 + digit;
        c = fgetc(file);
    } while (c >= '0' && c <= '9');

    if (c != EOF && !isspace((unsigned char)c))
        return 0;

    *value = result;
    return 1;
}

int read_pgm(const char *filename, GrayImage *image)
{
    FILE *file = fopen(filename, "rb");

    if (file == NULL) {
        perror("Could not open image");
        return 0;
    }

    image->pixels = NULL;

    char magic[3];

    if (fscanf(file, "%2s", magic) != 1 ||
        (strcmp(magic, "P2") != 0 &&
         strcmp(magic, "P5") != 0)) {
        fprintf(stderr, "Only P2 and P5 PGM files are supported.\n");
        fclose(file);
        return 0;
    }

    int is_binary = (strcmp(magic, "P5") == 0);

    if (!read_pgm_int(file, &image->width) ||
        !read_pgm_int(file, &image->height) ||
        !read_pgm_int(file, &image->max_value)) {
        fprintf(stderr, "Invalid PGM header.\n");
        fclose(file);
        return 0;
    }

    if (image->width <= 0 || image->height <= 0 ||
        image->max_value < 1 || image->max_value > 255 ||
        (size_t)image->width >
        SIZE_MAX / (size_t)image->height) {
        fprintf(stderr, "Unsupported or invalid image dimensions/value.\n");
        fclose(file);
        return 0;
    }

    size_t count =
        (size_t)image->width * (size_t)image->height;

    image->pixels = malloc(count * sizeof(uint8_t));

    if (image->pixels == NULL) {
        fprintf(stderr, "Memory allocation failed.\n");
        fclose(file);
        return 0;
    }

    if (is_binary) {
        /* P5: one byte per pixel when max_value <= 255. */
        if (fread(image->pixels, sizeof(uint8_t), count, file) != count) {
            fprintf(stderr, "Incomplete P5 pixel data.\n");
            free(image->pixels);
            image->pixels = NULL;
            fclose(file);
            return 0;
        }

        /* Validate pixels against the declared maximum. */
        for (size_t i = 0; i < count; i++) {
            if (image->pixels[i] > image->max_value) {
                fprintf(stderr, "Invalid P5 pixel value.\n");
                free(image->pixels);
                image->pixels = NULL;
                fclose(file);
                return 0;
            }
        }
    } else {
        /* P2: pixel values are ASCII decimal numbers. */
        for (size_t i = 0; i < count; i++) {
            int value;

            if (!read_pgm_int(file, &value) ||
                value < 0 || value > image->max_value) {
                fprintf(stderr, "Invalid or missing P2 pixel data.\n");
                free(image->pixels);
                image->pixels = NULL;
                fclose(file);
                return 0;
            }

            image->pixels[i] = (uint8_t)value;
        }
    }

    fclose(file);
    return 1;
}

int save_pgm(const char *filename, const GrayImage *image)
{
    printf("Filename %s\n width: %d | height %d | max_val %d\n", filename, image->width, image->height, image->max_value);
    if (!filename || !image || !image->pixels ||
        image->width <= 0 || image->height <= 0 ||
        image->max_value < 1 || image->max_value > 255) {
        fprintf(stderr, "Invalid image or filename.\n");
        return 0;
    }

    const char *dot = strrchr(filename, '.');
    size_t base_len = dot ? (size_t)(dot - filename)
                          : strlen(filename);

    char *output_name = malloc(base_len + sizeof("_output.pgm"));
    if (!output_name) {
        perror("Memory allocation failed");
        return 0;
    }

    memcpy(output_name, filename, base_len);
    strcpy(output_name + base_len, "_output.pgm");

    FILE *file = fopen(output_name, "w");
    if (!file) {
        perror("Could not create output file");
        free(output_name);
        return 0;
    }

    fprintf(file, "P2\n%d %d\n%d\n",
            image->width, image->height, image->max_value);

    size_t count = (size_t)image->width * image->height;

    for (size_t i = 0; i < count; i++) {
        fprintf(file, "%u ", (unsigned int)image->pixels[i]);

        if ((i + 1) % image->width == 0)
            fputc('\n', file);
    }

    int ok = !ferror(file);
    if (fclose(file) != 0)
        ok = 0;

    if (!ok)
        fprintf(stderr, "Error writing output file.\n");

    free(output_name);
    return ok;
}

int main(int argc, char *argv[]) {
    // Example image: 4 pixels wide, 2 pixels high
    if (argc != 2) {
        printf("Usage: ./histogram [path_to_pgm_image]");
        return 1;
    }

    GrayImage input, output;

    if (!read_pgm(argv[1], &input)) {
        return 1;
    }

    printf("Read succesfully\n");

    histogram_equalization(input, &output, input.width, input.height);


    if(!save_pgm(argv[1], &output)) {
        printf("Failed to save image to file\n");
        free(input.pixels);
        free(output.pixels);
        return 1;
    }

    return 0;
}