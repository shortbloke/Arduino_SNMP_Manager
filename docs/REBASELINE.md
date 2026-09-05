# 2.x baseline and maintenance policy

The 2.x `main` branch is based on the released 1.2.1 mainline plus shared release
automation. The original friendly-query commit history is preserved at
`archive/friendly-query-api-2026-09-05`. `release/1.x` retains the compatible 1.x API.
Other retired branches have matching `archive/*-2026-09-05` tags.

## Mainline improvements retained

- Null-input rejection in the public response parser, with recovery tests.
- Request-aware dispatch for repeated IP/OID registrations and `updateCount()`.
- Bounded strings, persistent OID lists, fresh-response polling, corrected counter
  arithmetic, and configuration hints in the low-level examples.
- Shared GitHub Actions, Markdown checks, and release metadata automation.
  The 2.x changelog records this major line; published 1.x history remains on
  `release/1.x`.

## Intentional 2.x differences

- Compiled implementation files and build-wide configuration instead of header-only
  linkage and sketch-local configuration.
- Typed version/port/request-ID APIs, fixed-width destinations, mandatory buffer
  capacities, and noncopyable owning objects.
- Strict bounded request tracking rather than the 1.x compatibility rolling window.
- Friendly queries, owned results, walks, tables, SET and notification handling.
- Modular tests, independent mock agents, low-heap tests, wire interoperability,
  and optional hardware harnesses.

These differences are not lost backports. See [migration](../MIGRATION.md) for
application changes and [release maintenance](RELEASING.md) for branch policy.
