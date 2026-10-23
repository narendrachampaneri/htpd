"""Tests of the htpd skill package: the zip an AI app installs, run as the app would."""

from __future__ import annotations

import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SINGLE = ROOT / "examples" / "single-file" / "sample.htpd"

spec = importlib.util.spec_from_file_location("build_skill", ROOT / "scripts" / "build_skill.py")
build_skill = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build_skill)


class SkillPackage(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.t = Path(self._tmp.name)
        self.zip = build_skill.build(self.t / "htpd-skill.zip")

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def test_one_folder_at_the_root(self) -> None:
        names = zipfile.ZipFile(self.zip).namelist()
        self.assertEqual({n.split("/")[0] for n in names}, {"htpd"})
        self.assertIn("htpd/SKILL.md", names)
        self.assertIn("htpd/LICENSE", names)
        self.assertIn("htpd/scripts/htpd.py", names)
        self.assertIn("htpd/scripts/htpd_reader/miniyaml.py", names)
        self.assertFalse([n for n in names if "__pycache__" in n or "/tests/" in n])

    def test_skill_rules(self) -> None:
        f = build_skill.check_skill((ROOT / "skills" / "htpd" / "SKILL.md").read_text(encoding="utf-8"))
        self.assertEqual(f["name"], "htpd")
        self.assertLessEqual(len(f["description"]), 200)
        with self.assertRaises(SystemExit):
            build_skill.check_skill("---\nname: htpd\ndescription: " + "x" * 201 + "\n---\n")
        with self.assertRaises(SystemExit):
            build_skill.check_skill("---\nname: HTPD Skill\ndescription: ok\n---\n")

    def test_the_same_bytes_every_time(self) -> None:
        again = build_skill.build(self.t / "again.zip")
        self.assertEqual(self.zip.read_bytes(), again.read_bytes())

    def run_skill(self, *args: str) -> subprocess.CompletedProcess:
        """As an AI app's sandbox runs it: unpacked elsewhere, plain Python, no PyYAML."""
        where = self.t / "installed"
        zipfile.ZipFile(self.zip).extractall(where)
        env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HTPD_NO_YAML": "1"}
        return subprocess.run([sys.executable, "-I", str(where / "htpd" / "scripts" / "htpd.py"), *args],
                              capture_output=True, text=True, env=env, cwd=self.t, timeout=60)

    def test_answers_from_the_file(self) -> None:
        p = self.run_skill("ask", str(SINGLE), "What is the reset value of the READY bit?", "--context")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn("[page 5, A.1 Status register]", p.stdout)
        self.assertIn("0 | READY | R | 0", p.stdout)
        self.assertIn("not instructions", p.stdout)   # the context says so

    def test_not_found(self) -> None:
        p = self.run_skill("ask", str(SINGLE), "Which HDMI version is supported?")
        self.assertEqual((p.returncode, p.stdout.strip()), (1, "Not found in this file. (the file never mentions HDMI)"))

    def test_other_commands(self) -> None:
        self.assertIn("3.1 Thermal derating", self.run_skill("contents", str(SINGLE)).stdout)
        self.assertIn("If the temperature exceeds 85", self.run_skill("section", str(SINGLE), "S11").stdout)
        self.assertEqual(self.run_skill("check", str(SINGLE)).returncode, 0)
        renamed = self.t / "sample.htpd.txt"     # a chat app may only take .txt
        renamed.write_bytes(SINGLE.read_bytes())
        self.assertEqual(self.run_skill("ask", str(renamed), "READY reset").returncode, 0)


if __name__ == "__main__":
    unittest.main()


class SkillMcp(unittest.TestCase):
    """`htpd.py mcp` runs the MCP server: how the Claude Code plugin starts it."""

    def test_mcp_server_from_the_skill(self) -> None:
        msgs = [
            {"jsonrpc": "2.0", "id": 1, "method": "initialize",
             "params": {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "t", "version": "1"}}},
            {"jsonrpc": "2.0", "method": "notifications/initialized"},
            {"jsonrpc": "2.0", "id": 2, "method": "tools/call",
             "params": {"name": "ask", "arguments": {"bundle": str(SINGLE), "question": "What is the maximum bus voltage?"}}},
        ]
        env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HTPD_NO_YAML": "1"}
        r = subprocess.run([sys.executable, "-I", str(ROOT / "skills" / "htpd" / "scripts" / "htpd.py"), "mcp"],
                           input="".join(json.dumps(m) + "\n" for m in msgs), capture_output=True, text=True,
                           env=env, cwd=ROOT, timeout=60)
        replies = {d["id"]: d for d in map(json.loads, r.stdout.splitlines())}
        self.assertEqual(replies[1]["result"]["serverInfo"]["name"], "htpd")
        text = replies[2]["result"]["content"][0]["text"]
        self.assertIn("page", text)


class Plugin(unittest.TestCase):
    """The Claude Code plugin: the repository is the marketplace and the plugin."""

    def test_manifests(self) -> None:
        plugin = json.loads((ROOT / ".claude-plugin" / "plugin.json").read_text(encoding="utf-8"))
        market = json.loads((ROOT / ".claude-plugin" / "marketplace.json").read_text(encoding="utf-8"))
        self.assertEqual(plugin["name"], "htpd")
        self.assertEqual([p["name"] for p in market["plugins"]], ["htpd"])
        self.assertEqual(market["plugins"][0]["source"], "./")
        args = plugin["mcpServers"]["htpd"]["args"]
        self.assertTrue((ROOT / args[0].replace("${CLAUDE_PLUGIN_ROOT}/", "")).is_file())
        # the version of the plugin, the pip package and the reader agree
        from htpd_reader import __version__
        self.assertEqual(plugin["version"], __version__)
        self.assertIn(f'version = "{__version__}"', (ROOT / "pyproject.toml").read_text(encoding="utf-8"))
        # no .mcp.json at the root: it would also load as a project server in this repository
        self.assertFalse((ROOT / ".mcp.json").exists())

    def test_skills(self) -> None:
        for name in ("htpd", "convert"):
            f = build_skill.front_matter((ROOT / "skills" / name / "SKILL.md").read_text(encoding="utf-8"))
            self.assertEqual(f["name"], name)
            self.assertLessEqual(len(f["description"]), 200)

    def test_convert_script_names_the_release(self) -> None:
        script = (ROOT / "skills" / "convert" / "scripts" / "convert.sh").read_text(encoding="utf-8")
        from htpd_reader import __version__
        # the release of the converter: the version without a ".postN" of the Python package
        self.assertIn(f'VERSION="{__version__.split(".post")[0]}"', script)
        self.assertRegex(script, r'SHA256="[0-9a-f]{64}"')

    def test_mcp_registry_entry(self) -> None:
        """server.json lists the PyPI package; its README carries the name the registry checks."""
        from htpd_reader import __version__
        server = json.loads((ROOT / "server.json").read_text(encoding="utf-8"))
        self.assertEqual(server["version"], __version__)
        self.assertEqual(server["packages"][0]["identifier"], "htpd-reader")
        self.assertEqual(server["packages"][0]["version"], __version__)
        self.assertLessEqual(len(server["description"]), 100)
        self.assertIn(f"<!-- mcp-name: {server['name']} -->", (ROOT / "README.md").read_text(encoding="utf-8"))
        self.assertIn('htpd-reader = "htpd_reader.mcp:main"', (ROOT / "pyproject.toml").read_text(encoding="utf-8"))

