#!/usr/bin/env python3
"""Bakes resource files under source/ into C headers.

Each resource lives next to the feature that uses it. Its header is written to
the same relative path under the output directory, which the build puts on the
include path:

  features/explorer/explorer.html     -> OUT/features/explorer/explorer_html.h
      The page as one C string, kExplorerHtml. Inside <script> blocks,
      indentation, blank lines and full-line // comments are dropped.

  features/process/process_tools.json -> OUT/features/process/process_tools.h
      One McpToolDef per MCP tool (kToolProcessStart, ...): its minified JSON
      definition, name first, ready for mcp_server_register_tool(). Each
      file's "$defs" hold property definitions shared by its tools; every
      "$ref": "#/$defs/<name>" is inlined, so clients never see a reference.
      A $def used by several tools is stored once: the tool's text holds a
      byte 0x01..0x1f where it goes, an index into the McpToolDef array (see
      mcp_server.h).

Usage:
  scripts/generate_resource.py --out DIR            generate every resource
  scripts/generate_resource.py --out DIR FILE...    generate only these

`make` and tests/run.sh run this; the headers are build outputs, not committed.
A header is rewritten only when its content changes, so unchanged resources
do not trigger a rebuild.
"""
import json
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
SOURCE = os.path.join(ROOT, "source")
SCRIPT = "scripts/generate_resource.py"


def camel(name):
    return "".join(part[:1].upper() + part[1:] for part in name.replace("-", "_").split("_") if part)


def c_escape(s):
    s = s.replace("\\", "\\\\").replace('"', '\\"').replace("\t", "\\t")
    return re.sub(r"[\x00-\x1f]", lambda m: "\\%03o" % ord(m.group()), s)


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

def squeeze_scripts(lines, path):
    """Drops indentation, blank lines and full-line // comments inside <script>
    blocks. Line breaks stay (automatic semicolon insertion relies on them)."""
    out, in_script = [], False
    for line in lines:
        s = line.strip()
        if in_script and s == "</script>":
            in_script = False
        elif in_script:
            # A template literal or a continued string could span lines, where
            # the indentation is content.
            if "`" in s or s.endswith("\\"):
                raise SystemExit("%s: multi-line JS strings are not supported" % path)
            if s and not s.startswith("//"):
                out.append(s)
            continue
        elif s == "<script>":
            in_script = True
        out.append(line)
    return out


def generate_text(path):
    with open(path, encoding="utf-8", newline="") as f:
        text = f.read()
    if "\r" in text:
        raise SystemExit("%s must use LF line endings" % path)

    stem, ext = os.path.splitext(os.path.basename(path))
    symbol = "k" + camel(stem) + camel(ext[1:])
    lines = header_banner(os.path.basename(path)) + ["static const char %s[] =" % symbol]
    for line in squeeze_scripts(text.rstrip("\n").split("\n"), path):
        lines.append('    "%s\\n"' % c_escape(line))
    lines[-1] += ";"
    return "%s_%s.h" % (stem, ext[1:]), "\n".join(lines) + "\n"


# --- MCP tool definitions -----------------------------------------------------

# A shared $def's place in a tool's text is marked by a byte 0x01..0x1f (raw
# control characters never occur in JSON text), so a tool can use 31 of them.
MAX_SHARED = 0x1f

# What a $ref kept for sharing serializes to (see resolve_refs).
PLACEHOLDER = r'"\\u0000(\w+)\\u0000"'


def minify(node):
    return json.dumps(node, separators=(",", ":"), ensure_ascii=False)


def resolve_refs(node, defs, where, keep=()):
    """Inlines every $ref, except that one to a def in `keep` becomes a
    placeholder string."""
    if isinstance(node, dict):
        if "$ref" in node:
            ref = node["$ref"]
            prefix = "#/$defs/"
            if len(node) != 1 or not ref.startswith(prefix) or ref[len(prefix):] not in defs:
                raise SystemExit("%s: unresolvable $ref %r" % (where, ref))
            d = ref[len(prefix):]
            if d in keep:
                return "\0%s\0" % d
            return resolve_refs(defs[d], defs, where, keep)
        return {k: resolve_refs(v, defs, where, keep) for k, v in node.items()}
    if isinstance(node, list):
        return [resolve_refs(v, defs, where, keep) for v in node]
    return node


