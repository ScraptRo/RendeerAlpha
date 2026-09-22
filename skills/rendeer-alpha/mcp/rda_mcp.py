#!/usr/bin/env python3
"""An MCP server for RendeerAlpha, over stdio.

Five tools: read one page of the reference, compile a layout and get the real error,
build a project, summarise a project's declared names, and disassemble a blueprint.

The point of the compiling ones is that a model should not have to be *told* whether a
binding is legal -- it can ask the compiler, which is the only thing that actually knows.

    python rda_mcp.py               speak MCP on stdin/stdout (what a client runs)
    python rda_mcp.py --selftest    call every tool once and print the result

No dependencies: the stdio transport is newline-delimited JSON-RPC, and the standard
library has everything that needs. Python 3.8 or newer.

The engine is found in RDA_ENGINE (a file or the folder holding it), then in the
checkout's bin/ three levels above this file, then on PATH.
"""

import json
import os
import re
import subprocess
import sys
import tempfile

VERSION = "1.0.0"
PROTOCOL = "2024-11-05"

HERE = os.path.dirname(os.path.abspath(__file__))
SKILL_DIR = os.path.dirname(HERE)
CHECKOUT = os.path.abspath(os.path.join(SKILL_DIR, "..", ".."))

PAGES = {
    "skill": os.path.join(SKILL_DIR, "SKILL.md"),
    "elements": os.path.join(SKILL_DIR, "references", "elements.md"),
    "bindings": os.path.join(SKILL_DIR, "references", "bindings.md"),
    "state": os.path.join(SKILL_DIR, "references", "state.md"),
    "theme": os.path.join(SKILL_DIR, "references", "theme.md"),
    "backends": os.path.join(SKILL_DIR, "references", "backends.md"),
    "recipes": os.path.join(SKILL_DIR, "references", "recipes.md"),
    "errors": os.path.join(SKILL_DIR, "references", "errors.md"),
}


# ---- finding the toolchain ---------------------------------------------------------

def tool_name():
    return "rda.exe" if sys.platform == "win32" else "rda"


def find_tool():
    """The rda executable, or None. Same order every binding uses."""
    name = tool_name()
    from_env = os.environ.get("RDA_ENGINE")
    places = []
    if from_env:
        places.append(from_env if os.path.isfile(from_env) else os.path.join(from_env, name))
    places.append(os.path.join(CHECKOUT, "bin", name))
    for place in places:
        if os.path.isfile(place):
            return place
    # PATH last, so a stale copy elsewhere never wins over the checkout's own.
    for directory in os.environ.get("PATH", "").split(os.pathsep):
        candidate = os.path.join(directory, name)
        if os.path.isfile(candidate):
            return candidate
    return None


NO_TOOL = (
    "The rda tool was not found. Build the engine once -- scripts/windows-bringup.bat or "
    "scripts/linux-bringup.sh in the checkout -- which stages it into bin/. Or set "
    "RDA_ENGINE to the folder holding it."
)


def run_tool(args, timeout=120):
    """Runs rda and returns (exit code, everything it printed)."""
    tool = find_tool()
    if not tool:
        return 1, NO_TOOL
    try:
        result = subprocess.run([tool] + list(args), capture_output=True, text=True,
                                timeout=timeout)
    except subprocess.TimeoutExpired:
        return 1, "rda did not finish within %d seconds" % timeout
    except OSError as problem:
        return 1, "cannot run %s: %s" % (tool, problem)
    return result.returncode, (result.stdout + result.stderr).strip()


# ---- the tools ----------------------------------------------------------------------

def tool_reference(args):
    page = str(args.get("page", "")).strip().lower()
    if page not in PAGES:
        return "No page called %r. There are: %s" % (page, ", ".join(sorted(PAGES)))
    try:
        with open(PAGES[page], encoding="utf-8") as f:
            return f.read()
    except OSError as problem:
        return "cannot read the %s page: %s" % (page, problem)


def tool_check(args):
    path = str(args.get("path", "")).strip()
    if not path:
        return "Give the path of a .tsx layout or a theme .ts."
    if not os.path.isfile(path):
        return "There is no file at %s" % path

    # Compiled into a temporary directory, never beside the source: checking something
    # must not quietly replace the blueprint the application is running.
    out = tempfile.mkdtemp(prefix="rdacheck")
    if path.endswith(".tsx"):
        code, output = run_tool(["layout", path, os.path.join(out, "check.rdab")])
        kind = "layout"
    elif path.endswith(".ts"):
        code, output = run_tool(["theme", path, os.path.join(out, "check.rdth")])
        kind = "theme"
    else:
        return "Only a .tsx layout or a .ts theme can be checked; %s is neither." % path

    # The first line is the tool echoing the path it was given.
    lines = [l for l in output.splitlines() if l.strip() and not l.startswith("rda: " + path)]
    body = "\n".join(l.strip() for l in lines)
    if code == 0:
        return "OK - the %s compiles.\n%s" % (kind, body)
    return ("FAILED - the %s does not compile:\n%s\n\n"
            "Read the reference page 'bindings' if this is about a thunk, or 'elements' "
            "if it is about a property." % (kind, body))


