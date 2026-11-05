// For the rda_theme_error_line test: a theme that throws while it is built. It used to say
// the message alone, and the colour helpers written in front of the theme put its lines off
// by their length besides. The error has to say this file's line: 10.
type Palette = { accent: string }

const palette: Palette | undefined = undefined

export const button = {
  primary: {
    background: palette!.accent,
  },
}
