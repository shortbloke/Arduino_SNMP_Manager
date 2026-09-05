# Maintaining and releasing SNMP Manager

Maintainer reference: these workflows publish the library. Users do not need them
to read device data; follow [getting started](GETTING_STARTED.md) instead.

## Branches

- `main`: 2.x development and releases; this is the default branch.
- `release/1.x`: backward-compatible 1.x fixes and releases.
- Historical branches are preserved as `archive/*` tags; use `release/1.x` for new maintenance work.

Fix defects on the relevant line and port them to the other line when applicable.
The implementation and public APIs differ; do not merge the entire 2.x tree into
1.x. Keep release automation consistent on both active branches. Tags are immutable.

## Prepare a release

1. Add user-facing notes under `## Unreleased` in `CHANGELOG.md` on the target line.
2. In GitHub Actions, select **Prepare release PR**, choose `main` or `release/1.x`,
   and enter the complete version, such as `2.0.0-rc.1` or `1.2.2`.
3. The workflow updates both library manifests, current-version README guidance,
   and the changelog heading together, then opens a PR and explicitly dispatches CI.
4. Review the diff and that PR branch's CI run, then merge the release PR.

The repository must allow GitHub Actions to create pull requests (Settings >
Actions > General > Workflow permissions). No personal access token is required.
PRs created with `GITHUB_TOKEN` do not automatically run ordinary push/PR checks;
the prepare workflow explicitly starts the compatibility workflow on their branch.

For local preparation, run:

```sh
python3 scripts/release.py prepare --version 2.0.0-alpha.2 --branch main
python3 scripts/release.py check --version 2.0.0-alpha.2 --branch main
```

The example assumes you checked out `main` and wrote an Unreleased section.
For a 1.x patch, use `release/1.x` and an appropriate 1.x version.
Maintainers still author release notes: version and metadata edits are automated.

## Existing 2.x draft

`2.0.0-alpha.1` is an unpublished GitHub draft. A draft is not an installed
Library Manager release or a public version tag. The publication workflow creates
a new release; it does not publish an existing draft. Do not run it for a version
that already has a draft. After review, either complete that draft manually against
a validated immutable tag, or remove the draft before using the workflow. Review
and update any draft-status wording in the notes before publication.

## Publish a prepared release

Run **Publish release** on the same branch with its prepared version. The workflow
runs the full compatibility workflow for that exact commit, verifies manifests,
branch/version compatibility and changelog notes, then creates an annotated tag
and GitHub release. It refuses an existing tag; it never rewrites a published release.
If publication fails after tagging, inspect the tag and complete the GitHub release
manually from that verified tag instead of moving it.

Prereleases (`-alpha.N`, `-beta.N`, `-rc.N`) are marked as prereleases. A 1.x release
does not replace a stable 2.x release as GitHub's latest release. Publishing 2.x
requires an intentional workflow run; merging to `main` does not release it.

Arduino Library Manager indexes eligible tagged releases from this registered
repository; allow time for indexing. PlatformIO Registry is a separate publishing
service: these workflows do not claim to publish there. Its credentials and
registry publication must be configured separately if desired.

## CI coverage

Both active lines run native tests, sanitizer checks, embedded builds, Markdown
checks and release-tool tests. The 2.x line additionally exercises its modular
headers, memory configurations, lifecycle/leak checks, hardware-log parsers,
example builds, and Net-SNMP wire interoperability. Hardware compilation does not
flash a board or certify physical interoperability.

### Linting locally

CI checks workflows and their embedded shell with actionlint and ShellCheck,
Python with Ruff, and Arduino packaging with Arduino Lint. These checks also gate
release publication. Markdown and C++ formatting checks remain in the compatibility
workflow.

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements-lint.txt
python scripts/install_lint_tools.py /tmp/snmp-lint-tools
export PATH="/tmp/snmp-lint-tools:$PATH"
actionlint -shellcheck shellcheck
ruff check scripts tests
ruff format --check scripts tests
python scripts/lint_arduino.py
```

The binary installer supports Linux x86-64 and Apple Silicon macOS. Other platforms
can install the versions recorded in `scripts/lint-tools.json` manually. Downloads
are SHA-256 verified; update the version, URLs and checksums together when upgrading.
Dependabot proposes Ruff and GitHub Actions updates on both maintained branches.

Arduino Lint uses specification compliance and the existing Library Manager entry.
It checks a temporary copy of tracked working-tree files to exclude local build
outputs and private hardware configuration. Add new package files to Git before
running it. This checks packaging; the embedded builds check compilation.
