"""Finding the engine, and the calls that reach it.

Everything here is ctypes against the C ABI in `RendeerC.h`. There is no extension
module to build: the engine is a shared library and this is a description of it, so a
Python application needs a Python interpreter and the engine binary and nothing else.

Nothing in this file is public. `rda/__init__.py` is the API; a generated state module
calls the accessors here by name.
"""

import ctypes
import os
import shutil
import sys
from ctypes import (CFUNCTYPE, POINTER, Structure, c_char_p, c_double, c_float, c_int,
                    c_int64,
                    c_int32, c_uint32, c_void_p, create_string_buffer)

# The RendeerC.h this package was written against. The major has to match the engine's
# exactly and the engine's minor has to be at least this one -- see the contract in
# RendeerC.h. Checked rather than assumed, because the two are installed separately and
# a mismatch would otherwise arrive much later, as a missing attribute or a wrong answer.
ABI_MAJOR = 1
ABI_MINOR = 6

NO_SIGNAL = 0xFFFFFFFF
NO_TABLE = 0xFFFFFFFF

# A callback handed to C must outlive the call that registered it. ctypes does not know
# that -- it would collect the trampoline as soon as the last Python reference went, and
# the engine would then call into freed memory on the next click. So every one is kept
# here for the life of the process, which is the life of the binding it belongs to.
_alive = []

_Callback = CFUNCTYPE(None, c_void_p)
_UpdateCallback = CFUNCTYPE(None, c_float, c_void_p)

DRAW_CLEAR, DRAW_RECT, DRAW_LINE, DRAW_TEXT = 0, 1, 2, 3


class DrawCmd(Structure):
    """One drawing instruction, laid out exactly as rda_draw_cmd.

    The fields are positional because the op decides what they mean; a struct per shape
    would be a union, and a union is what ctypes is worst at.
    """
    _fields_ = [
        ("op", c_int32),
        ("color", c_uint32),
        ("a", c_float), ("b", c_float), ("c", c_float), ("d", c_float),
        ("e", c_float),
        ("text", c_char_p),
    ]


class RdaError(RuntimeError):
    """Something the engine refused. The message is the engine's own."""


_lib = None
_ids = {}  # signal name -> id, since a name lookup is a round trip to the loop thread
_tables = {}  # the same for tables, and for (table, column) -> column index


def _library_name():
    if sys.platform == "win32":
        return "rendeer_c.dll"
    if sys.platform == "darwin":
        return "librendeer_c.dylib"
    return "librendeer_c.so"


def _tool_name():
    return "rda.exe" if sys.platform == "win32" else "rda"


def _staged():
    """The checkout's bin/, when this package is running from inside one.

    bindings/python/rda/ is three levels below the root, and bin/ is where the build
    stages the shared library, the tool and the engine's assets together. A package
    that was copied elsewhere -- pip's non-editable install, say -- has no checkout
    around it, and this is simply not there.
    """
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.normpath(os.path.join(here, "..", "..", "..", "bin"))


def _homes():
    """The directories the engine might be in, nearest first.

    RDA_ENGINE is the escape hatch and comes first: a developer running against a build
    tree has the binary somewhere no convention would guess.
    """
    fromenv = os.environ.get("RDA_ENGINE")
    if fromenv:
        yield os.path.dirname(fromenv) if os.path.isfile(fromenv) else fromenv
    yield os.path.dirname(os.path.abspath(__file__))   # beside the package
    yield _staged()                                     # the checkout's bin/
    yield os.getcwd()                                   # beside whatever is running


def _candidates():
    name = _library_name()
    fromenv = os.environ.get("RDA_ENGINE")
    if fromenv and os.path.isfile(fromenv):
        yield fromenv
    for home in _homes():
        yield os.path.join(home, name)
    yield name                              # and finally whatever the loader can find


def engine_home():
    """The directory holding the engine this process would load, or None."""
    name = _library_name()
    for home in _homes():
        if os.path.isfile(os.path.join(home, name)):
            return home
    return None


