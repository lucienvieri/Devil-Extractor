# Devil-Extractor v2.4 — Extraction Issues Log

*Compiled 2026-08-12 during venue search round 3*

## Summary

- Total URLs fetched: 230
- Successfully extracted: 201
- Failed to fetch (HTTP/0 bytes): 28 (Yelp, US News, theDiveBarTourist, etc.)
- Devil-extractor produced empty output: 3

## Devil-Extractor Empty Extractions

The extractor returned ~14 bytes of text despite receiving valid HTML. These are all JS-heavy/SPA sites where devil-extractor couldn't extract static content:

1. **bar.app** — URL: https://bar.app/ (index 10)
   - HTML: 6,560 bytes, extracted text: 14 bytes
   - Likely a JS-rendered single-page app

2. **hitthetown.app/denver/dive-bars** (index 55)
   - HTML: 30,134 bytes, extracted text: 14 bytes
   - React SPA, content loaded via JavaScript

3. **denvergov.org/Home** (index 128)
   - HTML: 196,735 bytes, extracted text: 14 bytes
   - Large government site, likely JS-driven navigation

## Fetch Failures (Not Devil Issues)

These URLs failed during the curl fetch step (0 bytes received), so devil-extractor never ran:
- Yelp search pages (7 URLs, JS-heavy)
- travel.usnews.com
- theDiveBarTourist.com
- Various JS-rendered sites

## Notes for Future Rounds

- Devil-extractor v2.4 handles static HTML well but struggles with SPAs
- Sites returning 14 bytes of text are the ones that need headless browser extraction or `devil-v2-cli` flags for JS rendering
- Consider adding a retry or flag check for known SPA sites
- The `-j -a` flags were used during extraction
