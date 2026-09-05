# Native regression tests

Run from the repository root:

```sh
make -C tests/native check
make -C tests/native sanitize
pio test -e native
```

Make needs a C++11 compiler and a POSIX host (macOS/Linux). PlatformIO uses
GoogleTest and downloads its native dependencies. Both runners execute the same
cases in isolated child processes so crashes, assertions, and timeouts fail a case
without stopping the remaining tests. All current cases belong to the normal
suite; the older `--regressions` option is retained for tooling compatibility.

ASan (AddressSanitizer) detects memory errors such as out-of-bounds access and
use-after-free. UBSan (UndefinedBehaviorSanitizer) detects undefined C++ operations.
The sanitizer target enables both. Passing only covers executed paths; it does not
prove absence of all memory errors. Child-process isolation limits exit-time leak
checking, so these runs are not a complete leak audit.

Coverage includes BER length/sign boundaries, Counter64, full-range OID arcs,
truncated packets and malformed response trees, binary/community handling,
per-binding exceptions, string termination, float scaling, callback ownership,
allocation failures, request correlation, continued polling after lost replies,
and receive cleanup without UDP transmissions. Fixtures encode expected wire bytes
independently of the library serializer. [RFC notes](RFC_NOTES.md) describe the
protocol rationale; this is not a complete conformance certification.

`check` also compiles a C++11 executable from multiple translation units. It uses
the original 1.x member signatures, numeric versions, short IDs/ports, `setIP()`,
capacity-free text callbacks, custom pointer-only BER overrides, and sketch-local
configuration/debug defines. Runtime cases verify independent ownership on copies
and portable integer destinations. Both safe bounded methods and legacy forms are
covered; a legacy caller still owns responsibility for sufficient buffer storage.

The stub consumes received bytes and models ESP8266's transmit-on-flush behavior.
It injects bind/send/read/allocation failures, but cannot establish actual Wi-Fi
performance, fragmentation behavior, or hardware interoperability.

For real core compilation, run `pio run -d tests/embedded` and
`pio run -d tests/examples`. The latter builds the maintained 1.x examples.
Those are compile checks only; neither command uploads firmware.

The suite also exercises each example’s local `Polling.h` against mocked UDP
responses: fresh and unchanged values, incomplete samples, late replies, timeout
wraparound, and Counter32 rate calculation boundaries.

## Markdown checks

Run `npx --yes markdownlint-cli@0.49.1 $(git ls-files '*.md')` from the repository
root. CI checks every tracked Markdown file using `.markdownlint.json`.
The standard rules apply except the line-length rule: prose and URLs may remain
on long source lines. Headings, lists, fenced code blocks, and whitespace are
checked consistently.
