
function setTimeout(f, ms) {
  return __rda.timer(__rdaOwner, f, Number(ms) || 0, false, Array.prototype.slice.call(arguments, 2));
}
function setInterval(f, ms) {
  return __rda.timer(__rdaOwner, f, Number(ms) || 0, true, Array.prototype.slice.call(arguments, 2));
}
function clearTimeout(id) { __rda.clearTimer(Number(id) || 0); }
function clearInterval(id) { __rda.clearTimer(Number(id) || 0); }