def tool_path():
    """The `rda` toolchain, from the same places, then PATH. None when there is none."""
    name = _tool_name()
    for home in _homes():
        candidate = os.path.join(home, name)
        if os.path.isfile(candidate):
            return candidate
    return shutil.which("rda")


def load(path=None):
    """Loads the engine. Called for you by rda.init(); call it early to fail early."""
    global _lib
    if _lib is not None:
        return _lib

    tried = []
    for candidate in ([path] if path else _candidates()):
        try:
            _lib = ctypes.CDLL(candidate)
            break
        except OSError:
            tried.append(candidate)
    if _lib is None:
        raise RdaError(
            "cannot find the engine ({}). Set RDA_ENGINE to the file or the directory "
            "holding it. Looked in: {}".format(_library_name(), ", ".join(tried)))
    _check_abi(_lib)
    _declare(_lib)
    return _lib


def _check_abi(lib):
    """Refuses an engine this package cannot talk to, before it says anything else.

    Checked here rather than trusted: the package and the engine are installed
    separately, so "the engine was rebuilt and the package was not" is the normal case
    rather than an odd one.
    """
    wanted = "{}.{}".format(ABI_MAJOR, ABI_MINOR)
    try:
        lib.rda_abi_major.restype = c_int
        lib.rda_abi_minor.restype = c_int
        major = lib.rda_abi_major()
        minor = lib.rda_abi_minor()
    except AttributeError:
        raise RdaError(
            "this engine predates the ABI version check, so it is older than this "
            "package (which needs {}). Rebuild the engine: scripts/windows-bringup.bat "
            "or scripts/linux-bringup.sh in the checkout.".format(wanted))

    found = "{}.{}".format(major, minor)
    if major != ABI_MAJOR:
        # A different major in either direction: the engine has either removed something
        # this package calls or never had it. Which way it went decides which half moves.
        which = ("The engine is behind the package: rebuild it, or use the package from "
                 "the same checkout" if major < ABI_MAJOR else
                 "The package is behind the engine: reinstall it with "
                 "python <checkout>/bindings/python/register.py")
        raise RdaError(
            "the engine speaks ABI {} and this package speaks {}. A different major "
            "number is a different ABI, not an older one, so nothing here will work. {}."
            .format(found, wanted, which))
    if minor < ABI_MINOR:
        raise RdaError(
            "the engine offers ABI {} and this package needs {}. Same major, so nothing "
            "was removed -- the engine is simply missing what was added since: rebuild "
            "it, or use the package from the same checkout.".format(found, wanted))


