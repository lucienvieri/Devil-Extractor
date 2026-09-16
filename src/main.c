#include "devil.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* CLI flag defaults */
static bool flag_noise = true;
static bool flag_adblock = false;
static bool flag_js = false;
static bool flag_jsonld = true;
static bool flag_strip = false;

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
    size_t cap = 4096, n = 0;
    uint8_t *buf = malloc(cap);
    if (!buf) return NULL;

    size_t rd;
    while ((rd = fread(buf + n, 1, cap - n - 1, stdin)) > 0) {
        n += rd;
        if (n + 1 >= cap) {
            cap *= 2;
            uint8_t *tmp = realloc(buf, cap);
            if (!tmp) { free(buf); return NULL; }
            buf = tmp;
        }
    }

    buf[n] = 0;
    *out_len = n;
    return buf;
}

static void
print_usage(void)
{
    fprintf(stderr,
        "devil v2.4 — HTML-to-text extractor\n"
        "\n"
        "Usage: devil [OPTIONS] [FILE...]\n"
        "       cat page.html | devil [OPTIONS]\n"
        "\n"
        "Options:\n"
        "  -n, --noise         Enable noise tag filtering (script, style, nav…)\n"
        "                      Default: on\n"
        "  -N, --no-noise      Disable noise tag filtering\n"
        "  -a, --adblock       Enable CSS-class ad-block filter\n"
        "                      Default: off\n"
        "  -A, --no-adblock    Disable ad-block filter (explicit)\n"
        "  -j, --js            Extract text from <script>/<style> elements\n"
        "                      Useful for JS-heavy sites where content\n"
        "                      lives inside script blocks\n"
        "                      Default: off\n"
        "  -J, --no-js         Disable JS extraction (explicit)\n"
        "  --jsonld            Include JSON-LD structured data in output\n"
        "                      Default: on\n"
        "  --no-jsonld         Suppress JSON-LD output\n"
        "  --plain             Output raw text only (no === headers)\n"
        "  -h, --help          Show this help\n"
        "\n"
        "If no files are given, reads stdin.\n"
    );
}

static void
process_html(const uint8_t *html, size_t len)
{
    devil_doc_t *doc = devil_parse(html, len);
    if (!doc) {
        fprintf(stderr, "devil: failed to parse HTML\n");
        return;
    }

    char *text = devil_extract(doc, flag_noise, flag_adblock, flag_js);
    if (!text) {
        fprintf(stderr, "devil: failed to extract text\n");
        devil_free(doc);
        return;
    }

    if (flag_strip) {
        printf("%s\n", text);
    } else {
        printf("=== text ===\n%s\n", text);
    }
    devil_free_string(text);

    if (flag_jsonld) {
        char *jsonld = json_ld_extract((const char *)html, len);
        if (jsonld) {
            printf("\n=== json-ld ===\n%s\n", jsonld);
            free(jsonld);
        }
    }

    devil_free(doc);
}

static void
parse_args(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-n") == 0 || strcmp(a, "--noise") == 0) {
            flag_noise = true;
        } else if (strcmp(a, "-N") == 0 || strcmp(a, "--no-noise") == 0) {
            flag_noise = false;
        } else if (strcmp(a, "-a") == 0 || strcmp(a, "--adblock") == 0) {
            flag_adblock = true;
        } else if (strcmp(a, "-A") == 0 || strcmp(a, "--no-adblock") == 0) {
            flag_adblock = false;
        } else if (strcmp(a, "--jsonld") == 0) {
            flag_jsonld = true;
        } else if (strcmp(a, "--no-jsonld") == 0) {
            flag_jsonld = false;
        } else if (strcmp(a, "-j") == 0 || strcmp(a, "--js") == 0) {
            flag_js = true;
        } else if (strcmp(a, "-J") == 0 || strcmp(a, "--no-js") == 0) {
            flag_js = false;
        } else if (strcmp(a, "--plain") == 0) {
            flag_strip = true;
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            print_usage();
            exit(0);
        } else {
            /* Treat as filename (fall through to file processing) */
            continue;
        }
    }
}

static void
main_loop(int argc, char **argv)
{
    int file_count = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') continue;
        file_count++;
    }

    if (file_count == 0) {
        size_t len = 0;
        uint8_t *html = read_stdin(&len);
        if (!html) {
            fprintf(stderr, "devil: failed to read stdin\n");
            exit(1);
        }
        process_html(html, len);
        free(html);
    } else {
        int first = 1;
        for (int i = 1; i < argc; i++) {
            if (argv[i][0] == '-') continue;

            size_t len = 0;
            uint8_t *html = read_file(argv[i], &len);
            if (!html) continue;
            if (!first) printf("---\n");
            process_html(html, len);
            free(html);
            first = 0;
        }
    }
}

int
main(int argc, char **argv)
{
    parse_args(argc, argv);
    main_loop(argc, argv);
    return 0;
}
