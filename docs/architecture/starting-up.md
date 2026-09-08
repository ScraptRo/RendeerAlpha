# Starting up

```mermaid
sequenceDiagram
    participant main as main.cpp
    participant eng as engineMain
    participant gpu as engineBringUp
    participant host as LayoutHost
    participant sig as Signals

    main->>eng: rendeerRun(config)
    eng->>gpu: engineBringUp(config)
    gpu->>gpu: GLFW window (1280x800)
    gpu->>gpu: Vulkan device, swapchain, renderer
    gpu->>gpu: font atlas → gui.init()
    gpu->>gpu: syntax languages, then theme (.rdth)
    gpu->>gpu: clipboard handlers
    eng->>eng: loopWork().setServiceThread()
    Note over eng: gRunning = true happens BEFORE onStart,<br/>so a startup that calls rendeerStop() is obeyed
    eng->>main: config.onStart()
    main->>sig: State::define()
    Note over main,sig: state first — a binding resolves what it reads<br/>when it is created, so the signal must exist
    main->>host: open(gui.retained(), "hello.rdab", source)
    host->>host: read blueprint, instantiate widgets
    host->>sig: resolve signal names, register bindings
    host->>host: attach onClick / onChange handlers
    eng->>eng: enter the frame loop
```

Two ordering rules in there are load-bearing and were both bugs once:

- `gRunning = true` is set **before** `onStart`, so a startup that decides to stop is not
  overwritten by the loop raising the flag afterwards.
- `State::define()` runs **before** the layout opens, because a binding resolves the
  signals it reads at creation time.

---

Back to [the architecture index](README.md) · [all documentation](../README.md)
