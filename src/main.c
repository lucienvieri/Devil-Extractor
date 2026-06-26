#include "devil.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *
read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "devil: cannot open '%s'\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *buf = malloc(size + 1);
    if (!buf) {
        fclose(f);
        fprintf(stderr, "devil: out of memory\n");
        return NULL;
    }

    size_t n = fread(buf, 1, size, f);
    fclose(f);
    buf[n] = 0;
    *out_len = (size_t)n;
    return buf;
}

static uint8_t *
read_stdin(size_t *out_len)
{
    size_t cap = 4096;
    uint8_t *buf = malloc(cap);
    if (!buf) return NULL;

    size_t n = 0;
    int c;
    while ((c = fgetc(stdin)) != EOF) {
        if (n + 1 >= cap) {
            cap *= 2;
            uint8_t *tmp = realloc(buf, cap);
            if (!tmp) {
                free(buf);
                fprintf(stderr, "devil: out of memory\n");
                return NULL;
            }
            buf = tmp;
        }
        buf[n++] = (uint8_t)c;
    }

    buf[n] = 0;
    *out_len = n;
    return buf;
}

static void
process_html(const uint8_t *html, size_t len)
{
    devil_doc_t *doc = devil_parse(html, len);
    if (!doc) {
        fprintf(stderr, "devil: failed to parse HTML\n");
        return;
    }

    char *text = devil_strip(doc);
    if (!text) {
        fprintf(stderr, "devil: failed to extract text\n");
        devil_free(doc);
        return;
    }

    printf("%s", text);

    devil_free_string(text);
    devil_free(doc);
}

int
main(int argc, char **argv)
{
    if (argc <= 1) {
        size_t len = 0;
        uint8_t *html = read_stdin(&len);
        if (!html) {
            fprintf(stderr, "devil: failed to read stdin\n");
            return 1;
        }
        process_html(html, len);
        free(html);
    } else {
        int first = 1;
        for (int i = 1; i < argc; i++) {
            size_t len = 0;
            uint8_t *html = read_file(argv[i], &len);
            if (!html) continue;
            if (!first) printf("---\n");
            process_html(html, len);
            free(html);
            first = 0;
        }
    }

    return 0;
}