def tool_build(args):
    project = str(args.get("project", ".")).strip() or "."
    language = str(args.get("language", "")).strip().lower()
    if not os.path.isdir(project):
        return "There is no directory at %s" % project
    if not os.path.isdir(os.path.join(project, "res")):
        return ("%s has no res/ directory. A project keeps its declaration, theme and "
                "layouts there." % project)

    command = ["build", project]
    if language in ("python", "node", "csharp"):
        command.append("--" + language)
    elif language and language != "none":
        return "language must be python, node, csharp, or left out."
    if args.get("force"):
        command.append("--force")

    code, output = run_tool(command, timeout=300)
    if code == 0:
        return "OK\n" + output
    return "FAILED\n" + output


def _block(text, header):
    """The body of an `interface X {` ... `}` block, or ''."""
    match = re.search(re.escape(header) + r"\s*\{(.*?)\n\}", text, re.S)
    return match.group(1) if match else ""


def tool_project(args):
    project = str(args.get("project", ".")).strip() or "."
    if not os.path.isdir(project):
        return "There is no directory at %s" % project

    lines = []
    types = os.path.join(project, "res", "layouts", "rda.d.ts")
    if os.path.isfile(types):
        text = open(types, encoding="utf-8").read()

        signals = re.findall(r"^\t(\w+)\??: (.+?);", _block(text, "interface RdaState"), re.M)
        if signals:
            lines.append("Signals (state.<name>):")
            for name, kind in signals:
                lines.append("  %-16s %s" % (name, kind))

        commands = re.findall(r"^\t(\w+)\(\)", _block(text, "interface RdaCommands"), re.M)
        if commands:
            lines.append("Commands (commands.<name>(), handlers only):")
            lines.append("  " + ", ".join(commands))

        for table, body in re.findall(r"interface Rda(\w+)Row \{(.*?)\n\}", text, re.S):
            columns = re.findall(r"^\t(\w+)\??: (.+?);", body, re.M)
            lines.append("Table rows (item.<column> inside <list of=\"%s\">):" % table.lower())
            for name, kind in columns:
                lines.append("  %-16s %s" % (name, kind))

        route = re.search(r"^\troute\??: (.+?);", _block(text, "interface RdaState"), re.M)
        if route and '"' in route.group(1):
            lines.append("Screens (state.route = ...): " + route.group(1))
    else:
        lines.append("No res/layouts/rda.d.ts yet - run rda_build on this project first, "
                     "and the declared names will be listed here.")

    layouts = os.path.join(project, "res", "layouts")
    if os.path.isdir(layouts):
        lines.append("")
        lines.append("Layouts:")
        for name in sorted(os.listdir(layouts)):
            if not name.endswith(".tsx"):
                continue
            source = os.path.join(layouts, name)
            blueprint = source[:-4] + ".rdab"
            if not os.path.isfile(blueprint):
                note = "not compiled"
            elif os.path.getmtime(blueprint) < os.path.getmtime(source):
                note = "STALE - build again"
            else:
                note = "compiled"
            lines.append("  %-24s %s" % (name, note))

    tool = find_tool()
    lines.append("")
    lines.append("Toolchain: " + (tool if tool else "NOT FOUND - " + NO_TOOL))
    return "\n".join(lines)


def tool_dump(args):
    path = str(args.get("blueprint", "")).strip()
    if not os.path.isfile(path):
        return "There is no file at %s" % path
    code, output = run_tool(["dump", path])
    return output if code == 0 else "FAILED\n" + output


TOOLS = [
    {
        "name": "rda_reference",
        "description": (
            "Read one page of the RendeerAlpha reference. Read 'elements' before choosing "
            "an element or a property, 'bindings' before writing anything inside {() => ...}, "
            "'state' before adding a signal, command, table or screen, 'theme' for colours, "
            "'backends' for the C++/Python/Node/C# side, 'recipes' for complete working "
            "layouts, 'errors' to look up a message."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "page": {
                    "type": "string",
                    "enum": sorted(PAGES),
                    "description": "which page to read",
                },
            },
            "required": ["page"],
        },
        "handler": tool_reference,
    },
    {
        "name": "rda_check",
        "description": (
            "Compile one .tsx layout or one theme .ts and report whether it builds, with "
            "the compiler's own error. Writes nothing beside the file. Use this after "
            "writing or editing a layout, before telling anyone it is done."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "path": {"type": "string", "description": "path to the .tsx or theme .ts"},
            },
            "required": ["path"],
        },
        "handler": tool_check,
    },
    {
        "name": "rda_build",
        "description": (
            "Compile everything under a project's res/: each theme, each layout, the "
            "rda.d.ts, and the generated state module for the backend's language. Skips "
            "whatever is already up to date."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "project": {"type": "string", "description": "the project directory"},
                "language": {
                    "type": "string",
                    "enum": ["python", "node", "csharp", "none"],
                    "description": "which state module to generate; 'none' for a C++ project",
                },
                "force": {"type": "boolean", "description": "rebuild even what is up to date"},
            },
            "required": ["project"],
        },
        "handler": tool_build,
    },
    {
        "name": "rda_project",
        "description": (
            "What a project declares: every signal with its type, every command, every "
            "table's columns, the screens, and which layouts are compiled or stale. Read "
            "this before writing a layout for an existing project -- a name that is not "
            "here does not exist at run time."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "project": {"type": "string", "description": "the project directory"},
            },
            "required": ["project"],
        },
        "handler": tool_project,
    },
    {
        "name": "rda_dump",
        "description": (
            "Print what a compiled .rdab blueprint contains: every node, its properties, "
            "and the disassembled bytecode of every binding. For working out what a layout "
            "actually compiled to."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "blueprint": {"type": "string", "description": "path to a .rdab file"},
            },
            "required": ["blueprint"],
        },
        "handler": tool_dump,
    },
]

