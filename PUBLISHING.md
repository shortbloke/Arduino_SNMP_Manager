# Publishing maintained releases

Keep the library name **SNMP Manager** on both branches: `main` releases 2.x;
`release/1.x` releases compatible 1.x updates. Keep `library.properties` and
`library.json` versions identical. Never move a release tag or reuse a version.

## One-time setup

Add the repository Actions secret `PLATFORMIO_AUTH_TOKEN` for a PlatformIO account
with permission to publish under `shortbloke`. Generate a personal authentication
token using `pio account token` after logging in. Store it directly in GitHub's
Actions secrets, not in source files or release notes.

## New releases

1. Add release notes under `## Unreleased` in `CHANGELOG.md` on the relevant branch.
2. Run **Prepare release PR** on that branch with the next version. Review and
   merge the resulting PR after its checks pass.
3. Run **Publish release** on the same branch with that version. After validation,
   it creates the immutable Git tag and GitHub release, then calls **Publish
   PlatformIO package** for that tag and verifies that exact registry version.
4. Confirm the version appears in Arduino Library Manager after refreshing its
   index. Arduino automatically indexes eligible tags from this registered
   repository; no separate branch registration or upload is needed.

The PlatformIO job verifies that the tag identifies the validated commit and
packs only the tagged checkout. It checks the Actions token and package publishing
permissions even when the version is already available. Its workflow artifact contains the package sent
to the registry. GitHub's latest release remains on 2.x when publishing 1.x fixes.
Update the version guidance on the other branch when a new supported patch ships.
Metadata descriptions change in library listings with future published releases;
editing a branch does not modify packages already published.

## Publish an existing tag or recover a failed PlatformIO publication

Run **Publish PlatformIO package** from an active branch, entering the existing
version without `v`, for example `1.2.1`. This publishes the original tagged
sources, not the current branch, and does not recreate the tag or GitHub release.
It also allows recovery if GitHub publication succeeded but PlatformIO failed.
An already available registry version is left unchanged. A processing delay can
cause the availability check to time out; inspect the registry before retrying.

Version **1.2.1** was backfilled to PlatformIO from its original tag on
2026-09-06. Both Arduino and PlatformIO now list 1.2.1 and 2.0.0. Do not retag
either release to include the new descriptions; those belong in the next
releases of each line.

## User installation

Users find both lines under **SNMP Manager** in Arduino Library Manager and
**shortbloke/SNMP Manager** in PlatformIO. Arduino users choose a version manually;
PlatformIO projects can retain a major version with `@^1.2.1` or `@^2.0.0` after
publication. Keep the README's version table and migration link easy to find.

See [Arduino indexing requirements](https://github.com/arduino/library-registry/blob/main/FAQ.md#updates)
and [PlatformIO publishing](https://docs.platformio.org/en/stable/core/userguide/pkg/cmd_publish.html).
