// What the C# binding does, checked against a running engine.
//
// Run by ctest as `rda_csharp_binding`. It opens a real window on a real device, because
// that is what the thing under test does -- a binding that cannot be exercised without
// one is a binding whose bugs only show up in an application.
//
// The checklist here, in check.py and in check.mjs are deliberately the same, in the same
// order. Three bindings over one ABI should be able to answer the same questions, and a
// line that appears in two outputs and not the third is a gap worth seeing.

using System;
using System.Collections.Generic;
using System.Text;
using System.Threading;
using Rendeer;
using RdaState;

static class Check {
	static readonly List<string> Failures = new List<string>();

	// A value as one short, printable line. Escaped, because this console is not UTF-8
	// and would render a correct round trip as question marks -- which reads exactly like
	// the failure one of these checks exists to catch.
	static string Shown(object? value) {
		string text = value switch {
			null => "null",
			bool b => b ? "true" : "false",
			string s => "\"" + s + "\"",
			_ => value.ToString() ?? "",
		};
		var escaped = new StringBuilder();
		foreach (char c in text) {
			if (c < 128) escaped.Append(c);
			else escaped.Append("\\u").Append(((int)c).ToString("x4"));
		}
		text = escaped.ToString();
		return text.Length <= 60 ? text : text.Substring(0, 57) + "... (" + text.Length + " chars)";
	}

	static void Report(string what, object? got, object? want) {
		bool ok = Shown(got) == Shown(want);
		Console.WriteLine((ok ? "  ok    " : "  FAIL  ") + what + " -> " + Shown(got)
			+ (ok ? "" : " (wanted " + Shown(want) + ")"));
		Console.Out.Flush();
		if (!ok) Failures.Add(what);
	}

	// A call that must fail, and must say so rather than quietly doing nothing.
	static void Refused(string what, Action work) {
		try {
			work();
			Report(what, "accepted", "refused");
		} catch (RdaException) {
			Console.WriteLine("  ok    " + what + " -> RdaException");
			Console.Out.Flush();
		}
	}

