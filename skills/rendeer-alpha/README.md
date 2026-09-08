# The RendeerAlpha skill

Everything a language model needs to write an interface against this engine, written for
a model rather than for a person. `docs/` explains why the engine is the way it is; this
says what to type and what not to. It is deliberately terse, tabular, and repetitive
where repetition prevents a mistake.

It is sized for a **small local model**. Every page fits in a short context:

| Page | Lines | ≈ tokens | Read it before |
| --- | --- | --- | --- |
| `SKILL.md` | 169 | 1,700 | anything — the rules and a complete example |
| `references/elements.md` | 247 | 2,300 | choosing an element or a property |
| `references/bindings.md` | 132 | 1,200 | writing anything inside `{() => ...}` |
| `references/state.md` | 106 | 950 | adding a signal, command, table or screen |
| `references/theme.md` | 127 | 1,200 | colours, sizes, variants |
| `references/backends.md` | 151 | 1,300 | the C++, Python, Node or C# side |
| `references/recipes.md` | 261 | 2,200 | a list, a form, screens, tabs, docking |
| `references/errors.md` | 82 | 1,400 | looking up a message |

`SKILL.md` plus one reference page is about 4,000 tokens, which leaves room to work in an
8k window. Do not paste all of them at once.

## With a local model, no tools

The way to use `gemma3:12b`, or anything else without tool calling.

1. `SKILL.md` is the **system prompt**.
2. Paste the one reference page the task needs into the conversation.
3. Give the model the project's `res/state.ts` — every name it may use is in there.
4. Compile what it writes, and paste any error straight back. The compiler's messages are
   written to be acted on, and `references/errors.md` maps each to a fix.

Baking the system prompt into an Ollama model, so it is there every time:

```bash
cd skills/rendeer-alpha
{ echo 'FROM gemma3:12b'
  echo 'PARAMETER num_ctx 16384'
  printf 'SYSTEM """'; cat SKILL.md; printf '"""\n'
} > Modelfile
ollama create rda-gemma -f Modelfile
ollama run rda-gemma
```

PowerShell:

```powershell
cd skills\rendeer-alpha
"FROM gemma3:12b`nPARAMETER num_ctx 16384`nSYSTEM """"""$(Get-Content SKILL.md -Raw)""""""" |
  Set-Content Modelfile -Encoding utf8
ollama create rda-gemma -f Modelfile
```

Raise `num_ctx`: Ollama's default of 4k will silently drop the system prompt once the
conversation grows. 16k is comfortable with one reference page loaded.

## With a local model that calls tools

`qwen3:8b` does. Check yours with `ollama show <model>` and look for `tools` under
capabilities. Then the model pulls the page it needs instead of being handed one, and —
the part that matters — it can **compile what it wrote and read the real error** rather
than being told whether it was right.

`mcp/rda_mcp.py` is an MCP server over stdio. No dependencies, Python 3.8+.

```bash
python skills/rendeer-alpha/mcp/rda_mcp.py --selftest
```

That prints where it found the engine, reads every page, compiles one good layout and one
bad one, and says whether it all works. Run it before wiring anything up.

| Tool | What it does |
| --- | --- |
| `rda_reference(page)` | one page of this folder |
| `rda_check(path)` | compile one `.tsx` or theme `.ts`, return the compiler's error. Writes nothing beside the file |
| `rda_build(project, language)` | compile everything under a project's `res/` |
| `rda_project(project)` | every signal, command, table column and screen the project declares, and which layouts are stale |
| `rda_dump(blueprint)` | what a compiled `.rdab` contains, bindings disassembled |

Most MCP clients (Cline, Continue, LM Studio, Open WebUI's MCP bridge) take the same
shape of configuration:

```json
{
  "mcpServers": {
    "rendeer-alpha": {
      "command": "python",
      "args": ["/path/to/RendeerAlpha/skills/rendeer-alpha/mcp/rda_mcp.py"]
    }
  }
}
```

Use an absolute path. The server finds the engine in `RDA_ENGINE`, then in the checkout's
`bin/`, then on `PATH`; add `"env": {"RDA_ENGINE": "/path/to/RendeerAlpha/bin"}` if the
server lives somewhere else.

Ask for the workflow explicitly, because a small model will not invent it:

> Read rda_reference('skill'). Then rda_project on /path/to/MyApp. Write
> res/layouts/home.tsx, run rda_check on it, and fix anything it reports.

## With Claude Code

`SKILL.md` has the frontmatter a skill needs. Either copy the folder into
`.claude/skills/`, or add the MCP server:

```bash
claude mcp add rendeer-alpha -- python /abs/path/to/skills/rendeer-alpha/mcp/rda_mcp.py
```

## Keeping it true

The element and property tables were generated from the engine's own widget schema, and
every rule about bindings was confirmed by compiling the case and reading what the
compiler said. The examples are checked the same way — each complete `.tsx` in these
pages compiles as written.

When the engine's schema changes, regenerate `res/layouts/rda.d.ts` in any project
(`rda build`) and compare it against `references/elements.md`; the `.d.ts` is the
authority, and it is generated from the same table the loader reads.
