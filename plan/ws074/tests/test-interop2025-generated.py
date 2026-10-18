#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Verify pinned generated Window HTTP inputs and real native harness boundaries."""

import ast
import functools
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
from types import SimpleNamespace
import typing
import unittest
from urllib.parse import urlsplit, urlunsplit
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[3]
WPT = ROOT / "build/ws074-suites/wpt"
SPEC = importlib.util.spec_from_file_location("generated_harness_controls", Path(__file__).with_name("run-interop2025-harness.py"))
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)
CONTRACT = RUNNER.generated_contract(WPT)
PARENT = Path(os.environ.get("WPT_GENERATED_CONTROL_ROOT", str(ROOT / "build/ws074-generated-controls"))).resolve()
PARENT.mkdir(parents=True, exist_ok=True)
RETAINED = Path(tempfile.mkdtemp(prefix="retained-", dir=PARENT))
PROGRAM = os.environ.get("WPT_GENERATED_PROGRAM")


def primary_functions():
    source = subprocess.check_output(["git", "-C", str(WPT), "show", RUNNER.WPT_COMMIT + ":tools/manifest/sourcefile.py"])
    tree = ast.parse(source)
    selected = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == "read_script_metadata"]
    namespace = dict(vars(typing))
    namespace["__builtins__"] = __builtins__
    import re
    namespace["Pattern"] = re.Pattern
    exec(compile(ast.Module(body=selected, type_ignores=[]), "<pinned-metadata-reader>", "exec"), namespace)
    source = subprocess.check_output(["git", "-C", str(WPT), "show", RUNNER.WPT_COMMIT + ":tools/serve/serve.py"])
    tree = ast.parse(source)
    html = next(node for node in tree.body if isinstance(node, ast.ClassDef) and node.name == "HtmlWrapperHandler")
    selected = [node for node in html.body if isinstance(node, ast.FunctionDef) and node.name in ("_meta_replacement", "_script_replacement")]
    exec(compile(ast.Module(body=selected, type_ignores=[]), "<pinned-html-metadata>", "exec"), namespace)
    source = subprocess.check_output(["git", "-C", str(WPT), "show", RUNNER.WPT_COMMIT + ":tools/wptserve/wptserve/server.py"])
    tree = ast.parse(source)
    rewriter = next(node for node in tree.body if isinstance(node, ast.ClassDef) and node.name == "RequestRewriter")
    selected = [node for node in rewriter.body if isinstance(node, ast.FunctionDef) and node.name == "rewrite"]
    namespace.update(urlsplit=urlsplit, urlunsplit=urlunsplit)
    exec(compile(ast.Module(body=selected, type_ignores=[]), "<pinned-request-rewriter>", "exec"), namespace)
    return namespace


PRIMARY = primary_functions()


def fixture(label, kind="any", script=b"", helper=None, helper_headers=None, query="", duplicate=False):
    root = RETAINED / label
    root.mkdir()
    for relative in ["resources/testharness.js", "resources/testharnessreport.js", "resources/webidl2/lib/webidl2.js"]:
        target = root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(WPT / relative, target)
    if duplicate:
        report_path = root / "resources/testharnessreport.js"
        report_path.write_bytes(report_path.read_bytes() + b"\n" + RUNNER.REPORT_SCRIPT.encode())
    source = label + "." + kind + ".js"
    (root / source).write_bytes(script)
    if helper is not None:
        (root / "helpers").mkdir()
        (root / "helpers/import.js").write_bytes(helper)
        if helper_headers is not None:
            (root / "helpers/import.js.headers").write_bytes(helper_headers)
    row = dict(id="/" + label + "." + kind + ".html" + query, source=source)
    raw = root / "native-raw"
    server, context, tool = RUNNER.generated_server(root, row, CONTRACT, raw)
    worker = threading.Thread(target=server.serve_forever, kwargs={"poll_interval": 0.01}, daemon=True)
    worker.start()
    return root, row, server, context, tool, worker


def stop(server, worker):
    server.shutdown()
    server.server_close()
    worker.join()


