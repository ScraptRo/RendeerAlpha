# The tutorial series

Six pages that teach the engine in order, from what it is to a backend in four languages.
They are the gentler path through the same material the rest of `docs/` covers as
reference.

| | |
| --- | --- |
| [What RendeerAlpha is](what-it-is.md) | the shape of the whole thing before any details |
| [Installing the engine](install.md) | what you need, and the one bring-up script |
| [Adding it to a project](project-setup.md) | the four project shapes |
| [Your first interface](first-interface.md) | the three files under `res/` |
| [The layout language](layout-language.md) | constants, bindings, handlers, sizing, elements |
| [State and backends](backends.md) | one declaration, four generated sides |

## These are published, and fetched as they are

The website that shows this series reads these files **at run time**, straight from this
repository's `main` branch on GitHub. A correction that reaches it is live there within
GitHub's cache window, with nothing rebuilt and nothing redeployed -- which is the whole
reason the content lives beside the code instead of in the site. A correction on `dev` is
not live until it has been merged and pushed.

That costs one rule: `index.json` is the series, and a page is only in it if it is listed
there. `slug` names the page in a URL, `file` is the markdown beside it, `title`, `tagline`
and `minutes` are what the index page shows, and `summary` opens the page itself.

## What the markdown may contain

A reader renders these into the site's own blocks, so the dialect is small and fixed:

| | |
| --- | --- |
| `## A heading` | starts a section, and becomes a link target |
| a paragraph | wrapped lines are joined |
| `- an item` | a list |
| `\| a \| b \|` | a two-column table, with the usual `\| --- \|` row for a header |
| `> a note` | drawn as a callout |
| a fenced block | ` ```tsx title="res/layouts/home.tsx" ` -- the title is the file label |

Inline, `` `code` ``, `**bold**`, `*emphasis*` and `[text](https://…)` all work, and a link
to another `.md` in `docs/` opens that page on GitHub. Anything else -- images, nested lists, three-column tables, headings below `##` -- has nowhere to go
and will come out as plain text.

---

Back to [all documentation](../README.md)
