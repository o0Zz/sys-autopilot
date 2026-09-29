#!/usr/bin/env python3
"""Bakes resource files under source/ into C headers.

Each resource lives next to the feature that uses it. Its header is written to
the same relative path under the output directory, which the build puts on the
include path:

  features/explorer/explorer.html     -> OUT/features/explorer/explorer_html.h
      The page as one C string, kExplorerHtml.

  features/process/process_tools.json -> OUT/features/process/process_tools.h
      One McpToolDef per MCP tool (kToolProcessStart, ...): its minified JSON
      definition, name first, ready for mcp_server_register_tool(). Each
      file's "$defs" hold property definitions shared by its tools; every
      "$ref": "#/$defs/<name>" is inlined, so clients never see a reference.

Usage:
  scripts/generate_resource.py --out DIR            generate every resource
  scripts/generate_resource.py --out DIR FILE...    generate only these

`make` and tests/run.sh run this; the headers are build outputs, not committed.
A header is rewritten only when its content changes, so unchanged resources
do not trigger a rebuild.
"""
import json
import os
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
SOURCE = os.path.join(ROOT, "source")
SCRIPT = "scripts/generate_resource.py"


def camel(name):
    return "".join(part[:1].upper() + part[1:] for part in name.replace("-", "_").split("_") if part)


def c_escape(s):
    return s.replace("\\", "\\\\").replace('"', '\\"').replace("\t", "\\t")


def c_string_lines(payload, width=100):
    """A long single-line payload split into adjacent C string literals."""
    return ['    "%s"' % c_escape(payload[i:i + width]) for i in range(0, len(payload), width)]


def header_banner(src_name):
    return [
        "// Generated from %s by %s -- do not edit by hand." % (src_name, SCRIPT),
        "#pragma once",
        "",
    ]


# --- text resources (HTML) ----------------------------------------------------

def generate_text(path):
    with open(path, encoding="utf-8", newline="") as f:
        text = f.read()
    if "\r" in text:
        raise SystemExit("%s must use LF line endings" % path)

    stem, ext = os.path.splitext(os.path.basename(path))
    symbol = "k" + camel(stem) + camel(ext[1:])
    lines = header_banner(os.path.basename(path)) + ["static const char %s[] =" % symbol]
    for line in text.rstrip("\n").split("\n"):
        lines.append('    "%s\\n"' % c_escape(line))
    lines[-1] += ";"
    return "%s_%s.h" % (stem, ext[1:]), "\n".join(lines) + "\n"


# --- MCP tool definitions -----------------------------------------------------

def resolve_refs(node, defs, where):
    if isinstance(node, dict):
        if "$ref" in node:
            ref = node["$ref"]
            prefix = "#/$defs/"
            if len(node) != 1 or not ref.startswith(prefix) or ref[len(prefix):] not in defs:
                raise SystemExit("%s: unresolvable $ref %r" % (where, ref))
            return resolve_refs(defs[ref[len(prefix):]], defs, where)
        return {k: resolve_refs(v, defs, where) for k, v in node.items()}
    if isinstance(node, list):
        return [resolve_refs(v, defs, where) for v in node]
    return node


def generate_tools(path):
    with open(path, encoding="utf-8") as f:
        doc = json.load(f)
    name = os.path.basename(path)
    defs = doc.get("$defs", {})
    tools = doc.get("tools")
    if not isinstance(tools, list) or not tools:
        raise SystemExit("%s: needs a non-empty \"tools\" array" % name)

    lines = header_banner(name) + ['#include "features/mcp/mcp_server.h"', ""]
    seen = set()
    for tool in tools:
        tname = tool.get("name")
        where = "%s: tool %r" % (name, tname)
        if not tname or not isinstance(tname, str):
            raise SystemExit("%s: every tool needs a \"name\"" % name)
        # The server matches tools/call names against the schema's leading
        # {"name":"<name>", so the name must need no JSON escaping.
        if not all(c.islower() or c.isdigit() or c == "_" for c in tname):
            raise SystemExit("%s: name must be [a-z0-9_]" % where)
        if tname in seen:
            raise SystemExit("%s: duplicate tool" % where)
        seen.add(tname)
        if not tool.get("description"):
            raise SystemExit("%s: missing \"description\"" % where)
        if not isinstance(tool.get("inputSchema"), dict):
            raise SystemExit("%s: missing \"inputSchema\" object" % where)

        resolved = resolve_refs(tool, defs, where)
        resolved = dict([("name", tname)] + [(k, v) for k, v in resolved.items() if k != "name"])
        payload = json.dumps(resolved, separators=(",", ":"), ensure_ascii=False)
        lines.append("static const McpToolDef kTool%s = {" % camel(tname))
        lines.extend(c_string_lines(payload))
        lines.append("};")
        lines.append("")
    return os.path.splitext(name)[0] + ".h", "\n".join(lines).rstrip("\n") + "\n"


# --- driver -------------------------------------------------------------------

def generator_for(path):
    if path.endswith("_tools.json"):
        return generate_tools
    if path.endswith(".html"):
        return generate_text
    return None


def all_resources():
    found = []
    for d, _, files in os.walk(SOURCE):
        for f in sorted(files):
            p = os.path.join(d, f)
            if generator_for(p):
                found.append(p)
    return sorted(found)


def main(argv):
    if len(argv) < 2 or argv[0] != "--out":
        raise SystemExit("usage: %s --out DIR [FILE...]" % SCRIPT)
    out_dir = argv[1]
    paths = [os.path.abspath(a) for a in argv[2:]] or all_resources()
    for p in paths:
        gen = generator_for(p)
        if not gen:
            raise SystemExit("%s: not a resource (expected *.html or *_tools.json)" % p)
        header, content = gen(p)
        out = os.path.join(out_dir, os.path.relpath(os.path.dirname(p), SOURCE), header)
        if os.path.exists(out):
            with open(out, encoding="utf-8", newline="") as f:
                if f.read() == content:
                    continue
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, "w", encoding="utf-8", newline="\n") as f:
            f.write(content)
        print("generated", os.path.relpath(p, SOURCE).replace(os.sep, "/"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
