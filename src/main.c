/**
 * main.c — Devil Extractor v2 CLI
 *
 * Command-line interface for the Devil Extractor HTML-to-text library.
 *
 * Pipeline: searchngx | devil-v2-cli | chat_agent
 *
 * Usage:
 *   # Read from stdin (default)
 *   echo '<p>Hello</p>' | devil-v2-cli
 *
 *   # Process single file
 *   devil-v2-cli page.html
 *
 *   # Process multiple files (separated by ---)
 *   devil-v2-cli page1.html page2.html
 *
 * Input modes:
 *   - No arguments: reads HTML from stdin until EOF
 *   - One or more file arguments: processes each file sequentially
 *     - Files are separated by "---" in the output
 *     - If a file fails to open, an error is printed to stderr
 *       and processing continues with the next file
 *
 * Output format:
 *   - Plain text, LLM-tokenizer-friendly
 *   - Block boundaries produce newlines
 *   - Noise tags (script, style, nav, etc.) are filtered
 *   - Multiple files are separated by "---"
 *
 * Error handling:
 *   - Parse failures → stderr message, continue to next input
 *   - Memory allocation failures → stderr message, exit 1
 *   - Missing stdin → stderr message, exit 1
 *   - File open failures → stderr message, skip file
 *
 * Dependencies:
 *   - libdevil-v2 (linked library)
 *   - libc (stdlib, stdio, string)
 *
 * Compilation:
 *   cmake -B build && cmake --build build
 */

#include "devil.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Read an entire file into a malloc'd buffer.
 *
 * Opens the file in binary mode, seeks to end to determine size,
 * reads all bytes into a buffer, then closes the file immediately.
 *
 * @param path   Filesystem path to read
 * @param out_len Pointer to store the number of bytes read
 * @return malloc'd buffer containing file contents, or NULL on error
 *
 * Memory management:
 *   - Caller owns the returned buffer (free with free())
 *   - File handle is always closed (even on error)
 *
 * Error cases:
 *   - File cannot be opened → prints error to stderr, returns NULL
 *   - Memory allocation fails → prints error to stderr, returns NULL
 */
static uint8_t *
read_file(const char *path, size_t *out_len)
{
    /* Open file in binary mode */
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "devil: cannot open '%s'\n", path);
        return NULL;
    }

    /* Determine file size via seek */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    /* Allocate buffer (size + 1 for NUL terminator) */
    uint8_t *buf = malloc(size + 1);
    if (!buf) {
        fclose(f);
        fprintf(stderr, "devil: out of memory\n");
        return NULL;
    }

    /* Read all bytes */
    size_t n = fread(buf, 1, size, f);
    fclose(f);  /* Close immediately — we have the data */
    buf[n] = 0;
    *out_len = (size_t)n;
    return buf;
}

/**
 * Read all data from stdin into a malloc'd buffer.
 *
 * Reads character-by-character, growing the buffer as needed.
 * Starts with 4096 bytes and doubles on each reallocation.
 *
 * @param out_len Pointer to store the number of bytes read
 * @return malloc'd buffer containing stdin contents, or NULL on error
 *
 * Memory management:
 *   - Caller owns the returned buffer (free with free())
 *
 * Error cases:
 *   - Initial allocation fails → returns NULL
 *   - Reallocation fails → frees partial buffer, returns NULL
 */
static uint8_t *
read_stdin(size_t *out_len)
{
    size_t cap = 4096;
    uint8_t *buf = malloc(cap);
    if (!buf) return NULL;

    size_t n = 0;
    int c;
    while ((c = fgetc(stdin)) != EOF) {
        /* Double capacity when full */
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

/**
 * Parse and extract text from HTML data.
 *
 * Wraps the full pipeline: parse → strip → output → cleanup.
 * Handles errors gracefully (prints to stderr, does not crash).
 *
 * @param html HTML data to process
 * @param len  Number of bytes in html
 *
 * Pipeline:
 *   1. devil_parse(html, len) → devil_doc_t*
 *   2. devil_strip(doc) → text string
 *   3. printf text to stdout
 *   4. devil_free_string(text)
 *   5. devil_free(doc)
 */
static void
process_html(const uint8_t *html, size_t len)
{
    /* Parse HTML into DOM */
    devil_doc_t *doc = devil_parse(html, len);
    if (!doc) {
        fprintf(stderr, "devil: failed to parse HTML\n");
        return;
    }

    /* Extract text (with noise filtering) */
    char *text = devil_strip(doc);
    if (!text) {
        fprintf(stderr, "devil: failed to extract text\n");
        devil_free(doc);
        return;
    }

    /* Output to stdout */
    printf("%s", text);

    /* Cleanup */
    devil_free_string(text);
    devil_free(doc);
}

/**
 * CLI entry point.
 *
 * Usage:
 *   devil-v2-cli                    # reads from stdin
 *   devil-v2-cli file.html          # processes single file
 *   devil-v2-cli file1.html file2   # processes multiple files (--- separated)
 *
 * Exit codes:
 *   0 — success
 *   1 — stdin read error (no arguments provided)
 */
int
main(int argc, char **argv)
{
    if (argc <= 1) {
        /* No arguments: read from stdin */
        size_t len = 0;
        uint8_t *html = read_stdin(&len);
        if (!html) {
            fprintf(stderr, "devil: failed to read stdin\n");
            return 1;
        }
        process_html(html, len);
        free(html);
    } else {
        /* Process each file argument, separated by "---" */
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
