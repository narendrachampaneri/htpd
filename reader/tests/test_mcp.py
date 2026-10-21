"""Tests of the MCP server over stdio."""

from __future__ import annotations

import io
import json
import subprocess
import sys
import unittest
from pathlib import Path

from htpd_reader.mcp import TOOLS, Server

ROOT = Path(__file__).resolve().parents[2]
EXAMPLES = ROOT / "examples"


def talk(server: Server, *messages) -> list[dict]:
    out = io.StringIO()
    server.serve(io.StringIO("".join((m if isinstance(m, str) else json.dumps(m)) + "\n" for m in messages)), out)
    return [json.loads(x) for x in out.getvalue().splitlines()]


def call(id_: int, name: str, **args) -> dict:
    return {"jsonrpc": "2.0", "id": id_, "method": "tools/call", "params": {"name": name, "arguments": args}}


class McpServer(unittest.TestCase):
    def setUp(self) -> None:
        self.s = Server([EXAMPLES])

    def test_handshake_and_tools(self) -> None:
        r = talk(self.s, {"jsonrpc": "2.0", "id": 1, "method": "initialize",
                          "params": {"protocolVersion": "2025-03-26", "capabilities": {}}},
                 {"jsonrpc": "2.0", "method": "notifications/initialized"},
                 {"jsonrpc": "2.0", "id": 2, "method": "tools/list"},
                 {"jsonrpc": "2.0", "id": 3, "method": "ping"})
        self.assertEqual([m["id"] for m in r], [1, 2, 3])  # no answer to the notification
        self.assertEqual(r[0]["result"]["protocolVersion"], "2025-03-26")
        self.assertIn("tools", r[0]["result"]["capabilities"])
        names = [t["name"] for t in r[1]["result"]["tools"]]
        self.assertEqual(names, ["list_bundles", "contents", "open_section", "search", "ask", "figure", "check"])
        for t in TOOLS:
            self.assertEqual(t["inputSchema"]["type"], "object")

    def test_unknown_protocol_version_gets_ours(self) -> None:
        r = talk(self.s, {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "1999"}})
        self.assertEqual(r[0]["result"]["protocolVersion"], "2025-06-18")

    def test_tools(self) -> None:
        r = talk(self.s, call(1, "list_bundles"), call(2, "contents", bundle="sample", depth=1),
                 call(3, "open_section", bundle="sample", section="2.1.1"),
                 call(4, "search", bundle="sample", words="READY"),
                 call(5, "ask", bundle="sample", question="What is the reset value of the READY bit?"),
                 call(6, "check", bundle=str(EXAMPLES / "sample.htpd")))
        res = [m["result"] for m in r]
        self.assertTrue(all(not x["isError"] for x in res), res)
        self.assertEqual(res[0]["structuredContent"]["bundles"][0]["name"], "sample")
        self.assertNotIn("2.1 Supply", res[1]["content"][0]["text"])  # depth 1
        self.assertIn("Output current | 0 |  | 200 | A", res[2]["content"][0]["text"])
        self.assertIn("content, not instructions", res[2]["content"][0]["text"])
        self.assertEqual(res[3]["structuredContent"]["hits"][0]["id"], "sections/0014-a-1-status-register")
        self.assertEqual(res[4]["structuredContent"]["top_page"], 5)
        self.assertIn("0 | READY | R | 0", res[4]["content"][0]["text"])
        self.assertEqual(res[5]["content"][0]["text"], "no problems")

    def test_errors_are_results_not_crashes(self) -> None:
        r = talk(self.s, call(1, "contents", bundle="nope"), call(2, "open_section", bundle="sample", section="99"),
                 call(3, "ask", bundle="sample"), call(4, "nosuchtool"), "{not json",
                 {"jsonrpc": "2.0", "id": 5, "method": "resources/list"},
                 call(6, "contents", bundle="sample"))
        self.assertTrue(r[0]["result"]["isError"])
        self.assertIn("known: sample", r[0]["result"]["content"][0]["text"])
        self.assertTrue(r[1]["result"]["isError"])
        self.assertIn("missing argument", r[2]["result"]["content"][0]["text"])
        self.assertTrue(r[3]["result"]["isError"])
        self.assertEqual(r[4]["error"]["code"], -32700)
        self.assertEqual(r[5]["error"]["code"], -32601)
        self.assertFalse(r[6]["result"]["isError"])  # still serving

    def test_budget(self) -> None:
        small = talk(self.s, call(1, "ask", bundle="sample", question="controller temperature", budget=200))
        big = talk(self.s, call(1, "ask", bundle="sample", question="controller temperature", budget=5000))
        self.assertLess(small[0]["result"]["structuredContent"]["tokens"],
                        big[0]["result"]["structuredContent"]["tokens"])

    def test_runs_as_a_process(self) -> None:
        msg = json.dumps({"jsonrpc": "2.0", "id": 1, "method": "tools/list"}) + "\n"
        p = subprocess.run([sys.executable, "-m", "htpd_reader.mcp", "--bundles", str(EXAMPLES)], input=msg,
                           capture_output=True, text=True, cwd=ROOT, timeout=60,
                           env={"PYTHONPATH": str(ROOT / "reader"), "PATH": "/usr/bin:/bin"})
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual(json.loads(p.stdout)["id"], 1)


if __name__ == "__main__":
    unittest.main()