BY_NAME = dict((t["name"], t) for t in TOOLS)


# ---- the protocol -------------------------------------------------------------------

def result(request_id, payload):
    return {"jsonrpc": "2.0", "id": request_id, "result": payload}


def error(request_id, code, message):
    return {"jsonrpc": "2.0", "id": request_id, "error": {"code": code, "message": message}}


def handle(message):
    """One request in, one response out -- or None for a notification."""
    method = message.get("method")
    request_id = message.get("id")
    params = message.get("params") or {}

    if request_id is None:
        return None                      # a notification: initialized, cancelled, ...

    if method == "initialize":
        asked = params.get("protocolVersion")
        return result(request_id, {
            "protocolVersion": asked if isinstance(asked, str) and asked else PROTOCOL,
            "capabilities": {"tools": {}},
            "serverInfo": {"name": "rendeer-alpha", "version": VERSION},
            "instructions": (
                "RendeerAlpha: interfaces are TypeScript compiled to a blueprint; the "
                "backend owns the state. Call rda_reference('skill') first for the rules, "
                "rda_project before editing an existing project, and rda_check on every "
                "layout you write."),
        })

    if method == "ping":
        return result(request_id, {})

    if method == "tools/list":
        listed = [dict((k, t[k]) for k in ("name", "description", "inputSchema")) for t in TOOLS]
        return result(request_id, {"tools": listed})

    if method == "tools/call":
        name = params.get("name")
        tool = BY_NAME.get(name)
        if not tool:
            return error(request_id, -32602, "no tool called %r" % name)
        try:
            text = tool["handler"](params.get("arguments") or {})
        except Exception as problem:                          # never take the server down
            text = "%s failed: %s" % (name, problem)
            return result(request_id, {"content": [{"type": "text", "text": text}],
                                       "isError": True})
        return result(request_id, {"content": [{"type": "text", "text": text}]})

    # Clients probe for these; an empty list is friendlier than an error.
    if method in ("resources/list", "resources/templates/list"):
        return result(request_id, {"resources": [], "resourceTemplates": []})
    if method == "prompts/list":
        return result(request_id, {"prompts": []})

    return error(request_id, -32601, "method not found: %s" % method)


def serve():
    out = sys.stdout
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            message = json.loads(line)
        except ValueError:
            out.write(json.dumps(error(None, -32700, "not JSON")) + "\n")
            out.flush()
            continue
        response = handle(message)
        if response is not None:
            out.write(json.dumps(response) + "\n")
            out.flush()


# ---- checking it without a client ----------------------------------------------------

def selftest():
    print("checkout:  %s" % CHECKOUT)
    print("tool:      %s" % (find_tool() or "NOT FOUND"))
    print()

    ok = True
    for page in sorted(PAGES):
        body = tool_reference({"page": page})
        state = "%d lines" % len(body.splitlines())
        if body.startswith("cannot read"):
            state, ok = body, False
        print("  reference %-10s %s" % (page, state))

    sample = os.path.join(tempfile.mkdtemp(prefix="rdaself"), "good.tsx")
    with open(sample, "w", encoding="utf-8") as f:
        f.write('export default function P() {\n'
                '  return (<label id="a" height="content" text={() => `${state.n}`} />)\n}\n')
    answer = tool_check({"path": sample})
    print("\n  check (valid layout):   %s" % answer.splitlines()[0])
    ok = ok and answer.startswith("OK")

    with open(sample, "w", encoding="utf-8") as f:
        f.write('export default function P() {\n'
                '  return (<label id="a" text={() => Math.round(state.n)} />)\n}\n')
    answer = tool_check({"path": sample})
    print("  check (bad binding):    %s" % answer.splitlines()[0])
    ok = ok and answer.startswith("FAILED")

    print("\n  tools/list: %s" % ", ".join(sorted(BY_NAME)))
    reply = handle({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
    print("  initialize: protocol %s" % reply["result"]["protocolVersion"])

    print("\n%s" % ("everything works" if ok else "SOMETHING IS WRONG - see above"))
    return 0 if ok else 1


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        sys.exit(selftest())
    serve()
