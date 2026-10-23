# Unicode 17 segmentation fixtures

These are unmodified official conformance files from
`https://www.unicode.org/Public/17.0.0/ucd/auxiliary/`:

- `GraphemeBreakTest.txt`: 766 cases,
  SHA-256 `e2d134d2c52919bace503ebb6a551c1855fe1a1faec18478c78fff254a1793ec`.
- `WordBreakTest.txt`: 1,944 cases,
  SHA-256 `1de23a75f37904abc7d206239ee8d34f8fdf0fb4ab32a7174dfbabbde25419b2`.

License: [Unicode-3.0](../../../../kits/document/LICENSE.Unicode).
`test_richtext` reads the files from the repository root; it performs no network
access. Regeneration of runtime properties is described by
`tools/gen_richtext_unicode.py --help` and the Document Kit references.