def _declare(lib):
    """Argument and return types, so ctypes converts rather than guesses.

    Without these every pointer is an int and every double is truncated -- and on 64-bit
    Windows a returned pointer is silently cut to 32 bits, which is the kind of bug that
    only shows up once an allocation happens to land high in memory.
    """
    lib.rda_last_error.restype = c_char_p

    lib.rda_config_new.restype = c_void_p
    lib.rda_config_free.argtypes = [c_void_p]
    for setter in ("rda_config_set_name", "rda_config_set_theme", "rda_config_set_languages"):
        getattr(lib, setter).argtypes = [c_void_p, c_char_p]
    lib.rda_config_set_vsync.argtypes = [c_void_p, c_int]
    lib.rda_config_on_start.argtypes = [c_void_p, _Callback, c_void_p]
    lib.rda_config_on_update.argtypes = [c_void_p, _UpdateCallback, c_void_p]
    lib.rda_config_on_shutdown.argtypes = [c_void_p, _Callback, c_void_p]

    lib.rda_config_set_font.argtypes = [c_void_p, c_char_p, c_float]

    lib.rda_init.argtypes = [c_void_p]
    lib.rda_init.restype = c_int
    lib.rda_running.restype = c_int
    lib.rda_load_interface.argtypes = [c_char_p, c_char_p]
    lib.rda_load_interface.restype = c_int

    lib.rda_open_routes.argtypes = [POINTER(c_char_p), POINTER(c_char_p),
                                    POINTER(c_char_p), c_int, c_char_p, c_char_p]
    lib.rda_open_routes.restype = c_int
    lib.rda_set_transition_ms.argtypes = [c_float]
    for name in ("rda_route_back", "rda_route_forward",
                 "rda_route_can_go_back", "rda_route_can_go_forward"):
        getattr(lib, name).restype = c_int

    lib.rda_table_find.argtypes = [c_char_p]
    lib.rda_table_find.restype = c_uint32
    lib.rda_table_column.argtypes = [c_uint32, c_char_p]
    lib.rda_table_column.restype = c_int
    lib.rda_table_rows.argtypes = [c_uint32]
    lib.rda_table_rows.restype = c_int
    lib.rda_table_resize.argtypes = [c_uint32, c_int]
    lib.rda_table_resize.restype = c_int
    lib.rda_table_set_numbers.argtypes = [c_uint32, c_int, c_int, POINTER(c_double), c_int]
    lib.rda_table_set_numbers.restype = c_int
    lib.rda_table_set_bools.argtypes = [c_uint32, c_int, c_int, POINTER(c_int), c_int]
    lib.rda_table_set_bools.restype = c_int
    lib.rda_table_set_texts.argtypes = [c_uint32, c_int, c_int, POINTER(c_char_p), c_int]
    lib.rda_table_set_texts.restype = c_int
    lib.rda_table_get_number.argtypes = [c_uint32, c_int, c_int, POINTER(c_double)]
    lib.rda_table_get_number.restype = c_int
    lib.rda_table_get_bool.argtypes = [c_uint32, c_int, c_int, POINTER(c_int)]
    lib.rda_table_get_bool.restype = c_int
    lib.rda_table_get_text.argtypes = [c_uint32, c_int, c_int, c_char_p, c_int]
    lib.rda_table_get_text.restype = c_int

    lib.rda_define_number.argtypes = [c_char_p, c_double]
    lib.rda_define_number.restype = c_uint32
    lib.rda_define_bool.argtypes = [c_char_p, c_int]
    lib.rda_define_bool.restype = c_uint32
    lib.rda_define_text.argtypes = [c_char_p, c_char_p]
    lib.rda_define_text.restype = c_uint32
    lib.rda_define_command.argtypes = [c_char_p]
    lib.rda_define_command.restype = c_int
    lib.rda_define_table.argtypes = [c_char_p]
    lib.rda_define_table.restype = c_int
    lib.rda_define_column.argtypes = [c_char_p, c_char_p, c_int]
    lib.rda_define_column.restype = c_int

    lib.rda_config_set_size.argtypes = [c_void_p, c_int, c_int]
    lib.rda_config_set_icon.argtypes = [c_void_p, c_char_p]
    lib.rda_config_set_decorated.argtypes = [c_void_p, c_int]
    lib.rda_config_set_resizable.argtypes = [c_void_p, c_int]
    lib.rda_config_set_maximized.argtypes = [c_void_p, c_int]
    lib.rda_config_set_fullscreen.argtypes = [c_void_p, c_int]
    lib.rda_config_set_always_on_top.argtypes = [c_void_p, c_int]
    lib.rda_config_set_transparent.argtypes = [c_void_p, c_int]
    lib.rda_config_set_opacity.argtypes = [c_void_p, c_float]
    lib.rda_config_set_size_limits.argtypes = [c_void_p, c_int, c_int, c_int, c_int]
    lib.rda_config_set_position.argtypes = [c_void_p, c_int, c_int]
    lib.rda_poll_dropped_file.argtypes = [c_char_p, c_int]
    lib.rda_poll_dropped_file.restype = c_int

    lib.rda_image_define.argtypes = [c_char_p, c_void_p, c_int]
    lib.rda_image_define.restype = c_int
    lib.rda_image_define_pixels.argtypes = [c_char_p, c_void_p, c_int, c_int]
    lib.rda_image_define_pixels.restype = c_int
    lib.rda_image_forget.argtypes = [c_char_p]
    lib.rda_image_forget.restype = c_int

    lib.rda_stream_push.argtypes = [c_char_p, c_void_p, c_int, c_int]
    lib.rda_stream_push.restype = c_int
    lib.rda_stream_push_encoded.argtypes = [c_char_p, c_void_p, c_int]
    lib.rda_stream_push_encoded.restype = c_int
    lib.rda_stream_wanted.argtypes = [c_char_p]
    lib.rda_stream_wanted.restype = c_int
    lib.rda_stream_counts.argtypes = [c_char_p, POINTER(c_int64), POINTER(c_int64)]
    lib.rda_stream_counts.restype = c_int
    lib.rda_stream_close.argtypes = [c_char_p]
    lib.rda_stream_close.restype = c_int

    lib.rda_effect_define.argtypes = [c_char_p, c_char_p]
    lib.rda_effect_define.restype = c_int
    lib.rda_effect_apply.argtypes = [c_char_p, c_char_p, c_char_p, POINTER(c_float), c_int]
    lib.rda_effect_apply.restype = c_int
    lib.rda_effect_forget.argtypes = [c_char_p]
    lib.rda_effect_forget.restype = c_int
    lib.rda_effect_apply_many.argtypes = [c_char_p, POINTER(c_char_p), c_int, c_char_p,
                                          POINTER(c_float), c_int]
    lib.rda_effect_apply_many.restype = c_int
    lib.rda_stream_read.argtypes = [c_char_p, c_char_p, c_int, POINTER(c_int),
                                    POINTER(c_int)]
    lib.rda_stream_read.restype = c_int

    lib.rda_focus.argtypes = [c_char_p]
    lib.rda_focus.restype = c_int
    lib.rda_pick_folder.argtypes = [c_char_p, c_char_p, c_char_p, c_int]
    lib.rda_pick_folder.restype = c_int
    lib.rda_pick_file.argtypes = [c_char_p, c_char_p, c_char_p, c_char_p, c_int]
    lib.rda_pick_file.restype = c_int

    lib.rda_set_title.argtypes = [c_char_p]
    lib.rda_set_title.restype = c_int
    lib.rda_set_icon.argtypes = [c_char_p]
    lib.rda_set_icon.restype = c_int
    lib.rda_clipboard_set.argtypes = [c_char_p]
    lib.rda_clipboard_set.restype = c_int
    lib.rda_clipboard_get.argtypes = [c_char_p, c_int]
    lib.rda_clipboard_get.restype = c_int
    lib.rda_measure_text.argtypes = [c_char_p, c_float, POINTER(c_float), POINTER(c_float)]
    lib.rda_measure_text.restype = c_int

    lib.rda_set_theme.argtypes = [c_char_p]
    lib.rda_set_theme.restype = c_int

    lib.rda_viewport_draw.argtypes = [c_char_p, POINTER(DrawCmd), c_int]
    lib.rda_viewport_draw.restype = c_int
    lib.rda_viewport_size.argtypes = [c_char_p, POINTER(c_float), POINTER(c_float)]
    lib.rda_viewport_size.restype = c_int

    lib.rda_signal_find.argtypes = [c_char_p]
    lib.rda_signal_find.restype = c_uint32
    lib.rda_signal_type.argtypes = [c_uint32]
    lib.rda_signal_type.restype = c_int
    lib.rda_signal_get_number.argtypes = [c_uint32, POINTER(c_double)]
    lib.rda_signal_get_number.restype = c_int
    lib.rda_signal_set_number.argtypes = [c_uint32, c_double]
    lib.rda_signal_set_number.restype = c_int
    lib.rda_signal_get_bool.argtypes = [c_uint32, POINTER(c_int)]
    lib.rda_signal_get_bool.restype = c_int
    lib.rda_signal_set_bool.argtypes = [c_uint32, c_int]
    lib.rda_signal_set_bool.restype = c_int
    lib.rda_signal_get_text.argtypes = [c_uint32, c_char_p, c_int]
    lib.rda_signal_get_text.restype = c_int
    lib.rda_signal_set_text.argtypes = [c_uint32, c_char_p]
    lib.rda_signal_set_text.restype = c_int

    lib.rda_command_bind.argtypes = [c_char_p, _Callback, c_void_p]
    lib.rda_command_bind.restype = c_int
    lib.rda_command_invoke.argtypes = [c_char_p]
    lib.rda_command_invoke.restype = c_int


