// The look. Each export names a widget type, each key inside it is a variant, and a widget
// picks one with variant="...". A field left out inherits from that type's `default`.
export const button: RdaButtonTheme = {
  default: { transitionMs: 140, easing: "out" },
  primary: {
    normal: "#3A6AD0", hovered: "#4C7CE6", pressed: "#2A54B4",
    text: "#FFFFFF", radius: 6,
  },
}

export const label: RdaLabelTheme = {
  heading: { color: "#DDE3EC", fontSize: 22, weight: "bold" },
}
