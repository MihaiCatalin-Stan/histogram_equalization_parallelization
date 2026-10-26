#ifdef PROFILE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#define GRAY_LEVELS 256

#ifdef PROFILE
/* Per-step wall-clock timing, enabled with -DPROFILE (make profile). */
#include <time.h>

enum { P_READ, P_ALLOC, P_HIST, P_CDF, P_MAP, P_APPLY, P_WRITE, P_COUNT };

static double prof_ms[P_COUNT];

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

#define PROF_DECL(t)      double t = now_ms()
#define PROF_RESET(t)     ((t) = now_ms())
#define PROF_LAP(t, id)   do { double n_ = now_ms(); prof_ms[id] = n_ - (t); (t) = n_; } while (0)
#else
#define PROF_DECL(t)
#define PROF_RESET(t)
#define PROF_LAP(t, id)
#endif

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

    PROF_DECL(t);

    output->pixels = malloc(total_pixels * sizeof(uint8_t));

    if (!output->pixels) {
        printf("Malloc failed for output->pixels\n");
        return;
    }

    output->height = input.height;
    output->width = input.width;
    output->max_value = 255;

    PROF_LAP(t, P_ALLOC);

    // 1. Calculate the histogram - Fully parallel
    for (int i = 0; i < total_pixels; i++) {
        histogram[input.pixels[i]]++;
    }

    PROF_LAP(t, P_HIST);

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

    PROF_LAP(t, P_CDF);

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

    PROF_LAP(t, P_MAP);

    // Barrier, all threads need the mapping done

    // 5. Apply the mapping to every pixel - Fully parallel
    for (int i = 0; i < total_pixels; i++) {
        output->pixels[i] = mapping[input.pixels[i]];
    }

    PROF_LAP(t, P_APPLY);

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

    FILE *file = fopen(output_name, "wb");
    if (!file) {
        perror("Could not create output file");
        free(output_name);
        return 0;
    }

    /* P5: binary header followed by one byte per pixel, written in one call. */
    fprintf(file, "P5\n%d %d\n%d\n",
            image->width, image->height, image->max_value);

    size_t count = (size_t)image->width * image->height;

    if (fwrite(image->pixels, sizeof(uint8_t), count, file) != count)
        fprintf(stderr, "Short write of pixel data.\n");

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

    PROF_DECL(t);

    if (!read_pgm(argv[1], &input)) {
        return 1;
    }

    PROF_LAP(t, P_READ);

    printf("Read succesfully\n");

    histogram_equalization(input, &output, input.width, input.height);

    PROF_RESET(t);

    if(!save_pgm(argv[1], &output)) {
        printf("Failed to save image to file\n");
        free(input.pixels);
        free(output.pixels);
        return 1;
    }

    PROF_LAP(t, P_WRITE);

#ifdef PROFILE
    /* One machine-readable line (ms) for the benchmark script. */
    fprintf(stderr, "PROFILE %d %d", input.width, input.height);
    for (int i = 0; i < P_COUNT; i++)
        fprintf(stderr, " %.6f", prof_ms[i]);
    fputc('\n', stderr);
#endif

    return 0;
}