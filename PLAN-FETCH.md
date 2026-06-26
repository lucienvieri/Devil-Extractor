# Plan: devil-fetch — Standalone URL Fetcher + Extractor

## Objective

Create a standalone `devil-fetch` binary that combines URL fetching and HTML-to-text extraction in a single process. Eliminates fork/exec + pipe overhead for faster single-URL and batch parallel processing.

## Architecture

```
devil-extractor-v2/
├── src/
│   ├── devil.c              ← unchanged core parser
│   ├── devil.h              ← unchanged public API
│   ├── devil-fetch.c        ← standalone fetcher + extractor (main entry)
│   ├── devil-extract.c      ← standalone extractor (libdevil-v2 only)
│   ├── main.c               ← existing CLI (unchanged)
│   └── filters/             ← existing filter plugins
├── CMakeLists.txt           ← add two new binaries
├── PLAN-FETCH.md            ← this file
└── test-sites/              ← existing test pages
```

## Two Binaries

### `devil-extract` (renamed from devil-v2-cli)
- Same as current `devil-v2-cli`
- Reads HTML from stdin or files
- Outputs plain text
- Backwards compatible: `curl | devil-extract`

### `devil-fetch` (new standalone binary)
- Fetches a URL and extracts text in one process
- No fork, no pipe, no shared memory copies
- Supports single URL and batch parallel modes

## API (devil-fetch.c)

### Single URL
```c
// Fetch a URL and extract text. Single process, no fork.
// Returns malloc'd text, NULL on failure.
// Status code set via pointer (0 on fetch failure).
char *devil_fetch_extract(const char *url, long *status_code);
```

Usage:
```bash
devil-fetch https://example.com
# outputs extracted text to stdout

devil-fetch https://example.com | devil-extract
# can still pipe (e.g. to jq, grep, etc.)
```

### Batch Mode
```c
// Fetch N URLs concurrently using threading.
// results[i] corresponds to urls[i].
// Caller owns all result buffers.
// Returns count of successful fetches.
int devil_fetch_batch(const char **urls, int n,
                      char ***out_texts, long **out_status,
                      char ***out_errors);
```

Usage:
```bash
devil-fetch --batch url1 url2 url3
# outputs extracted text for each, separated by ---
# failed URLs get error messages on stderr
```

## Implementation Details

### Single URL Flow (devil-fetch.c)
```
main()
  → if single URL arg:
      text = devil_fetch_extract(url, &status)
      if text: printf("%s\n", text)
      else:     error(url, status, error_msg)
      free(text, error_msg)
  → if --batch mode:
      devil_fetch_batch(urls, n, &texts, &statuses, &errors)
      for each result:
          if text: output text (--- separator)
          else:    stderr error
      free all results
  → else:
      fallback to existing stdin/file modes
```

### devil_fetch_extract(url, status_code)
```
1. easy = curl_easy_init()
2. curl_easy_setopt(easy, CURLOPT_URL, url)
3. curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, html_cb)
4. curl_easy_setopt(easy, CURLOPT_WRITEDATA, &buf)
5. curl_easy_setopt(easy, CURLOPT_WRITEHEADER, &status)
6. curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L)
7. curl_easy_setopt(easy, CURLOPT_MAXREDIRS, 10L)
8. curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, 30000)
9. curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, 5000)
10. curl_easy_setopt(easy, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS)
11. curl_easy_setopt(easy, CURLOPT_USERAGENT, "devil/2.0 (searchngx)")
12. curl_easy_perform(easy)
13. curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, status)
14. if error: free buf, return NULL with error message
15. doc = devil_parse(buf.data, buf.size)
16. if !doc: free buf, return NULL
17. text = devil_strip(doc)
18. devil_free(doc)
19. free buf
20. return text
```

### Batch Mode — Threading
```
devil_fetch_batch(urls, n, &texts, &statuses, &errors)
1. Create thread pool: 4 workers
   (min(n, 4) to avoid spawning more threads than needed)
2. Create task queue with n tasks: {url, index}
3. Create result array: {text, status, error, done, index}
4. For each worker thread:
     while task = pop(queue):
         result = devil_fetch_extract(task->url, &status)
         result_array[task->index].text = result
         result_array[task->index].status = status
         result_array[task->index].done = 1
5. Wait for all threads to finish
6. Copy results to output arrays (maintaining input order)
7. free task queue, threads
```

Thread pool: 4 workers, uses a mutex + condition variable queue.
Simple, portable, same throughput as curl_multi for this workload.

### Key Design Decisions

1. **curl_easy (single handle)** for single URL — no overhead
2. **Threading** for batch (not curl_multi) — simpler code, same performance, pthreads already available on Linux
3. **curl built-in decompression** — CURLOPT_HTTP_CONTENT_DECODING handles gzip/br/zstd
4. **No new dependencies** — only libcurl (already on system via dev package)
5. **Standalone** — devil-fetch has its own main(), no dependency on devil-extract binary
6. **Connection reuse** — for URLs to the same host, each thread's curl handle reuses connections via curl's internal cache

## Performance Expectations

| Scenario | curl + pipe | devil-fetch | Improvement |
|----------|-------------|-------------|-------------|
| Single URL | 3-5ms fork + 10ms pipe + parse | 3ms process | 2-3x start-to-output |
| Batch 10 URLs | ~10× sequential (xargs) | ~1× parallel (4 threads) | 4-8x total throughput |
| Memory | 2 processes × buffer | 1 process × buffer | 2x less RSS |
| CPU | 2 processes running | 1 process + thread pool | Lower context switches |

The real win is batch mode: 4 concurrent fetches vs xargs -P which still forks 4 processes.

## Testing Strategy

1. **Regression**: existing test sites still work with `devil-extract` (unchanged)
2. **Functional**: fetch known URLs, verify text output matches curl + pipe
3. **Batch**: fetch 5+ URLs, verify all succeed, verify output order matches input order
4. **Error handling**: broken URLs, slow servers, redirects, non-HTML content
5. **Memory**: valgrind --leak-check=full on both single and batch modes
6. **Performance**: `time devil-fetch URL` vs `time curl -s URL | devil-extract`
   - Should show lower real time for devil-fetch (no fork)

## Open Questions

1. Should `devil-fetch` support stdin fallback? (yes, for parity)
2. What's the default thread count for batch? (4, or auto from nproc)
3. Should we support cookies/sessions for batch? (no, overkill)
4. Should `devil-fetch` have a JSON output mode? (maybe later, keep it simple)

## Files to Create/Modify

| File | Action | Description |
|------|--------|-------------|
| `src/devil-fetch.c` | CREATE | Standalone fetcher + extractor binary |
| `src/devil-extract.c` | CREATE | Standalone extractor (libdevil-v2 only) |
| `src/main.c` | MODIFY | Rename from devil-v2-cli, keep stdin/file modes |
| `CMakeLists.txt` | MODIFY | Add two new binaries, link libcurl |

## Implementation Order

1. `src/devil-extract.c` — simple wrapper, no new logic
2. `src/devil-fetch.c` — core fetch logic (curl + parse + extract)
3. CMakeLists.txt — add both binaries
4. Test: single URL fetch + extract
5. Add --batch mode with threading
6. Valgrind check
7. Performance comparison vs curl