	static int Main() {
		// ---- lifecycle ----
		Console.WriteLine("lifecycle");
		Rda.Init(new StartupConfig { Name = "c# binding checks" });
		Report("init returns with the engine up", Rda.Running, true);

		Schema.Define();
		Rda.LoadInterface("res/layouts/first.rdab");
		Report("an interface loads", true, true);
		Refused("a blueprint that is not there is refused",
			() => Rda.LoadInterface("res/layouts/nope.rdab"));

		// ---- signals ----
		Console.WriteLine("signals");
		State.count = 41;
		Report("numbers", State.count, 41.0);
		State.count += 1;
		Report("read-modify-write", State.count, 42.0);
		State.flag = true;
		Report("booleans", State.flag, true);
		State.text = "written";
		Report("text", State.text, "written");
		const string accented = "éàü — non-ascii";
		State.text = accented;
		Report("text is utf-8 on the way out and back", State.text, accented);

		// Longer than the buffer the binding guesses at, so the length-then-retry runs.
		string longText = new string('x', 5000);
		State.note = longText;
		Report("text longer than the guess comes back whole", State.note, longText);
		Report("and is the length it should be", State.note.Length, 5000);

		// The other two bindings check that a misspelled signal is refused at run time.
		// Here it cannot be reached: `State.notAThing` is not a member, so the line does
		// not compile -- which is the same guarantee, collected earlier.
		Console.WriteLine("  ok    a misspelled signal is refused -> at compile time");

		// ---- tables ----
		Console.WriteLine("tables");
		Tables.Items.Fill(new[] {
			new Tables.Items.Row { label = "one", value = 1.5, on = true },
			new Tables.Items.Row { label = "two", value = 2.5, on = false },
		});
		Report("fill sets the count", Tables.Items.Rows, 2);
		Report("text came back", Tables.Items.label(0), "one");
		Report("numbers came back", Tables.Items.value(1), 2.5);
		Report("booleans came back", Tables.Items.on(0), true);
		Report("and a row that was not set is false", Tables.Items.on(1), false);

		// The row shapes the other bindings take at run time are one shape here, and the
		// compiler checks it. Both lines below are the same check, kept so the three
		// outputs line up.
		Tables.Items.Fill(new List<Tables.Items.Row> {
			new Tables.Items.Row { label = "list", value = 3.0, on = true } });
		Report("a sequence in column order works", Tables.Items.label(0), "list");
		Tables.Items.Fill(new[] { new Tables.Items.Row { label = "struct", value = 4.0, on = false } });
		Report("an object with those attributes works", Tables.Items.label(0), "struct");

		Tables.Items.Rows = 5;
		Report("resizing grows it", Tables.Items.Rows, 5);
		Report("and the new rows are empty", Tables.Items.label(4), "");
		Tables.Items.setLabel(new[] { "a", "b" }, 3);
		Report("a run can start anywhere", Tables.Items.label(3), "a");
		Report("and leaves the row before it alone", Tables.Items.label(2), "");

		Refused("a row past the end is refused", () => Tables.Items.label(999));

		var many = new List<Tables.Items.Row>(10000);
		for (int i = 0; i < 10000; ++i) {
			many.Add(new Tables.Items.Row { label = "r" + i, value = i, on = i % 2 == 0 });
		}
		Tables.Items.Fill(many);
		Report("ten thousand rows land", Tables.Items.Rows, 10000);
		Report("the last of them is right", Tables.Items.label(9999), "r9999");

		// ---- screens ----
		Console.WriteLine("screens");
		Rda.SetTransitionMs(0);          // instant, so a check cannot race a cross-fade
		Rda.OpenRoutes(Routes.All, "res/layouts");
		Thread.Sleep(500);
		Report("the first declared route is where it opens", State.route, "first");
		Report("with nothing to go back to", Rda.CanGoBack, false);

		State.route = "second";
		Thread.Sleep(500);
		Report("writing the signal navigates", State.route, "second");
		Report("and that is history", Rda.CanGoBack, true);
		Report("back returns", Rda.Back(), true);
		Thread.Sleep(500);
		Report("to where it was", State.route, "first");
		Report("and forward is open", Rda.CanGoForward, true);
		Report("forward returns", Rda.Forward(), true);
		Thread.Sleep(500);
		Report("to where it went", State.route, "second");

		// ---- commands ----
		Console.WriteLine("commands");
		var asked = new List<double>();
		Commands.bump(() => asked.Add(State.count));

		// Like Python and unlike Node: the CLR takes a call from the engine's thread, so
		// by the time Invoke returns the handler has already run.
		State.count = 7;
		Rda.Invoke("bump");
		Report("invoke runs the handler", asked.Count == 1 ? asked[0] : -1, 7.0);

		Refused("invoking a command nothing is bound to is refused", () => Rda.Invoke("unused"));
		Refused("invoking a command that does not exist is refused", () => Rda.Invoke("nope"));

		// ---- the theme ----
		Console.WriteLine("theme");
		Refused("changing to a theme that is not there is refused",
			() => Rda.SetTheme("layouts/nope.rdth"));
		Refused("changing to a theme with no name is refused", () => Rda.SetTheme(""));

		// ---- the viewport ----
		// The second screen is showing, and it has <viewport name="canvas"> on it.
		Console.WriteLine("viewport");
		Thread.Sleep(300);
		var canvas = Rda.ViewportSize("canvas");
		Report("a placed viewport reports its size", canvas.Width + "x" + canvas.Height, "400x120");
		var nowhere = Rda.ViewportSize("nowhere");
		Report("one nothing has drawn is zero", nowhere.Width + "x" + nowhere.Height, "0x0");

		var drawing = new Drawing();
		drawing.Clear("#11141A");
		drawing.Rect(8, 8, 80, 40, "#3A6AD0", 4);
		drawing.Line(0, 0, canvas.Width, canvas.Height, "#4C7CE6", 2);
		drawing.Circle(200, 60, 24, "#6ED09C");
		drawing.Text(12, 60, "drawn from C#", "#DCE0E7", 12);
		Report("five shapes built", drawing.Count, 5);
		Rda.Draw("canvas", drawing);
		Thread.Sleep(300);
		Report("the drawing was accepted", Rda.Running, true);

		Rda.Draw("canvas", new Drawing());   // an empty one takes it back down
		Refused("a colour that is not one is refused", () => new Drawing().Clear("mauve"));
		Refused("drawing in a viewport without a name is refused", () => Rda.Draw("", drawing));

		// ---- shutdown ----
		Console.WriteLine("shutdown");
		Rda.Stop();
		Rda.Wait();
		Report("the engine stops", Rda.Running, false);

		Console.WriteLine();
		Console.WriteLine(Failures.Count == 0
			? "c# binding: all checks passed"
			: "FAILED: " + string.Join(", ", Failures));
		return Failures.Count == 0 ? 0 : 1;
	}
}
