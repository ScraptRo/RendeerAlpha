# Any language with an FFI

`RendeerAlpha/include/RendeerC.h` — 60 functions, built as `rendeer_c.dll` /
`librendeer_c.so`. Everything above is written on top of it — Python, Node and C# alike.

## Which ABI you are talking to

Two numbers, and a binding reads them before it trusts anything else here:

```c
int rda_abi_major(void);   // 1
int rda_abi_minor(void);   // 0
```

| | |
| --- | --- |
| **major** | something was removed, renamed, given a different signature, or made to mean something else |
| **minor** | something was added and nothing else moved |

So the check a binding makes is two lines, and each one is a different question:

```
engine major == the major it was built for     // the same ABI at all
engine minor >= the minor it was built for     // and new enough to have everything
```

The major is compared for **equality, in both directions**. An engine a major ahead has
removed something the package calls; an engine a major behind never had it. Neither is
"older", so neither is worth trying.

The minor is one-sided, and that asymmetry is the whole point: an engine that has moved
from 1.0 to 1.7 still answers every package built against 1.0, unchanged. That promise
used to take a second function to state — the engine reporting how far back it went — and
now it is readable off the number itself.

**The case people get wrong** is a function that keeps its name and its signature and
starts meaning something else. Nothing catches that: not the compiler, not the loader, not
a symbol table. It is a major change, and treating it as one is the reason this is two
numbers rather than a build date.

This matters because the two halves are installed separately. A Python package is
registered once by path and the shared library is rebuilt underneath it; a `.csproj` copies
a DLL that a different checkout may have produced. Without the check, a mismatch is a
missing entry point at best and a function that quietly means something else at worst. The
bindings in this repository all make it at load and refuse with a sentence saying which
half is behind and what to do about it:

```
the engine offers ABI 1.0 and this package needs 1.5. Same major, so nothing was
removed -- the engine is simply missing what was added since: rebuild it, or use
the package from the same checkout.
```

`rda_init` also writes the pair into `RDA_DEBUG.txt` once, so a report of some later
oddity is answerable without asking which two halves were in the room.

These two functions may never change. They are what everything else is checked with.

**1.1** added `rda_set_theme`. **1.2** added `rda_config_set_size`, `rda_set_title`,
`rda_clipboard_set`, `rda_clipboard_get` and `rda_measure_text`. **1.3** added `rda_focus`,
`rda_pick_folder` and `rda_pick_file`. **1.4** added `rda_poll_dropped_file`. **1.5** added
`rda_image_define`, `rda_image_define_pixels` and `rda_image_forget`. **1.6** added the
stream and effect calls, and then the window ones: `rda_config_set_icon`,
`rda_config_set_decorated`, `rda_config_set_resizable`, `rda_config_set_maximized`,
`rda_config_set_fullscreen`, `rda_config_set_always_on_top`, `rda_config_set_transparent`,
`rda_config_set_opacity`, `rda_config_set_size_limits`, `rda_config_set_position` and
`rda_set_icon`. Nothing was removed or changed in any of them, so a package built against
1.0 still runs against a 1.6 engine untouched — which is the split doing exactly the job
it was added for.

### Not the same as the blueprint's version

`.rdab` files carry a version too, and it is deliberately a single number checked for
equality. The reader does fixed-stride reads straight out of the mapped bytes, so there is
no such thing as an additive change there — and a blueprint is regenerated from its `.tsx`
by `rda build`, so a mismatch means "rebuild your layouts" rather than "two installed
things drifted". Major and minor would be a promise that format cannot make.

## The threading contract

**Every call that touches engine state crosses to the loop thread and waits.** The signal
table is a vector of strings and observer lists; writing one from another thread while the
loop reads it is a race with a reallocating `std::string` in it — the kind that corrupts
rather than the kind that returns the wrong number. So nothing in the ABI touches the
signals, the commands, the tables or the widget tree on the caller's thread. Each call
hands its work to `LoopWork` and waits for the loop to run it.

A handful stand outside it, and each says why in its own comment: `rda_running`,
`rda_stop` and `rda_wait` read an atomic, `rda_set_transition_ms` stores one, and
`rda_poll_command` and `rda_poll_dropped_file` each read a queue of their own. The two file
choosers are the other kind of exception: they block for as long as a person is looking at
a dialog, which is far too long to hold the loop, so they run on the calling thread and the
window keeps drawing behind them. Everything else hops.

Three things follow:

- **A call costs a round trip to the next frame.** That is the right price for a backend
  and the wrong price for a render path, which is why there is no drawing here at all.
- **An idle window is woken first.** With on-demand redraw the loop can be parked in the
  event pump with no frame due for seconds, and a request that waited for one would look
  like a hang. `glfwPostEmptyEvent` is safe from any thread and is what does it.
- **A call from inside a callback runs inline.** Callbacks already run on the loop thread,
  so loading an interface from `on_start` costs nothing extra.

## Errors

Functions returning `int` return 1 for success and 0 for failure; `rda_last_error()` says
what went wrong, per thread. A failure is never a crash: a name that does not exist, a file
that will not load and a call made before `rda_init()` all return 0.