def native(label, kind, script, helper=None, helper_headers=None, query="", duplicate=False):
    root, row, server, context, tool, worker = fixture(label, kind, script, helper, helper_headers, query, duplicate)
    command = [PROGRAM, "--run", "--settle-ms=5000"] + RUNNER.font_arguments(ROOT / "userland/desktop/fonts") + ["http://127.0.0.1:%d%s" % (server.server_port, row["id"])]
    try:
        run = subprocess.run(command, capture_output=True, timeout=20, env=dict(os.environ, XDG_DATA_HOME=str(root / "data")))
    finally:
        stop(server, worker)
    raw = Path(context["raw_directory"])
    (raw / "stdout.bin").write_bytes(run.stdout)
    (raw / "stderr.bin").write_bytes(run.stderr)
    result = RUNNER.classify_output(run.stdout.decode("utf-8", errors="replace"), run.returncode)
    native_outcome = result["outcome"]
    RUNNER.generated_result(result, root, server, context, tool)
    result.update(native_outcome=native_outcome, generated=context, command=command, returncode=run.returncode)
    (root / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


class ContractTests(unittest.TestCase):
    def test_metadata_reader_matches_unmodified_primary(self):
        cases = [b'// META: title=A & <B>\r\n// META: script=x?y="a"&z=2\r\ntest();',
                 b'// META: global=window,worker\n// META: timeout=long\n\n// META: script=ignored.js\n',
                 b'/* license */\n// META: global=worker\n',
                 b'// META: global=\n// META: global=worker\n']
        for data in cases:
            self.assertEqual(RUNNER.generated_metadata(data, CONTRACT), list(PRIMARY["read_script_metadata"](io.BytesIO(data), CONTRACT["pattern"])))

    def test_wrapper_matches_original_primary_metadata_methods(self):
        data = b'// META: title=A & <B>\n// META: timeout=long\n// META: script=x?y="a"&z=2\n// META: global=window,worker\ntest();'
        for kind, source in [("WindowHandler", "/x.window.js"), ("AnyHtmlHandler", "/x.any.js")]:
            identifier = source[:-3] + ".html?q=1"
            body, context = RUNNER.generated_wrapper(identifier, source, data, CONTRACT)
            entries = list(PRIMARY["read_script_metadata"](io.BytesIO(data), CONTRACT["pattern"]))
            meta = [PRIMARY["_meta_replacement"](None, *entry) for entry in entries]
            scripts = [PRIMARY["_script_replacement"](None, *entry) for entry in entries]
            expected = CONTRACT["templates"][kind] % dict(meta="\n".join(x for x in meta if x), script="\n".join(x for x in scripts if x), path=source, query="?q=1")
            self.assertEqual(body, expected.encode())
            self.assertEqual(context["metadata"], entries)

    def test_default_first_global_and_window_exposure(self):
        for data in [b'test();', b'// META: global=\n// META: global=worker\n', b'// META: global=window,worker\n']:
            RUNNER.generated_wrapper("/x.any.html", "/x.any.js", data, CONTRACT)
        with self.assertRaises(ValueError):
            RUNNER.generated_wrapper("/x.any.html", "/x.any.js", b'// META: global=worker\n', CONTRACT)
        RUNNER.generated_wrapper("/x.window.html", "/x.window.js", b'// META: global=worker\n', CONTRACT)

    def test_only_selected_plain_window_shapes(self):
        self.assertEqual(RUNNER.generated_source(dict(id="/x.any.html?x=1", source="x.any.js")), "/x.any.js")
        for row in [dict(id="/x.any.worker.html", source="x.any.js"), dict(id="/x.https.window.html", source="x.https.window.js"),
                    dict(id="/x.sub.window.html", source="x.sub.window.js"), dict(id="/x.any.html", source="other.any.js")]:
            self.assertIsNone(RUNNER.generated_source(row))

    def test_rewrite_matches_unmodified_primary_with_query(self):
        class Logger:
            def debug(self, *_args):
                pass
        owner = SimpleNamespace(rules={path: ([verb], dest) for verb, path, dest in CONTRACT["rewrites"]}, logger=Logger())
        for method in ["GET", "HEAD"]:
            for path in ["/resources/WebIDLParser.js?x=1&y=2", "/resources/other.js"]:
                request = SimpleNamespace(path=path, command=method)
                PRIMARY["rewrite"](owner, request)
                self.assertEqual(RUNNER.generated_rewrite(path, CONTRACT, method), request.path)

    def test_http_wrapper_report_headers_alias_and_original_body(self):
        data = b'// META: script=helpers/import.js\ntest(function() {}, "case");'
        root, row, server, context, tool, worker = fixture("wire", script=data, helper=b'var imported=1;', helper_headers=b'Content-Type: text/javascript; charset=utf-8\nX-Fixture: retained\n', query="?q=wire")
        base = "http://127.0.0.1:%d" % server.server_port
        try:
            with urlopen(base + row["id"]) as response:
                self.assertEqual(response.read(), (root / "native-raw/wrapper.html").read_bytes())
                self.assertEqual(response.headers["Content-Type"], "text/html")
            with urlopen(base + "/helpers/import.js") as response:
                self.assertEqual(response.read(), b'var imported=1;')
                self.assertEqual(response.headers.get_all("Content-Type"), ["text/javascript; charset=utf-8"])
                self.assertEqual(response.headers["X-Fixture"], "retained")
            with urlopen(base + "/resources/testharnessreport.js?one=1") as response:
                self.assertEqual(response.read(), (root / "resources/testharnessreport.js").read_bytes() + b"\n" + RUNNER.REPORT_SCRIPT.encode())
            with urlopen(base + "/resources/WebIDLParser.js?unchanged=1") as response:
                self.assertEqual(response.read(), (WPT / "resources/webidl2/lib/webidl2.js").read_bytes())
        finally:
            stop(server, worker)
        self.assertEqual(server.rewrite_records, [dict(incoming="/resources/WebIDLParser.js?unchanged=1", destination="/resources/webidl2/lib/webidl2.js?unchanged=1")])
        self.assertEqual(RUNNER.generated_environment(root, server, context, tool), [])

    def test_missing_pinned_idl_wire_query_and_real_404(self):
        from urllib.error import HTTPError
        root, row, server, context, tool, worker = fixture("pinned-idl", script=b"test(function(){}, 'wire');")
        base = "http://127.0.0.1:%d" % server.server_port
        request = "/interfaces/event-timing.idl?source=pinned"
        expected = subprocess.check_output(["git", "-C", str(WPT), "show", RUNNER.WPT_COMMIT + ":interfaces/event-timing.idl"])
        try:
            with urlopen(base + request) as response:
                self.assertEqual(response.read(), expected)
                self.assertEqual(response.headers.get_all("Content-Type"), ["application/octet-stream"])
                self.assertEqual(int(response.headers["Content-Length"]), len(expected))
            with self.assertRaises(HTTPError) as caught:
                urlopen(base + "/interfaces/nonexistent-ws074-control.idl")
            self.assertEqual(caught.exception.code, 404)
            caught.exception.close()
            # A pinned non-IDL resource must remain a diagnosed unsupported input.
            with self.assertRaises(HTTPError) as caught:
                urlopen(base + "/resources/idlharness.js")
            self.assertEqual(caught.exception.code, 404)
            caught.exception.close()
        finally:
            stop(server, worker)
        self.assertFalse((root / "interfaces/event-timing.idl").exists())
        limits = RUNNER.generated_environment(root, server, context, tool)
        self.assertIn(["/resources/idlharness.js", "missing_pinned_resource"], limits)
        self.assertNotIn(["/interfaces/nonexistent-ws074-control.idl", "missing_pinned_resource"], limits)
        asset = next(x for x in context["assets"] if x["path"] == "interfaces/event-timing.idl")
        self.assertEqual(asset["sha256"], hashlib.sha256(expected).hexdigest())
        self.assertEqual(asset["request"], request)
        self.assertEqual(len(asset["absent_header_paths"]), 4)
        result = dict(outcome="pass")
        RUNNER.generated_result(result, root, server, context, tool)
        self.assertEqual(result["outcome"], "unsupported_environment")

    def test_pinned_idl_metadata_is_never_silently_ignored(self):
        contract = dict(CONTRACT, pinned_paths=CONTRACT["pinned_paths"] | {"interfaces/__dir__.sub.headers"})
        with self.assertRaisesRegex(ValueError, "headers"):
            RUNNER.generated_git_resource("interfaces/dom.idl", contract)
        for path in ["interfaces/../resources/testharness.js", "interfaces/other/nested.idl", "resources/idlharness.js"]:
            self.assertIsNone(RUNNER.generated_git_resource(path, CONTRACT))

    def test_missing_meta_and_wrapper_headers_are_refused(self):
        root = RETAINED / "refused"
        root.mkdir()
        row = dict(id="/x.any.html", source="x.any.js")
        (root / row["source"]).write_bytes(b'// META: script=missing.js\n')
        with self.assertRaisesRegex(ValueError, "absent"):
            RUNNER.generated_server(root, row, CONTRACT, root / "absent-raw")
        (root / "x.any.js.headers").write_text("Content-Type: text/plain\n")
        with self.assertRaisesRegex(ValueError, "wrapper response headers"):
            RUNNER.generated_server(root, row, CONTRACT, root / "header-raw")


@unittest.skipUnless(PROGRAM, "set WPT_GENERATED_PROGRAM to run real native controls")
class NativeTests(unittest.TestCase):
    def test_any_metadata_order_global_query_current_script_and_log(self):
        script = b'''// META: global=window,worker
// META: script=helpers/import.js
test(function() {
 assert_true(GLOBAL.isWindow()); assert_false(GLOBAL.isWorker()); assert_false(GLOBAL.isShadowRealm());
 assert_equals(order, "helper"); assert_equals(location.search, "?q=any");
 assert_true(!!document.getElementById("log"));
 assert_equals(document.currentScript.getAttribute("src"), "/any-positive.any.js");
}, "native any wrapper");'''
        result = native("any-positive", "any", script, helper=b'var order="helper";', helper_headers=b'Content-Type: text/javascript; charset=utf-8\n', query="?q=any")
        self.assertEqual(result["outcome"], "pass")
        self.assertEqual(len(result["result_callbacks"]), 1)

    def test_window_has_official_window_environment(self):
        script = b'''test(function() {
 assert_equals(typeof GLOBAL, "undefined"); assert_equals(self, window);
 assert_equals(document.currentScript.getAttribute("src"), "/window-positive.window.js");
 assert_true(!!document.getElementById("log"));
}, "native window wrapper");'''
        self.assertEqual(native("window-positive", "window", script)["outcome"], "pass")

    def test_native_failure_stays_failure(self):
        self.assertEqual(native("assertion-negative", "any", b'test(function(){assert_true(false);}, "negative");')["outcome"], "fail")

    def test_unsupported_server_resource_cannot_be_pass(self):
        script = b'// META: script=helpers/dynamic.sub.js\ntest(function(){assert_equals(dynamicValue,1);}, "static fallback is unsupported");'
        root = RETAINED / "unsupported"
        root.mkdir()
        for relative in ["resources/testharness.js", "resources/testharnessreport.js"]:
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(WPT / relative, path)
        (root / "helpers").mkdir()
        (root / "helpers/dynamic.sub.js").write_bytes(b'var dynamicValue=1;')
        (root / "unsupported.any.js").write_bytes(script)
        row = dict(id="/unsupported.any.html", source="unsupported.any.js")
        server, context, tool = RUNNER.generated_server(root, row, CONTRACT, root / "raw")
        worker = threading.Thread(target=server.serve_forever, kwargs={"poll_interval": 0.01}, daemon=True)
        worker.start()
        try:
            run = subprocess.run([PROGRAM, "--run", "--settle-ms=5000"] + RUNNER.font_arguments(ROOT / "userland/desktop/fonts") + ["http://127.0.0.1:%d%s" % (server.server_port, row["id"])], capture_output=True, timeout=20)
        finally:
            stop(server, worker)
        result = RUNNER.classify_output(run.stdout.decode(), run.returncode)
        self.assertEqual(result["outcome"], "pass")
        limits = RUNNER.generated_result(result, root, server, context, tool)
        self.assertEqual(result["outcome"], "unsupported_environment")
        self.assertEqual(result["native_outcome"], "pass")
        self.assertIn(["/helpers/dynamic.sub.js", "generated_server_resource"], limits)
        (root / "stdout.bin").write_bytes(run.stdout)
        (root / "stderr.bin").write_bytes(run.stderr)
        (root / "result.json").write_text(json.dumps(dict(native=result, unsupported=limits), indent=2) + "\n")

    def test_duplicate_completion_cannot_be_pass(self):
        result = native("duplicate", "any", b'test(function(){assert_true(true);}, "duplicate");', duplicate=True)
        self.assertEqual(result["outcome"], "multiple_completions")

    def test_zero_tests_cannot_be_pass(self):
        result = native("zero", "any", b'var no_tests_registered=1;')
        self.assertNotEqual(result["outcome"], "pass")


if __name__ == "__main__":
    unittest.main()
