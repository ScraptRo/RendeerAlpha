# Writing a binding for a new language

The path is the one Python, Node and C# all took:

1. **Find out whether your runtime can be called from a foreign thread**, before anything
   else. It decides the shape of the binding, and it is two lines to check: bind a
   callback with `rda_command_bind`, call `rda_init`, and see whether the process reaches
   the next line. C, C++, Python and C# do -- all four measured rather than assumed; Node
   does not, and hangs.
2. Load the shared library and declare **every** signature. On 64-bit Windows an
   undeclared pointer return is silently cut to 32 bits, which shows up only once an
   allocation lands high in memory.
3. Keep callbacks alive for the life of the process — or, if step 1 said no, use
   `rda_command_watch` and poll from your own loop.
4. Generate the state module from the declaration — do not write a dynamic lookup. The
   whole value of one declaration is that a typo is an error in the language that made it.
5. Wrap `rda_table_set_*` so an application hands over rows and the binding transposes
   them into columns.
6. **Copy the checklist.** `bindings/tests/check.py`, `check.mjs` and `check.cs` ask the
   same questions in the same order; a fourth one asks them again. That is the cheapest way to
   find out what your binding does not do yet, and it is how both of these were finished
   rather than merely written.

Adding a generator is one function beside `emitStatePython` / `emitStateNode` /
`emitStateCSharp` in `RendeerAlpha/src/Layout/StateSchema.cpp`, one subcommand in
`Cli/rda.cpp`, and a flag for it in that file's `build` command, which is how a
project without a build system asks for its state module. The test is a fourth
`check.<ext>` and a few lines in `bindings/tests/CMakeLists.txt`.

---

Back to [the backend index](README.md) · [all documentation](../README.md)
