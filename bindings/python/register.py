"""Makes `import rda` resolve to this checkout, for one Python interpreter.

    python register.py              register with the interpreter running this
    python register.py --remove     undo it
    python register.py --check      say whether it is registered, and where

Run it with the interpreter you want to use: inside an activated virtual environment it
registers with that environment; outside one, with your user site-packages, which every
interpreter of that version reads. The bring-up scripts run it for the interpreter on
PATH.

What it does is write one line -- the path to bindings/python -- into a `.pth` file,
which is the mechanism Python itself provides for "also look here". Nothing is copied,
so a rebuild of the engine is seen at once, and nothing needs pip: on a Linux with an
externally managed system Python, `pip install` refuses and this does not.

`pip install -e <this directory>` is the same thing done through pip, for a virtual
environment that would rather have it listed.
"""

import os
import site
import sys
import sysconfig

HERE = os.path.dirname(os.path.abspath(__file__))
NAME = "rda.pth"


def target_directory():
    """Where a .pth is read from, for the interpreter running this."""
    in_venv = sys.prefix != getattr(sys, "base_prefix", sys.prefix)
    if in_venv:
        return sysconfig.get_paths()["purelib"], "this virtual environment"
    if not site.ENABLE_USER_SITE:
        return None, ("user site-packages are disabled for this interpreter, so there is "
                      "nowhere to register. Activate a virtual environment and run this "
                      "again, or set PYTHONPATH={}".format(HERE))
    return site.getusersitepackages(), "your user site-packages"


def main(argv):
    directory, where = target_directory()
    if directory is None:
        print("rda: " + where)
        return 1
    path = os.path.join(directory, NAME)

    if "--check" in argv:
        if os.path.isfile(path):
            with open(path, encoding="utf-8") as f:
                print("rda: registered in {} -> {}".format(path, f.read().strip()))
            return 0
        print("rda: not registered for {} ({})".format(sys.executable, where))
        return 1

    if "--remove" in argv:
        if os.path.isfile(path):
            os.remove(path)
            print("rda: removed " + path)
        else:
            print("rda: nothing to remove at " + path)
        return 0

    os.makedirs(directory, exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(HERE + "\n")
    print("rda: registered with {} ({})".format(sys.executable, where))
    print("     {} -> {}".format(path, HERE))
    print("     `import rda` now resolves there. In a virtual environment, run this "
          "again with that environment active, or `pip install -e {}`.".format(HERE))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