## The surface

| | |
| --- | --- |
| **Configuration** | `rda_config_new/free`, `set_name/theme/languages/vsync/font`, `on_start/on_update/on_shutdown` |
| **Lifecycle** | `rda_init`, `rda_running`, `rda_stop`, `rda_wait` |
| **Interface** | `rda_load_interface` |
| **Screens** | `rda_open_routes`, `rda_set_transition_ms`, `rda_route_back/forward`, `rda_route_can_go_back/forward` |
| **Declaring** | `rda_define_number/bool/text/command/table/column` |
| **Signals** | `rda_signal_find/type`, `rda_signal_get_/set_` × `number`/`bool`/`text` |
| **Tables** | `rda_table_find/column/rows/resize`, `rda_table_set_numbers/bools/texts`, `rda_table_get_number/bool/text` |
| **Commands** | `rda_command_bind`, or `rda_command_watch` + `rda_poll_command`; `rda_command_invoke` to ask for one yourself |
| **Viewports** | `rda_viewport_draw`, `rda_viewport_size` |
| **Theme** | `rda_set_theme` — swap the whole look while the window is open |
| **The window** | `rda_config_set_size` (opening size), `rda_set_title`, `rda_set_icon`, and the `rda_config_set_*` group below |
| **How it opens** | `rda_config_set_icon`, `_decorated`, `_resizable`, `_maximized`, `_fullscreen`, `_always_on_top`, `_transparent`, `_opacity`, `_size_limits`, `_position` — read when the window is created, which is why they are on the config |
| **What it does after** | signals, not calls: `rda.maximized`, `rda.minimized`, `rda.fullscreen` and `rda.open` read *and* write; `rda.width`, `rda.height` and `rda.focused` report. See [the window](../frontend/window.md) |
| **The clipboard** | `rda_clipboard_set`, `rda_clipboard_get` |
| **Measuring** | `rda_measure_text` — what a string would be drawn at |
| **The keyboard** | `rda_focus` — put the caret on a widget, by the id its layout gave it |
| **Files** | `rda_pick_folder`, `rda_pick_file` — the platform's own chooser; `rda_poll_dropped_file` — what was let go over the window |
| **Pictures** | `rda_image_define`, `rda_image_define_pixels`, `rda_image_forget` — showing bytes without writing a file |
| **Streams** | `rda_stream_push`, `rda_stream_push_encoded`, `rda_stream_wanted`, `rda_stream_counts`, `rda_stream_close` — a picture that keeps arriving |
| **Effects** | `rda_effect_define`, `rda_effect_apply`, `rda_effect_apply_many`, `rda_effect_forget` — a compute filter over one to four pictures, without the Vulkan |
| **Reading back** | `rda_stream_read` — a frame copied into ordinary memory, as it looks on screen |

Two conventions worth knowing before writing a binding:

**Ids, not names, in the hot path.** `rda_signal_find` and `rda_table_find` are called once
and the number kept, because a name lookup is a round trip like any other.

**Text is asked for twice.** `rda_signal_get_text(id, NULL, 0)` reports the length the text
actually is; the second call fills a buffer. A caller that guessed too small retries rather
than truncating something nobody notices.

**Rows are written in runs.** `rda_table_set_numbers(table, column, first, values, count)`
writes a whole column in one hop. Filling a table cell by cell would be one round trip per
cell.

**Queues are drained, not delivered.** Commands and dropped files both arrive on the
engine's thread, and a callback from there is something a JavaScript binding cannot take.
So both are queued and asked for: call `rda_poll_dropped_file` until it answers 0, on the
same beat everything else is polled. Nothing is lost between one look and the next — the
queue is bounded, and a backend that stops asking entirely loses the oldest with a line in
the log.

**A drawing is written in one run too.** `rda_viewport_draw(name, commands, count)` takes
the whole picture as an array of `rda_draw_cmd` — four ops (`CLEAR`, `RECT`, `LINE`,
`TEXT`), positional fields whose meaning is the op's, because a struct per shape would be
a union and a union is what an FFI is worst at. See
[drawing in a viewport](viewport.md).

## In C

```c
#include <RendeerC.h>
#include <stdio.h>

static uint32_t gCount;

static void on_add(void* user) {
    double n = 0;
    rda_signal_get_number(gCount, &n);
    rda_signal_set_number(gCount, n + 1);
}

static void on_start(void* user) {
    gCount = rda_define_number("count", 0);
    rda_define_command("add");
    rda_load_interface("res/layouts/home.rdab", "");
    rda_command_bind("add", on_add, NULL);
}

int main(void) {
    rda_config* config = rda_config_new();
    rda_config_set_name(config, "My application");
    rda_config_set_theme(config, "res/themes/app.rdth");
    rda_config_on_start(config, on_start, NULL);

    if (!rda_init(config)) {
        fprintf(stderr, "%s\n", rda_last_error());
        return 1;
    }
    rda_config_free(config);     // rda_init copied what it needs
    rda_wait();
    return 0;
}
```

---

Back to [the backend index](README.md) · [all documentation](../README.md)
