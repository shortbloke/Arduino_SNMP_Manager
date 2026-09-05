import importlib.util
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "release", Path(__file__).resolve().parents[2] / "scripts/release.py"
)
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "library.properties").write_text("name=SNMP Manager\nversion=1.2.1\n")
        (self.root / "library.json").write_text('{"version":"1.2.1", "name":"SNMP Manager"}')
        (self.root / "README.md").write_text("Version 1.2.1; pin #v1.2.1 or @^1.2.1\n")
        (self.root / "CHANGELOG.md").write_text(
            "# Changes\n\n## Unreleased\n\n- A fix.\n\n## 1.2.1\n\n- Old fix.\n"
        )

    def test_prepare_updates_metadata_and_preserves_history(self):
        release.prepare(self.root, "1.2.2", "release/1.x")
        self.assertEqual(release.current(self.root), "1.2.2")
        self.assertEqual(release.notes(self.root, "1.2.2"), "- A fix.\n")
        self.assertIn("## 1.2.1", (self.root / "CHANGELOG.md").read_text())
        self.assertNotIn("1.2.1", (self.root / "README.md").read_text())

    def test_rejects_wrong_line_without_writes(self):
        before = {p.name: p.read_bytes() for p in self.root.iterdir()}
        with self.assertRaises(ValueError):
            release.prepare(self.root, "2.0.0", "release/1.x")
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.root.iterdir()})

    def test_rejects_downgrade_and_existing_release(self):
        for version in ["1.2.0", "1.2.1"]:
            with self.assertRaises(ValueError):
                release.prepare(self.root, version, "master")

    def test_rejects_invalid_versions_and_branches(self):
        for version in ["v1.2.2", "1.2", "01.2.3", "1.2.3;echo bad", "2.0.0-rc.0"]:
            with self.assertRaises(ValueError):
                release.parts(version)
        with self.assertRaises(ValueError):
            release.check_branch("untrusted", "1.2.2")

    def test_prerelease_order(self):
        versions = ["2.0.0-alpha.1", "2.0.0-beta.1", "2.0.0-rc.1", "2.0.0"]
        self.assertEqual(sorted(versions, key=release.parts), versions)
        release.check_branch("feature/friendly-query-api", "2.0.0-rc.1")

    def test_metadata_mismatch(self):
        (self.root / "library.json").write_text('{"version":"2.0.0"}')
        with self.assertRaises(ValueError):
            release.current(self.root)

    def test_missing_notes(self):
        (self.root / "CHANGELOG.md").write_text("# Changes\n\n## Unreleased\n")
        with self.assertRaises(ValueError):
            release.prepare(self.root, "1.2.2", "master")


if __name__ == "__main__":
    unittest.main()
