
function __rdaNoTimer() {
  throw new Error("a timer goes off while the application runs, and the top level of a file runs " +
                  "once, while it is built. Start it from a handler or an action");
}
var setTimeout = __rdaNoTimer, setInterval = __rdaNoTimer;
function clearTimeout() {}
function clearInterval() {}
