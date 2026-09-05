#!/usr/bin/env python3
"""Validate and prepare releases without third-party Python dependencies."""

import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION = re.compile(
    r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-((?:alpha|beta|rc)\.[1-9][0-9]*))?"
)


def parts(version):
    match = VERSION.fullmatch(version)
    if not match:
        raise ValueError("Use X.Y.Z or X.Y.Z-alpha.N/-beta.N/-rc.N")
    base = tuple(map(int, match.group(1, 2, 3)))
    suffix = match.group(4)
    return base + (
        (3, 0)
        if not suffix
        else ({"alpha": 0, "beta": 1, "rc": 2}[suffix.split(".")[0]], int(suffix.split(".")[1]))
    )


def current(root):
    properties = (root / "library.properties").read_text()
    versions = re.findall(r"^version=(.+)$", properties, re.M)
    if len(versions) != 1:
        raise ValueError("library.properties must contain exactly one version")
    version = versions[0]
    parts(version)
    if json.loads((root / "library.json").read_text())["version"] != version:
        raise ValueError("Library manifests disagree")
    return version


def check_branch(branch, version):
    # master may advance to 2.x; maintenance lines never change major version.
    expected = {"main": 2, "release/1.x": 1, "release/2.x": 2, "feature/friendly-query-api": 2}
    if branch not in {"master", *expected}:
        raise ValueError(
            "Release only from main, master, release/1.x, release/2.x or feature/friendly-query-api"
        )
    if parts(version)[0] not in (1, 2) or (
        branch in expected and parts(version)[0] != expected[branch]
    ):
        raise ValueError("Version does not match the release line")


def notes(root, version):
    text = (root / "CHANGELOG.md").read_text()
    match = re.search(r"^## " + re.escape(version) + r"\s*\n(.*?)(?=^## |\Z)", text, re.M | re.S)
    if not match or not match.group(1).strip():
        raise ValueError("Missing nonempty changelog section for " + version)
    return match.group(1).strip() + "\n"


def prepare(root, version, branch):
    old = current(root)
    check_branch(branch, version)
    if parts(version) < parts(old):
        raise ValueError("Cannot decrease the version")
    changelog = (root / "CHANGELOG.md").read_text()
    match = re.search(r"^## Unreleased\s*\n(.*?)(?=^## |\Z)", changelog, re.M | re.S)
    if not match or not match.group(1).strip():
        raise ValueError("Write release notes under ## Unreleased first")
    if re.search(r"^## " + re.escape(version) + r"\s*$", changelog, re.M):
        raise ValueError("Version already appears in the changelog")
    # Validate everything before mutating files.
    changelog = changelog[: match.start()] + changelog[match.start() :].replace(
        "## Unreleased", "## " + version, 1
    )
    manifest = json.loads((root / "library.json").read_text())
    manifest["version"] = version
    (root / "library.json").write_text(json.dumps(manifest, indent=2) + "\n")
    properties = (root / "library.properties").read_text()
    (root / "library.properties").write_text(
        re.sub(r"^version=.*$", "version=" + version, properties, flags=re.M)
    )
    # Update current-version guidance, without rewriting historical changelog entries.
    readme = (root / "README.md").read_text()
    (root / "README.md").write_text(
        re.sub(r"(?<![0-9.])" + re.escape(old) + r"(?![0-9.])", version, readme)
    )
    (root / "CHANGELOG.md").write_text(changelog)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["check", "prepare", "notes"])
    parser.add_argument("--version")
    parser.add_argument("--branch")
    parser.add_argument("--allow-unreleased", action="store_true")
    args = parser.parse_args()
    version = args.version or current(ROOT)
    if args.command == "prepare":
        if not args.branch:
            parser.error("prepare requires --branch")
        prepare(ROOT, version, args.branch)
    elif args.command == "notes":
        print(notes(ROOT, version), end="")
    else:
        if current(ROOT) != version:
            raise ValueError("Requested version differs from manifests")
        if args.branch:
            check_branch(args.branch, version)
        notes(
            ROOT,
            "Unreleased"
            if args.allow_unreleased and "## Unreleased" in (ROOT / "CHANGELOG.md").read_text()
            else version,
        )
        print("Release metadata verified: " + version)


if __name__ == "__main__":
    main()
