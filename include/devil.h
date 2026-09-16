// Public API
// See docs/devil-api.md for full documentation.

#ifndef DEVIL_H
#define DEVIL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct devil_doc devil_doc_t;

devil_doc_t *devil_parse(const uint8_t *html, size_t len);
char *devil_text(devil_doc_t *doc);
char *devil_strip(devil_doc_t *doc);
char *devil_extract(devil_doc_t *doc, bool noise, bool adblock, bool js);
void devil_free_string(char *str);
void devil_free(devil_doc_t *doc);

/* JSON-LD extraction from raw HTML */
char *json_ld_extract(const char *html, size_t html_len);

#ifdef __cplusplus
}
#endif

#endif /* DEVIL_H */