def _fail(what):
    message = _lib.rda_last_error().decode("utf-8", "replace") if _lib else ""
    raise RdaError("{}: {}".format(what, message) if message else what)


def signal(name):
    """The id of a declared signal, looked up once and remembered."""
    found = _ids.get(name)
    if found is not None:
        return found
    lib = load()
    found = lib.rda_signal_find(name.encode("utf-8"))
    if found == NO_SIGNAL:
        _fail("no signal called {!r}".format(name))
    _ids[name] = found
    return found


# ---- what a generated state module calls -------------------------------------------

def define_number(name, value):
    load().rda_define_number(name.encode("utf-8"), float(value))


def define_bool(name, value):
    load().rda_define_bool(name.encode("utf-8"), 1 if value else 0)


def define_text(name, value):
    load().rda_define_text(name.encode("utf-8"), str(value).encode("utf-8"))


def define_command(name):
    if not load().rda_define_command(name.encode("utf-8")):
        _fail("cannot define the command {!r}".format(name))


def define_table(name):
    if not load().rda_define_table(name.encode("utf-8")):
        _fail("cannot define the table {!r}".format(name))


def define_column(table, column, type_):
    if not load().rda_define_column(table.encode("utf-8"), column.encode("utf-8"), type_):
        _fail("cannot define {!r}.{!r}".format(table, column))


