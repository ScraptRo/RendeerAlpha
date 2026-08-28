// The Sandbox theme.
//
// Each export names a widget type; each key inside it is a variant, selected by a widget
// through `variant="..."`. A field left out inherits from that widget's "default"
// variant, so every entry here can be partial.
//
// This was XML, one tag pair per scalar. As TypeScript, sharing a look between variants
// is object spread rather than a `base=` attribute the parser had to implement, colours
// can be computed, and the field names are checked while they are being typed.
//
// The build compiles this to res/themes/sandbox.rdth. Nothing parses TypeScript at
// runtime.

// ---- helpers ---------------------------------------------------------------------
// Ordinary functions, run when the theme is compiled and gone afterwards. Colours are
// "#RGB" / "#RRGGBB" / "#RRGGBBAA", or "r,g,b[,a]".

/** Same colour at a different opacity, as an #RRGGBBAA string. */
function fade(hex: string, alpha: number): string {
  const byte = Math.max(0, Math.min(255, Math.round(alpha * 255)))
  return hex + byte.toString(16).padStart(2, "0").toUpperCase()
}

// ---- buttons ----------------------------------------------------------------------

/** Primary call to action: calm blue, rounded. */
const primary = {
  normal: "#3A6AD0",
  hovered: "#4C7CE6",
  pressed: "#2A54B4",
  text: "#FFFFFF",
  radius: 6,
}

export const button = {
  primary,

  // Destructive action. Spread keeps primary's rounding without naming it.
  danger: {
    ...primary,
    normal: "#B23A3A",
    hovered: "#CC4A4A",
    pressed: "#8E2A2A",
    text: "#FFECEC",
  },

  // Outlined and quiet: a border rather than a filled emphasis.
  ghost: {
    normal: "#22262E",
    hovered: "#2C323C",
    pressed: "#3A6AD0",
    text: "#AFC4E8",
    border: "#4C5A78",
    borderWidth: 1,
    radius: 6,
  },
}

// ---- checkbox, slider, label --------------------------------------------------------

export const checkbox = {
  accent: {
    box: "#2C3A30",
    boxHover: "#3C5040",
    check: "#63D68A",
    label: "#DFF3E4",
  },
}

export const slider = {
  warm: {
    track: "#332A22",
    fill: "#E08A3C",
    knob: "#F0C08A",
    knobActive: "#FFE0B0",
  },
}

export const label = {
  /** Dim caption text for anything secondary. */
  muted: { color: "#8A94A6" },
}

// ---- text fields ---------------------------------------------------------------------

/** One palette, shared by every code editor below. */
const darkPalette = {
  plain: "#D4D4D4",
  keyword: "#C586C0",
  type: "#4EC9B0",
  string: "#CE9178",
  number: "#B5CEA8",
  comment: "#6A9955",
  operator: "#D4D4D4",
  function: "#DCDCAA",
  preprocessor: "#9B9BFF",
}

/** The editor look. `mode: "code"` seeds the multi-line and gutter defaults. */
const editor = {
  mode: "code",
  background: "#101218",
  gutter: "#161922",
  lineNumber: "#5A6478",
  currentLine: fade("#FFFFFF", 0.06),
  selection: "#3C5A96A0",
  caret: "#78AAFF",
  caretWidth: 2,
  border: "#2A303C",
  borderWidth: 1,
  syntax: darkPalette,
}

export const textfield = {
  // Three editors that differ only in which language they highlight. In XML this was
  // base="ide" three times; here the shared part is simply a value.
  ide: { ...editor, language: "python" },
  "ide-cpp": { ...editor, language: "cpp" },
  "ide-js": { ...editor, language: "javascript" },

  /** A read-only log: dimmer than an editor, and without the gutter. */
  "console-out": {
    mode: "document",
    background: "#0C0E13",
    text: "#9AA6B8",
    border: "#2A303C",
    borderWidth: 1,
    padding: 8,
    readOnly: true,
  },

  /** A soft, rounded area for prose. */
  notes: {
    mode: "document",
    background: "#1C1E24",
    text: "#DDE1E8",
    border: "#333A46",
    borderWidth: 1,
    radius: 8,
    padding: 14,
  },
}
