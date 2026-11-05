
function __rdaByte(n) {
  n = Math.round(n);
  return n < 0 ? 0 : (n > 255 ? 255 : n);
}
function __rdaHex(n) {
  return __rdaByte(n).toString(16).padStart(2, "0").toUpperCase();
}
function __rdaParse(colour) {
  if (typeof colour !== "string") throw new Error("expected a colour string, got " + typeof colour);
  var s = colour.trim();
  if (s.charAt(0) === "#") s = s.slice(1);
  if (s.length === 3) s = s.charAt(0) + s.charAt(0) + s.charAt(1) + s.charAt(1) + s.charAt(2) + s.charAt(2);
  if (s.length === 6) s = s + "FF";
  if (s.length !== 8 || /[^0-9a-fA-F]/.test(s)) {
    throw new Error("'" + colour + "' is not a colour: expected #RGB, #RRGGBB or #RRGGBBAA");
  }
  return [parseInt(s.slice(0, 2), 16), parseInt(s.slice(2, 4), 16),
          parseInt(s.slice(4, 6), 16), parseInt(s.slice(6, 8), 16)];
}
function rgb(r, g, b) {
  return "#" + __rdaHex(r) + __rdaHex(g) + __rdaHex(b);
}
function rgba(r, g, b, a) {
  return rgb(r, g, b) + __rdaHex(a * 255);
}
function hsl(h, s, l) {
  h = ((h % 360) + 360) % 360 / 360;
  var q = l < 0.5 ? l * (1 + s) : l + s - l * s;
  var p = 2 * l - q;
  function channel(t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1 / 6) return p + (q - p) * 6 * t;
    if (t < 1 / 2) return q;
    if (t < 2 / 3) return p + (q - p) * (2 / 3 - t) * 6;
    return p;
  }
  if (s === 0) return rgb(l * 255, l * 255, l * 255);
  return rgb(channel(h + 1 / 3) * 255, channel(h) * 255, channel(h - 1 / 3) * 255);
}
function fade(colour, alpha) {
  var c = __rdaParse(colour);
  return "#" + __rdaHex(c[0]) + __rdaHex(c[1]) + __rdaHex(c[2]) + __rdaHex(alpha * 255);
}
function mix(a, b, t) {
  var x = __rdaParse(a), y = __rdaParse(b);
  var k = t < 0 ? 0 : (t > 1 ? 1 : t);
  return "#" + __rdaHex(x[0] + (y[0] - x[0]) * k)
             + __rdaHex(x[1] + (y[1] - x[1]) * k)
             + __rdaHex(x[2] + (y[2] - x[2]) * k)
             + __rdaHex(x[3] + (y[3] - x[3]) * k);
}
function lighten(colour, amount) { return mix(colour, "#FFFFFFFF", amount); }
function darken(colour, amount)  { return mix(colour, "#000000FF", amount); }
function alphaOf(colour) { return __rdaParse(colour)[3] / 255; }
