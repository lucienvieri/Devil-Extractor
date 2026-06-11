# Devil Extractor v2 — Architecture Plan

> Purpose: `searchngx | devil-v2 | chat_agent` — a pipe-stage in a RAG pipeline.
> Constraint: must be blazing-fast, zero-external-dependencies beyond lexbor, and output LLM-friendly text.
> Version: v2 — fork of devil-extractor, will diverge with new features/improvements.

## File Layout

```
devil-extractor/
├── CMakeLists.txt      # build script (libdevil + devil-cli)
├── include/
│   └── devil.h         # public API header
├── src/
│   ├── devil.c         # core: parser wrapper, DFS walk, noise filter, text assembly
│   └── main.c          # CLI: stdin or file args → stdout
```

## 1. Public API (`devil.h`)

### `devil_parse(html, len)` → `devil_doc_t*`
- Wraps lexbor's `lxb_html_parser_create` → `lxb_html_parser_init` → `lxb_html_parse`
- Stores the parsed `lxb_html_document_t*`, the arena (`lexbor_mraw_t*`), and the parser in a `devil_doc_t` struct
- Returns NULL on parse failure

### `devil_text(doc)` → `char*`
- Walks the DOM tree
- Extracts text content, injecting newlines at block-level boundaries
- Returns a malloc'd string (caller frees with `devil_free_string()`)

### `devil_strip(doc)` → `char*`
- Same as `devil_text` but skips noise tags entirely during walk
- Noise tags: script, style, nav, aside, footer, header, noscript, iframe, form, select, head, meta, link

### `devil_free_string(str)` → `void`
- Frees a string returned by `devil_text` or `devil_strip`

### `devil_free(doc)` → `void`
- Destroys the parser, document, and arena in the correct order
- Must be called after every `devil_parse` call

## 2. Core Engine (`devil.c`)

### Data Structure
```c
typedef struct {
    lexbor_html_document_t *doc;   // parsed DOM
    lexbor_mraw_t          *mraw;  // memory arena (v3.x requirement: init before use)
    lexbor_html_parser_t   *parser; // parser instance
} devil_doc_t;
```

### Noise Tag Set
- Array-based linear search (small set, fast enough)
- Uses `LXB_TAG_*` constants from `<lexbor/tag/tag.h>`
- Noise tags: SCRIPT, STYLE, NAV, ASIDE, FOOTER, HEADER, NOSCRIPT, IFRAME, FORM, SELECT, HEAD, META, LINK

### Block-Level Tag Set
- Tags that require a newline AFTER their content: DIV, P, H1-H6, LI, ARTICLE, BR, SECTION, UL, OL, DL, DD, DT, HR, BLOCKQUOTE, PRE, FIGURE, FIGCAPTION, CAPTION, TR, THEAD, TBODY, TFOOT, ABBR, ADDRESS, MAIN, SUMMARY, DETAILS
- These tags signal a "paragraph boundary" to prevent tokenizer poisoning

### DFS Walk Algorithm
```
extract(node, output_str):
    if node is noise tag:
        return 0  // skip entire subtree

    if node is text node:
        trim trailing whitespace from output
        append text_content to output
        set needs_newline = false
        return 0

    // element node, not noise
    for each child of node:
        extract(child, output)

    // after processing children, check if we need a block boundary
    if node is block-level tag AND output is not empty:
        append '\n' to output
```

### Text Trimming Strategy
- Between block boundaries: strip leading/trailing whitespace, collapse multiple spaces to single space
- Between inline elements within a block: preserve single space separator
- Between block-level elements: always inject exactly one newline
- This prevents "Hello World" becoming "HelloWorld" or "Hello   World"

### Memory Management (v3.x)
Critical: lexbor v3.x requires `lexbor_mraw_init(mraw, chunk_size)` after `lexbor_mraw_create()`, otherwise segfault on any string/parser operation.
```
devil_parse(html, len):
    mraw = lexbor_mraw_create()
    lexbor_mraw_init(mraw, 4096)           // mandatory for v3.x
    parser = lexbor_html_parser_create()
    lexbor_html_parser_init(parser)
    lexbor_html_parser_dom_opt_set(parser, DOM_OPT_DEFAULTS)
    doc = lexbor_html_parse(parser, html, len)
    if doc == NULL:
        lexbor_html_parser_destroy(parser)
        lexbor_mraw_destroy(mraw, true)
        return NULL
    result = malloc devil_doc_t
    result->doc = doc
    result->mraw = mraw
    result->parser = parser
    return result

devil_free(doc):
    lexbor_html_parser_destroy(doc->parser)
    lexbor_html_document_destroy(doc->doc)
    lexbor_mraw_destroy(doc->mraw, true)
    free(doc)
```

## 3. CLI (`main.c`)

### Input Modes
1. **stdin** (default): `cat page.html | devil` — reads until EOF
2. **File arguments**: `devil file1.html file2.html` — processes each file, outputs with `---` separator between files

### File Reading (Safe)
```
read_file(path):
    f = fopen(path, "rb")
    if f == NULL: print error, continue to next file
    fseek(f, 0, SEEK_END)
    size = ftell(f)
    fseek(f, 0, SEEK_SET)
    buf = malloc(size + 1)
    n = fread(buf, 1, size, f)
    fclose(f)  // close immediately
    buf[n] = 0
    return buf
```

### Pipeline Flow
```
for each input source:
    read all bytes into buffer
    doc = devil_parse(buffer, len)
    if doc == NULL: print "parse error" to stderr, continue
    text = devil_strip(doc)
    printf("%s", text)
    devil_free_string(text)
    devil_free(doc)
    free(buffer)
```

### Error Handling
- Parse errors: print to stderr, continue processing remaining input (don't crash)
- Memory allocation failure: print to stderr, free any partial state, return 1
- Missing stdin input: print usage to stderr, return 1
- File read errors: print filename + error to stderr, continue to next file

## 4. Build System (`CMakeLists.txt`)

```cmake
cmake_minimum_required(VERSION 3.16)
project(devil C)

# Find lexbor
find_library(LEXBOR_LIBRARY lexbor PATHS /usr/local/lib)
find_path(LEXBOR_INCLUDE_DIR lexbor/html/parser.h
    /usr/local/include /usr/include)

# Library
add_library(devil STATIC src/devil.c)
target_include_directories(devil PRIVATE include ${LEXBOR_INCLUDE_DIR})
target_link_libraries(devil ${LEXBOR_LIBRARY})

# CLI binary
add_executable(devil-cli src/main.c)
target_link_libraries(devil-cli devil)

# Install
install(TARGETS devil devil-cli DESTINATION bin)
```

## 5. Error Cases to Handle

1. **Empty input** — output empty string, no crash
2. **Malformed HTML** — lexbor is forgiving, will still produce a partial DOM; extract what's possible
3. **Very large HTML** — stdin reads in chunks if needed; for now, read entire buffer (suitable for typical RAG pipeline chunks)
4. **Nested noise tags** — `<div><script><style>...</div>` — the noise filter is recursive, so even deeply nested noise tags are skipped

## 6. Verification Checklist

- [ ] Compiles with `gcc -Wall -Wextra -pedantic` with zero warnings
- [ ] Pipes HTML from stdin correctly
- [ ] Handles multiple files with separators
- [ ] Noise tags fully removed (no script/style content leaks)
- [ ] Block boundaries produce newlines (no concatenated text)
- [ ] Memory: `valgrind --leak-check=full` reports zero leaks
- [ ] `devil_free` called exactly once per `devil_parse`
- [ ] `lexbor_mraw_destroy` called with `true` to free arena allocator
