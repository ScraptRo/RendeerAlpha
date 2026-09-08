# What is given up, compared with a separate process

An earlier design had the engine in one process and applications connecting to it over a
pipe. It is gone, and the reasoning is in
[the architecture notes](../architecture/bindings.md#out-of-process-hosting-and-why-it-is-gone). The short
version: what a pipe can carry is a *draw list*, so a client in another language is a
client that does its own drawing — which hands another language the pixels while denying
it the layouts, the router, the theme, the bindings and the animation.

What that trade costs is **process isolation**: a backend that crashes now takes the
window with it, exactly as it does for a C++ application today. That was the one thing the
protocol still had going for it, and it was never the thing anyone asked for.

---

Back to [the backend index](README.md) · [all documentation](../README.md)
