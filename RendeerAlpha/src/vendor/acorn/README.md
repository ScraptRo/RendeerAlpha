# acorn

[acorn](https://github.com/acornjs/acorn) 8.19.0, `dist/acorn.js` from the npm package,
unmodified. MIT, see `LICENSE`.

Build-time only. The layout compiler runs it inside the QuickJS it already has, to read a
bundled layout's top-level declarations and the names each function refers to -- what
QuickJS can compile but cannot report. It is embedded into the authoring half of the
engine as bytes (see `RendeerAlpha/CMakeLists.txt`), so it is linked only where the layout
compiler is, and nothing at run time ever calls it.

To update: `npm pack acorn@<version>`, copy `package/dist/acorn.js` and `package/LICENSE`
here, and change the version above.
