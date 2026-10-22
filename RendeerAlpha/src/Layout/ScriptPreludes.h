#pragma once

// JavaScript the engine supplies to code it did not write: `format`, and the four timer
// functions. Internal to the layout tools and the script host, like ModuleTransform.h.
//
// Each is compiled into a run-time module only when the module's code names it, the way
// the colour helpers are, so a layout that formats nothing carries none of it. And the
// same `format` is defined while a layout or state.ts is built, so a top-level
// `format.bytes(2048)` gives at build what it would give later -- the code is one text.
//
// JavaScript rather than C++ for that reason, and because what it does is string work
// QuickJS already does well. QuickJS has no Intl, which is why any of this exists: an
// interface wants grouped digits, sizes, dates and "3 minutes ago", and toLocaleString
// gives none of them.
namespace RDA::Layout {

	inline const char* formatPrelude() {
		return R"JS(
var format = (function () {
  "use strict";
  var MONTHS = ["January", "February", "March", "April", "May", "June", "July", "August",
                "September", "October", "November", "December"];
  var DAYS = ["Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"];
  var SIZES = ["B", "KB", "MB", "GB", "TB", "PB"];

  function pad(n, width) {
    var s = String(Math.abs(Math.trunc(n)));
    while (s.length < width) s = "0" + s;
    return s;
  }
  function group(digits, separator) {
    var out = "";
    for (var i = 0; i < digits.length; i++) {
      if (i > 0 && (digits.length - i) % 3 === 0) out += separator;
      out += digits.charAt(i);
    }
    return out;
  }
  function options(o) { return o !== null && typeof o === "object" ? o : {}; }

  // 1234567.891 -> "1,234,567.891". Up to three decimals unless told how many, the way a
  // person writes a number rather than the way a double prints.
  function number(x, o) {
    o = options(o);
    x = Number(x);
    if (x !== x) return "NaN";
    if (x === Infinity) return "∞";
    if (x === -Infinity) return "-∞";
    var text;
    if (o.decimals === undefined || o.decimals === null) {
      text = Math.abs(x).toFixed(3);
      if (text.indexOf(".") >= 0) text = text.replace(/0+$/, "").replace(/\.$/, "");
    } else {
      text = Math.abs(x).toFixed(Math.max(0, Math.min(20, Math.trunc(Number(o.decimals)) || 0)));
    }
    if (/e/.test(text)) return (x < 0 ? "-" : "") + text;
    var dot = text.indexOf(".");
    var whole = dot < 0 ? text : text.slice(0, dot);
    var fraction = dot < 0 ? "" : text.slice(dot + 1);
    if (o.grouping !== false) whole = group(whole, o.group === undefined ? "," : String(o.group));
    var out = whole + (fraction ? (o.point === undefined ? "." : String(o.point)) + fraction : "");
    // A negative that rounds to zero is zero: -0.001 at two decimals is "0.00", not "-0.00".
    var negative = x < 0 && /[1-9]/.test(text);
    return (negative ? "-" : (o.sign && x > 0 ? "+" : "")) + out;
  }

  // 0.256 -> "26%". A fraction of one, which is how a value usually arrives.
  function percent(x, o) {
    o = options(o);
    var copy = {};
    for (var k in o) copy[k] = o[k];
    if (copy.decimals === undefined) copy.decimals = 0;
    return number(Number(x) * 100, copy) + "%";
  }

  // 1536 -> "1.5 KB". Kilo is 1024 unless { si: true }, which makes it 1000 and "kB".
  function bytes(n, o) {
    o = options(o);
    n = Number(n);
    if (!isFinite(n)) return String(n);
    var base = o.si ? 1000 : 1024, v = Math.abs(n), i = 0;
    while (v >= base && i < SIZES.length - 1) { v /= base; i++; }
    var given = o.decimals !== undefined && o.decimals !== null;
    var text = number(n < 0 ? -v : v, { decimals: given ? o.decimals : (i === 0 ? 0 : 1) });
    if (!given) text = text.replace(/\.0$/, "");
    var unit = SIZES[i];
    if (o.si && unit === "KB") unit = "kB";
    return text + " " + unit;
  }

  // 3723000 ms -> "1:02:03", or { style: "short" } -> "1h 2m". What a timer or a player
  // shows. Milliseconds, as Date.now() and setTimeout count.
  function duration(ms, o) {
    o = options(o);
    ms = Number(ms);
    if (!isFinite(ms)) return "";
    var negative = ms < 0, total = Math.floor(Math.abs(ms) / 1000);
    var h = Math.floor(total / 3600), m = Math.floor(total / 60) % 60, s = total % 60;
    var out;
    if (o.style === "short") {
      var parts = [];
      if (h) parts.push(h + "h");
      if (m && parts.length < 2) parts.push(m + "m");
      if ((s || !parts.length) && parts.length < 2 && !h) parts.push(s + "s");
      out = parts.join(" ");
    } else {
      out = (h ? h + ":" + pad(m, 2) : String(m)) + ":" + pad(s, 2);
    }
    return (negative ? "-" : "") + out;
  }

  function toDate(t) {
    if (t instanceof Date) return t;
    return new Date(typeof t === "string" ? Date.parse(t) : Number(t));
  }

  // A date, by pattern: yyyy yy, M MM MMM MMMM, d dd, E EEE EEEE, H HH, h hh, m mm,
  // s ss, S SSS, a; 'quoted text' as it is. Local time unless { utc: true }. Names are
  // English unless the options give { months, days, am, pm }.
  function date(t, pattern, o) {
    o = options(o);
    var d = toDate(t);
    if (isNaN(d.getTime())) return "";
    var utc = !!o.utc;
    var Y = utc ? d.getUTCFullYear() : d.getFullYear();
    var M = utc ? d.getUTCMonth() : d.getMonth();
    var D = utc ? d.getUTCDate() : d.getDate();
    var W = utc ? d.getUTCDay() : d.getDay();
    var H = utc ? d.getUTCHours() : d.getHours();
    var N = utc ? d.getUTCMinutes() : d.getMinutes();
    var S = utc ? d.getUTCSeconds() : d.getSeconds();
    var L = utc ? d.getUTCMilliseconds() : d.getMilliseconds();
    var months = Array.isArray(o.months) && o.months.length === 12 ? o.months : MONTHS;
    var days = Array.isArray(o.days) && o.days.length === 7 ? o.days : DAYS;
    function token(c, n) {
      switch (c) {
        case "y": return n === 2 ? pad(Y % 100, 2) : pad(Y, Math.max(n, 4));
        case "M": return n >= 4 ? months[M] : n === 3 ? String(months[M]).slice(0, 3) : n === 2 ? pad(M + 1, 2) : String(M + 1);
        case "d": return n >= 2 ? pad(D, 2) : String(D);
        case "E": return n >= 4 ? days[W] : String(days[W]).slice(0, 3);
        case "H": return n >= 2 ? pad(H, 2) : String(H);
        case "h": { var h12 = H % 12 || 12; return n >= 2 ? pad(h12, 2) : String(h12); }
        case "m": return n >= 2 ? pad(N, 2) : String(N);
        case "s": return n >= 2 ? pad(S, 2) : String(S);
        case "S": return pad(L, 3).slice(0, Math.min(n, 3));
        case "a": return H < 12 ? (o.am === undefined ? "AM" : String(o.am)) : (o.pm === undefined ? "PM" : String(o.pm));
      }
      return "";
    }
    pattern = pattern === undefined || pattern === null ? "yyyy-MM-dd HH:mm" : String(pattern);
    var out = "", i = 0;
    while (i < pattern.length) {
      var c = pattern.charAt(i);
      if (c === "'") {
        var end = pattern.indexOf("'", i + 1);
        if (end < 0) end = pattern.length;
        out += end === i + 1 ? "'" : pattern.slice(i + 1, end);
        i = end + 1;
        continue;
      }
      if ("yMdEHhmsSa".indexOf(c) >= 0) {
        var j = i;
        while (j < pattern.length && pattern.charAt(j) === c) j++;
        out += token(c, j - i);
        i = j;
        continue;
      }
      out += c;
      i++;
    }
    return out;
  }

  // "just now", "5 minutes ago", "in 2 hours", "yesterday". Against Date.now() unless
  // given `now`. Worked out when the binding runs: something that has to stay current
  // reads a signal that moves, which a timer can keep moving.
  function relative(t, now) {
    var at = toDate(t).getTime();
    if (isNaN(at)) return "";
    var base = now === undefined || now === null ? Date.now() : toDate(now).getTime();
    var seconds = (at - base) / 1000, past = seconds <= 0, s = Math.abs(seconds);
    if (s < 45) return past ? "just now" : "in a moment";
    var steps = [[60, "minute"], [3600, "hour"], [86400, "day"], [604800, "week"],
                 [2629800, "month"], [31557600, "year"]];
    var limits = [45 * 60, 22 * 3600, 6.5 * 86400, 4 * 604800, 11 * 2629800, Infinity];
    var k = 0;
    while (k < steps.length - 1 && s >= limits[k]) k++;
    var v = Math.max(1, Math.round(s / steps[k][0])), unit = steps[k][1];
    if (unit === "day" && v === 1) return past ? "yesterday" : "tomorrow";
    var text = v + " " + unit + (v === 1 ? "" : "s");
    return past ? text + " ago" : "in " + text;
  }

  return Object.freeze({ number: number, percent: percent, bytes: bytes, duration: duration,
                         date: date, relative: relative });
})();
)JS";
	}

	// The timers, as a run-time module gets them: each asks the engine, naming the module
	// it belongs to, so a module's timers go with it when it is unloaded -- a reloaded
	// layout does not keep running the old one's.
	inline const char* timerPrelude() {
		return R"JS(
function setTimeout(f, ms) {
  return __rda.timer(__rdaOwner, f, Number(ms) || 0, false, Array.prototype.slice.call(arguments, 2));
}
function setInterval(f, ms) {
  return __rda.timer(__rdaOwner, f, Number(ms) || 0, true, Array.prototype.slice.call(arguments, 2));
}
function clearTimeout(id) { __rda.clearTimer(Number(id) || 0); }
function clearInterval(id) { __rda.clearTimer(Number(id) || 0); }
)JS";
	}

	// The same names while a layout or state.ts is built, where nothing runs later: the
	// top level runs once, and a timer started there would have nowhere to go off.
	inline const char* buildTimerPrelude() {
		return R"JS(
function __rdaNoTimer() {
  throw new Error("a timer goes off while the application runs, and the top level of a file runs " +
                  "once, while it is built. Start it from a handler or an action");
}
var setTimeout = __rdaNoTimer, setInterval = __rdaNoTimer;
function clearTimeout() {}
function clearInterval() {}
)JS";
	}
}
