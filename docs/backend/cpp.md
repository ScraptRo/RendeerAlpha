# C++

The native case. The engine is a static library; there is no ABI in the way and no round
trip to anything.

## Starting up

```cpp
#include <RendeerAlpha.h>
#include <Layout/LayoutHost.h>
#include <RdaState.h>

RDA::Layout::LayoutHost gHost;

int main() {
    RDA::AppConfig config;
    config.app.name = "My application";
    config.gui.themePath = "res/themes/app.rdth";
    config.redrawMode = RDA::RedrawMode::OnDemand;

    config.onStart = [] {
        RDA::State::define();                     // the names, first
        RDA::State::onSave([] { … });             // what the commands do
        gHost.open(getMainWindow()->gui().retained(), "res/layouts/home.rdab");
    };
    config.onUpdate = [](float dt) { gHost.reloadIfChanged(); };

    rendeerRun(config);
}
```

`define()` before the layout opens, always. A binding resolves the signals it reads when it
is instantiated; one that does not exist yet is created as a number and complained about.

## State

```cpp
double n = RDA::State::count();
RDA::State::setCount(n + 1);

std::string_view t = RDA::State::title();
RDA::State::setTitle("ready");
```

Reading is an array index, not a hash of a name: `define()` fills a table of ids and the
accessors use it.

## Commands

```cpp
RDA::State::onSave([] {
    RDA_LOG_INFO("saving " << RDA::State::text_box().size() << " characters");
});
```

Until something is bound, the command exists and does nothing, and whatever calls it
reports that rather than failing quietly.

## Tables

```cpp
std::vector<RDA::State::ProductsRow> rows;
rows.reserve(10000);
for (int i = 0; i < 10000; ++i) {
    RDA::State::ProductsRow row;
    row.title   = "Product " + std::to_string(i);
    row.price   = (i * 7) % 90 + 9.0;
    row.inStock = (i % 3) != 0;
    rows.push_back(std::move(row));
}
RDA::State::setProducts(rows);
```

One call rather than a loop of setters, so the column order stays where it was declared.

## Screens

```cpp
RDA::Layout::Router gRouter;

// in onStart, after define()
std::vector<RDA::Layout::Router::Route> routes;
for (const RDA::State::RouteDesc& declared : RDA::State::routes()) {
    RDA::Layout::Router::Route route{ std::string(declared.name),
                                      std::string(declared.layout), {} };
    for (std::string_view p : declared.params) route.params.emplace_back(p);
    routes.push_back(std::move(route));
}
gRouter.setTransitionMs(200.0f);
gRouter.open(gui.retained(), std::move(routes), "res/layouts", sourceDir());

// once a frame, in onUpdate
gRouter.update();
```

`update()` is called from the application's update rather than from a widget callback: a
swap restructures the tree, and a callback fires while that tree is being walked.

## Threading

`AppConfig::threadMode` decides where the loop runs.

- **`Caller`** — `rendeerRun` blocks until every window closes. The callbacks *are* your
  program.
- **`Owned`** — the engine takes a thread of its own and `rendeerRun` returns. Use
  `rendeerStop()`, `rendeerWait()` and `rendeerRunning()`.

Either way, engine state belongs to the loop thread. From another thread, hand work over
with `RDA::loopWork().request(…)` — which is exactly what the C ABI does for every call it
has.

---

Back to [the backend index](README.md) · [all documentation](../README.md)
