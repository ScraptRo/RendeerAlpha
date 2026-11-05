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
6. **Hand a handler its arguments.** Read `rda_command_args()` -- a JSON array -- inside the
   callback, or right after `rda_poll_command` returns, and call the handler with them; a
   command declared with `params` gets a handler typed from them in the generated module.
   Wrap `rda_call`, which returns a command's result as JSON, and have the generated
   `define()` pass the state code it carries to `rda_load_state_code` -- base64 in the
   generated file is the easy way to carry bytes in a language's source.
7. **Copy the checklist.** `bindings/tests/check.py`, `check.mjs` and `check.cs` ask the
   same questions in the same order; a fourth one asks them again. That is the cheapest way to
   find out what your binding does not do yet, and it is how both of these were finished
   rather than merely written.

Adding a generator is one file beside `PythonStateGen.cpp`, `NodeStateGen.cpp` and
`CSharpStateGen.cpp` in `RendeerAlpha/toolchain/src/Layout/Codegen/` -- using `CodeWriter` for its
string literals, so text with a newline in it comes out valid -- one target in
`Cli/CmdState.cpp` with its line in `Cli/main.cpp`'s command table, and a flag for it in
`Cli/CmdBuild.cpp`, which is how a
project without a build system asks for its state module. The test is a fourth
`check.<ext>` and a few lines in `bindings/tests/CMakeLists.txt`.

---

Back to [the backend index](README.md) · [all documentation](../README.md)
