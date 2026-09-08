"""The toolchain, from a Python project.

    python -m rda build [project] [--force]    compile the interface under <project>/res/
    python -m rda where                        which engine and tool this would use

`build` runs the engine's `rda build <project> --python`: every theme to its .rdth, every
layout to its .rdab, res/layouts/rda.d.ts, and state.py into the project directory --
skipping whatever is already newer than its source. Run it once before the first
`python app.py`, and again whenever res/state.ts changes; a running Debug engine
recompiles an edited layout by itself.

Anything else after `python -m rda` is handed to the tool as it is, so
`python -m rda dump res/layouts/home.rdab` works too.
"""

import os
import subprocess
import sys

from ._engine import engine_home, tool_path


def main(argv):
    if not argv or argv[0] in ("-h", "--help", "help"):
        print(__doc__.strip())
        return 0 if argv else 1

    tool = tool_path()
    if argv[0] == "where":
        home = engine_home()
        print("engine:  " + (home or "not found"))
        print("tool:    " + (tool or "not found"))
        if home and os.path.isfile(os.path.join(home, "BUILD.txt")):
            with open(os.path.join(home, "BUILD.txt"), encoding="utf-8") as f:
                print("built:   " + f.read().strip())
        return 0 if tool else 1

    if not tool:
        print("rda: cannot find the rda tool. Build the engine and stage it -- "
              "scripts/windows-bringup.bat or scripts/linux-bringup.sh in the checkout -- "
              "or set RDA_ENGINE to the folder holding it.")
        return 1

    command = [tool] + list(argv)
    if argv[0] == "build":
        # This is a Python project, so the state comes out as Python. Nothing stops a
        # project asking for more; the flags pass through.
        if not any(a in ("--python", "--node", "--csharp", "--cpp") for a in argv):
            command.append("--python")
    try:
        return subprocess.call(command)
    except OSError as problem:
        print("rda: cannot run {}: {}".format(tool, problem))
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