def tool_pieces(tool, defs, where, keep=()):
    """The tool's minified JSON, name first, split around its kept $refs:
    [text, def, text, def, ..., text]."""
    resolved = resolve_refs(tool, defs, where, keep)
    resolved = dict([("name", tool["name"])] + [(k, v) for k, v in resolved.items() if k != "name"])
    return re.split(PLACEHOLDER, minify(resolved))


def grow_shared(pieces, shared):
    """Moves the JSON that surrounds every use of a shared def (its property
    key, the separator before the next one...) into the shared text, then
    merges shared defs that always appear back to back. `pieces` lists are
    edited in place."""
    def uses(d):
        return [(ps, i) for ps in pieces for i in range(1, len(ps), 2) if ps[i] == d]

    changed = True
    while changed:
        changed = False
        for d in list(shared):
            occ = uses(d)
            while True:  # the character every use has just before
                c = {ps[i - 1][-1:] for ps, i in occ}
                if len(c) != 1 or c == {""}:
                    break
                shared[d] = c.pop() + shared[d]
                for ps, i in occ:
                    ps[i - 1] = ps[i - 1][:-1]
            while True:  # the character every use has just after
                c = {ps[i + 1][:1] for ps, i in occ}
                if len(c) != 1 or c == {""}:
                    break
                shared[d] += c.pop()
                for ps, i in occ:
                    ps[i + 1] = ps[i + 1][1:]
            # The shared def that every use is directly followed by, and only so.
            nxt = {ps[i + 2] if not ps[i + 1] and i + 2 < len(ps) else None for ps, i in occ}
            if len(nxt) == 1 and None not in nxt and d not in nxt:
                e = nxt.pop()
                if len(uses(e)) == len(occ):
                    shared[d] += shared.pop(e)
                    for ps, i in reversed(occ):
                        del ps[i + 1:i + 3]
                    changed = True
                    break


def generate_tools(path):
    with open(path, encoding="utf-8") as f:
        doc = json.load(f)
    name = os.path.basename(path)
    defs = doc.get("$defs", {})
    tools = doc.get("tools")
    if not isinstance(tools, list) or not tools:
        raise SystemExit("%s: needs a non-empty \"tools\" array" % name)

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

    # Share a $def when storing it once saves more than its pointers cost.
    counts = {}
    for tool in tools:
        for d in tool_pieces(tool, defs, name, defs)[1::2]:
            counts[d] = counts.get(d, 0) + 1
    texts = {d: minify(resolve_refs(defs[d], defs, name)) for d in counts}
    shared = {d: t for d, t in texts.items() if (counts[d] - 1) * len(t) > 9 * counts[d]}
    pieces = [tool_pieces(tool, defs, name, shared) for tool in tools]
    grow_shared(pieces, shared)

    stem = camel(name[:-len("_tools.json")])
    symbols = {d: "k%sDef%s" % (stem, camel(d)) for d in shared}
    lines = header_banner(name) + ['#include "features/mcp/mcp_server.h"', ""]
    for d, text in shared.items():
        lines.append("static const char %s[] =" % symbols[d])
        lines.extend(c_string_lines(text))
        lines[-1] += ";"
        lines.append("")
    for tool, ps in zip(tools, pieces):
        where = "%s: tool %r" % (name, tool["name"])
        used = list(dict.fromkeys(ps[1::2]))
        if len(used) > MAX_SHARED:
            raise SystemExit("%s: more than %d shared $defs" % (where, MAX_SHARED))
        text = "".join(chr(used.index(p) + 1) if i % 2 else p for i, p in enumerate(ps))
        # Self-check: the text with every marker expanded is the tool's full
        # definition, and still starts with its name (see mcp_server.h).
        full = tool_pieces(tool, defs, where)[0]
        if "".join(shared[p] if i % 2 else p for i, p in enumerate(ps)) != full or \
                re.search(r"[\x00-\x1f]", "".join(ps[0::2]) + "".join(shared.values())) or \
                not text.startswith('{"name":"%s"' % tool["name"]):
            raise SystemExit("%s: shared $defs do not rebuild the definition" % where)
        lines.append("static const McpToolDef kTool%s = {" % camel(tool["name"]))
        lines.extend(c_string_lines(text))
        lines[-1] += ","
        lines.extend("    %s," % symbols[d] for d in used)
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