# ---- the accessors a generated state module calls ---------------------------------

def get_number(name):
    out = c_double()
    if not _lib.rda_signal_get_number(signal(name), ctypes.byref(out)):
        _fail("cannot read {!r}".format(name))
    return out.value


def set_number(name, value):
    if not _lib.rda_signal_set_number(signal(name), float(value)):
        _fail("cannot write {!r}".format(name))


def get_bool(name):
    out = c_int()
    if not _lib.rda_signal_get_bool(signal(name), ctypes.byref(out)):
        _fail("cannot read {!r}".format(name))
    return out.value != 0


def set_bool(name, value):
    if not _lib.rda_signal_set_bool(signal(name), 1 if value else 0):
        _fail("cannot write {!r}".format(name))


def get_text(name):
    # Asked twice: once for the length, once for the text. The engine reports what the
    # value is rather than what fitted, so a guess that was too small is one retry
    # rather than a truncation nobody notices.
    handle = signal(name)
    length = _lib.rda_signal_get_text(handle, None, 0)
    if length < 0:
        _fail("cannot read {!r}".format(name))
    if length == 0:
        return ""
    buffer = create_string_buffer(length + 1)
    _lib.rda_signal_get_text(handle, buffer, length + 1)
    return buffer.value.decode("utf-8", "replace")


def set_text(name, value):
    if not _lib.rda_signal_set_text(signal(name), str(value).encode("utf-8")):
        _fail("cannot write {!r}".format(name))


# ---- tables ------------------------------------------------------------------------
#
# The rows a <list> shows. Every call here crosses to the loop thread and waits, so these
# are written a column at a time rather than a cell at a time: filling ten thousand rows
# of three columns is three round trips this way and thirty thousand the other.

def _table(name):
    """The id of a declared table, looked up once and remembered."""
    found = _tables.get(name)
    if found is not None:
        return found
    lib = load()
    found = lib.rda_table_find(name.encode("utf-8"))
    if found == NO_TABLE:
        _fail("no table called {!r}".format(name))
    _tables[name] = found
    return found


def _column(table, column):
    """The index of a declared column, looked up once and remembered."""
    key = (table, column)
    found = _tables.get(key)
    if found is not None:
        return found
    handle = _table(table)
    found = _lib.rda_table_column(handle, column.encode("utf-8"))
    if found < 0:
        _fail("no column {!r} in {!r}".format(column, table))
    _tables[key] = found
    return found


def table_rows(table):
    rows = load().rda_table_rows(_table(table))
    if rows < 0:
        _fail("cannot read the size of {!r}".format(table))
    return rows


