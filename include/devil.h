// Devil Extractor v2 Public API
// See docs/devil-api.md for full documentation.
//
// A blazing-fast C library that ingests raw HTML and outputs clean,
// plain text formatted for LLM context windows.
// Pipeline: searchngx | devil | chat_agent

#ifndef DEVIL_H
#define DEVIL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct devil_doc devil_doc_t;

devil_doc_t *devil_parse(const uint8_t *html, size_t len);
char *devil_text(devil_doc_t *doc);
char *devil_strip(devil_doc_t *doc);
void devil_free_string(char *str);
void devil_free(devil_doc_t *doc);

#ifdef __cplusplus
}
#endif

#endif /* DEVIL_H */
