# Reader

Reader is the built-in book reader in graphite shell. Supported formats are TXT, FB2, FB2 in ZIP (`fb2.zip`), and EPUB 2/3.

## EPUB and navigation

Reader parses OPF and the spine sequence. The table of contents comes from EPUB 3 nav or EPUB 2 NCX; nested entries and `#fragment` targets are supported. If a TOC is unavailable, Reader falls back to headings and spine order.

## Limitations

- DRM and encrypted books are not supported.
- CSS is not applied.
- Book images are not displayed.
- PDF is not supported.

These limitations describe current scope, not earlier project proposals. EPUB 2 and EPUB 3 were checked on the P705; the EPUB host test suite is reported as 8 passing tests.

## Next

Possible improvements, not commitments or part of the current scope, include in-book search, richer FB2 structure handling, and book illustrations. PDF would require a separate decision and is not part of Reader.