def table_resize(table, rows):
    if not load().rda_table_resize(_table(table), int(rows)):
        _fail("cannot resize {!r}".format(table))


def set_numbers(table, column, values, first=0):
    values = [float(v) for v in values]
    if not values:
        return
    block = (c_double * len(values))(*values)
    if not _lib.rda_table_set_numbers(_table(table), _column(table, column),
                                      int(first), block, len(values)):
        _fail("cannot write {}.{}".format(table, column))


def set_bools(table, column, values, first=0):
    values = [1 if v else 0 for v in values]
    if not values:
        return
    block = (c_int * len(values))(*values)
    if not _lib.rda_table_set_bools(_table(table), _column(table, column),
                                    int(first), block, len(values)):
        _fail("cannot write {}.{}".format(table, column))


def set_texts(table, column, values, first=0):
    # Encoded into a list first, and the list kept alive until the call returns: a
    # c_char_p array built from temporaries would point at bytes already collected.
    encoded = [str(v).encode("utf-8") for v in values]
    if not encoded:
        return
    block = (c_char_p * len(encoded))(*encoded)
    if not _lib.rda_table_set_texts(_table(table), _column(table, column),
                                    int(first), block, len(encoded)):
        _fail("cannot write {}.{}".format(table, column))


def get_cell_number(table, column, row):
    out = c_double()
    if not _lib.rda_table_get_number(_table(table), _column(table, column),
                                     int(row), ctypes.byref(out)):
        _fail("cannot read {}.{}[{}]".format(table, column, row))
    return out.value


def get_cell_bool(table, column, row):
    out = c_int()
    if not _lib.rda_table_get_bool(_table(table), _column(table, column),
                                   int(row), ctypes.byref(out)):
        _fail("cannot read {}.{}[{}]".format(table, column, row))
    return out.value != 0


def get_cell_text(table, column, row):
    handle, index = _table(table), _column(table, column)
    length = _lib.rda_table_get_text(handle, index, int(row), None, 0)
    if length < 0:
        _fail("cannot read {}.{}[{}]".format(table, column, row))
    if length == 0:
        return ""
    buffer = create_string_buffer(length + 1)
    _lib.rda_table_get_text(handle, index, int(row), buffer, length + 1)
    return buffer.value.decode("utf-8", "replace")


def cell(row, name, index):
    """One field of a row, however the caller chose to spell a row.

    A generated fill() takes whatever is natural to the application: a list of dicts
    straight out of a query, a list of objects, or a list of tuples in column order.
    Deciding that here rather than making every application convert first is the whole
    reason this exists.
    """
    if isinstance(row, dict):
        return row[name]
    if isinstance(row, (list, tuple)):
        return row[index]
    return getattr(row, name)


# Handlers bound before the engine was up. Python's natural shape is a decorator at
# module level, which runs at import -- long before init(), and before the state
# declaration has created the command to bind to. Rather than make every application
# move its handlers inside a callback, they are remembered and applied once both exist.
_pending = []


def bind_command(name, fn):
    """Binds `fn` to a declared command. Returns `fn`, so it works as a decorator."""
    if _lib is None or not _lib.rda_running():
        _pending.append((name, fn))
        return fn
    _bind_now(name, fn)
    return fn


def _bind_now(name, fn):
    trampoline = _Callback(lambda _user: fn())
    _alive.append(trampoline)
    if not _lib.rda_command_bind(name.encode("utf-8"), trampoline, None):
        _fail("cannot bind {!r}".format(name))


def invoke(name):
    """Asks for a command as if the interface had.

    For when the backend is the one that wants the work: a menu, a hotkey, a scheduled
    job. The handler runs where it always runs, so it cannot tell who asked.
    """
    if not load().rda_command_invoke(name.encode("utf-8")):
        _fail("cannot invoke {!r}".format(name))


def flush_pending():
    """Applies everything bound before the engine was up.

    Called once the application's on_start has run, because that is what declares the
    commands -- binding to a name before it exists would fail for the right reason at
    the wrong time.
    """
    waiting = list(_pending)
    del _pending[:]
    for name, fn in waiting:
        _bind_now(name, fn)
