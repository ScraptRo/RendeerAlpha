// rda -- write a RendeerAlpha backend in C#.
//
//     using Rendeer;
//
//     Rda.Init(new StartupConfig { Name = "My application", Theme = "res/themes/app.rdth" });
//     Schema.Define();
//     Rda.LoadInterface("res/layouts/home.rdab");
//
//     Commands.save(() => Console.WriteLine(State.notes));
//
//     Rda.Wait();
//
// C# is a *backend* here, which is what the engine is for. The interface is a compiled
// layout -- written in TSX, checked against a generated schema, styled by a theme, bound
// to signals and animated by the engine. C# does not draw it; it loads it, writes the
// state it reads, and answers the work it asks for. That is the same job C++ has, and the
// layout cannot tell which of them is behind it.
//
// ---- threading ----
//
// Init() starts the engine on a thread of its own and returns once it is up -- the window
// and the renderer ready, and OnStart finished -- so the line after it may write state and
// expect it to exist. The process stays yours; Wait() is for when there is nothing left to
// do but let the window live.
//
// Every call here is safe from any thread. Engine state belongs to the loop thread, so
// each call hands its work across and waits, which costs a frame and is the right price
// for a backend. Handlers run *on* the loop thread -- the CLR takes a call from a foreign
// thread happily, which is why a command here can be an ordinary delegate rather than the
// queue Node needs.

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace Rendeer {

	/// <summary>What the engine needs to start. Every field is optional.</summary>
	public sealed class StartupConfig {
		/// <summary>The window's title.</summary>
		public string Name = "RendeerAlpha";
		/// <summary>A compiled theme (.rdth). Empty keeps the engine's built-in look.</summary>
		public string Theme = "";
		/// <summary>Syntax languages beyond the built-in "python".</summary>
		public string Languages = "";
		/// <summary>Cap the loop to the display's refresh rate.</summary>
		public bool Vsync = true;
		/// <summary>A .ttf to draw the interface in. Empty keeps the engine's own.</summary>
		public string Font = "";
		/// <summary>Pixel height for body text. Zero keeps the default.</summary>
		public float FontHeight = 0.0f;

		/// <summary>Once, after the window is up and before Init() returns.</summary>
		public Action? OnStart;
		/// <summary>Once per frame, before it is drawn, with the seconds since the last.</summary>
		public Action<float>? OnUpdate;
		/// <summary>Once, after the loop ends.</summary>
		public Action? OnShutdown;
	}

	/// <summary>One screen: its name, the layout it shows, and its parameters.</summary>
	public readonly struct Route {
		/// <summary>What `state.route` holds while this screen is showing.</summary>
		public readonly string Name;
		/// <summary>The blueprint stem, without a directory or an extension.</summary>
		public readonly string Layout;
		/// <summary>
		/// Signals that are this screen's arguments. Going back restores them along with
		/// the route, which is the whole reason they are named.
		/// </summary>
		public readonly string[] Params;

		/// <summary>One screen, as the generated route table gives it.</summary>
		public Route(string name, string layout, string[]? parameters = null) {
			Name = name;
			Layout = layout;
			Params = parameters ?? Array.Empty<string>();
		}
	}

	/// <summary>
	/// The engine, from C#. Start it, show an interface, and answer what it asks for.
	/// </summary>
	public static class Rda {
		private static bool started;

		/// <summary>
		/// Starts the engine and returns once it is up.
		///
		/// Up means the window and the renderer are ready and OnStart has finished, so the
		/// line after this may declare state and load an interface. The engine runs on a
		/// thread of its own, which is why this returns at all.
		/// </summary>
		public static void Init(StartupConfig? config = null) {
			if (started) throw new RdaException("the engine is already running");
			// Before the first call that assumes anything about the library's contents.
			Engine.CheckAbi();
			config ??= new StartupConfig();

			IntPtr handle = Engine.rda_config_new();
			if (handle == IntPtr.Zero) throw new RdaException("cannot allocate a config");
			try {
				Engine.rda_config_set_name(handle, config.Name ?? "RendeerAlpha");
				if (!string.IsNullOrEmpty(config.Theme)) Engine.rda_config_set_theme(handle, config.Theme);
				if (!string.IsNullOrEmpty(config.Languages)) Engine.rda_config_set_languages(handle, config.Languages);
				Engine.rda_config_set_vsync(handle, config.Vsync ? 1 : 0);
				if (!string.IsNullOrEmpty(config.Font) || config.FontHeight > 0.0f) {
					Engine.rda_config_set_font(handle, config.Font ?? "", config.FontHeight);
				}

				// Held for the life of the process: the engine calls these long after this
				// method has returned, and the collector cannot see that it is holding them.
				if (config.OnStart != null) {
					Action onStart = config.OnStart;
					Engine.Callback trampoline = _ => onStart();
					Engine.Keep(trampoline);
					Engine.rda_config_on_start(handle, trampoline, IntPtr.Zero);
				}
				if (config.OnUpdate != null) {
					Action<float> onUpdate = config.OnUpdate;
					Engine.UpdateCallback trampoline = (dt, _) => onUpdate(dt);
					Engine.Keep(trampoline);
					Engine.rda_config_on_update(handle, trampoline, IntPtr.Zero);
				}
				if (config.OnShutdown != null) {
					Action onShutdown = config.OnShutdown;
					Engine.Callback trampoline = _ => onShutdown();
					Engine.Keep(trampoline);
					Engine.rda_config_on_shutdown(handle, trampoline, IntPtr.Zero);
				}

				if (Engine.rda_init(handle) == 0) Engine.Fail("the engine did not start");
				started = true;
			} finally {
				// rda_init copies what it needs, so this is freed on both paths.
				Engine.rda_config_free(handle);
			}
		}

		/// <summary>
		/// Shows a compiled layout (.rdab), replacing whatever was showing.
		///
		/// <paramref name="source"/> is the .tsx it came from. Where the build can compile
		/// one, naming it is what lets the engine reload on a change.
		/// </summary>
		public static void LoadInterface(string blueprint, string source = "") {
			if (Engine.rda_load_interface(blueprint, source ?? "") == 0)
				Engine.Fail("cannot load '" + blueprint + "'");
		}

		/// <summary>
		/// Shows a set of screens, one at a time, with `state.route` naming which.
		///
		/// The order is the order they were declared: the first is where the application
		/// opens unless `route` already says otherwise. After this, navigating is an
		/// ordinary write -- <c>State.route = "catalogue"</c>.
		/// </summary>
		public static void OpenRoutes(Route[] routes, string layoutDir, string sourceDir = "") {
			if (routes == null || routes.Length == 0) throw new RdaException("no routes to open");

			int count = routes.Length;
			IntPtr[] names = new IntPtr[count];
			IntPtr[] layouts = new IntPtr[count];
			IntPtr[] parameters = new IntPtr[count];
			try {
				for (int i = 0; i < count; ++i) {
					names[i] = Engine.Utf8(routes[i].Name);
					layouts[i] = Engine.Utf8(routes[i].Layout);
					parameters[i] = Engine.Utf8(string.Join(",", routes[i].Params));
				}
				if (Engine.rda_open_routes(names, layouts, parameters, count,
						layoutDir, sourceDir ?? "") == 0) {
					Engine.Fail("cannot open the screens");
				}
			} finally {
				for (int i = 0; i < count; ++i) {
					if (names[i] != IntPtr.Zero) Marshal.FreeHGlobal(names[i]);
					if (layouts[i] != IntPtr.Zero) Marshal.FreeHGlobal(layouts[i]);
					if (parameters[i] != IntPtr.Zero) Marshal.FreeHGlobal(parameters[i]);
				}
			}
		}

		/// <summary>How long one screen takes to cross-fade into the next. Zero is instant.</summary>
		public static void SetTransitionMs(float ms) => Engine.rda_set_transition_ms(ms);

		/// <summary>One step back through the screens. False when there is nowhere to go.</summary>
		public static bool Back() => Engine.rda_route_back() != 0;
		/// <summary>One step forward again. False when there is nowhere to go.</summary>
		public static bool Forward() => Engine.rda_route_forward() != 0;
		/// <summary>Whether there is a screen behind this one.</summary>
		public static bool CanGoBack => Engine.rda_route_can_go_back() != 0;
		/// <summary>Whether a Back() has left somewhere to return to.</summary>
		public static bool CanGoForward => Engine.rda_route_can_go_forward() != 0;

		/// <summary>Whether the window is still open.</summary>
		public static bool Running => Engine.rda_running() != 0;

		/// <summary>Asks the engine to close. Safe from anywhere, including before Init().</summary>
		public static void Stop() => Engine.rda_stop();

		/// <summary>Blocks until the engine has stopped and its thread has been joined.</summary>
		public static void Wait() {
			Engine.rda_wait();
			started = false;
		}

		/// <summary>
		/// Asks for a command as if the interface had.
		///
		/// For when the backend is the one that wants the work: a menu, a hotkey, a
		/// scheduled job. The handler runs where it always runs, so it cannot tell who
		/// asked. Refused, rather than ignored, when nothing is bound to that name.
		/// </summary>
		public static void Invoke(string command) => Engine.Invoke(command);

		/// <summary>Binds a handler to a declared command, by name.</summary>
		/// <remarks>
		/// The generated <c>Commands</c> class is the typed way to do this; use it unless
		/// the name is only known at run time.
		/// </remarks>
		public static void OnCommand(string command, Action handler) =>
			Engine.OnCommand(command, handler);

		/// <summary>Swaps the whole look while the window stays open.</summary>
		/// <remarks>
		/// The compiled <c>.rdth</c> replaces what was there rather than layering on top,
		/// so a theme that does not mention a variant does not inherit the last one's.
		/// Colours and radii travel to their new values over the transitionMs the new
		/// theme gives them; a theme whose variants say 0 swaps instantly.
		/// </remarks>
		public static void SetTheme(string path) {
			if (Engine.rda_set_theme(path) == 0) Engine.Fail("cannot use the theme '" + path + "'");
		}

		/// <summary>
		/// Puts <paramref name="drawing"/> in the &lt;viewport name="..."&gt; and leaves
		/// it there.
		/// </summary>
		/// <remarks>
		/// An empty <see cref="Drawing"/> takes it back down; a viewport nothing has ever
		/// drawn into shows the engine's own 3D scene instead.
		/// </remarks>
		public static void Draw(string name, Drawing drawing) {
			if (drawing == null) throw new RdaException("Draw() takes a Drawing");
			drawing.Send(name);
		}

		/// <summary>
		/// The size the viewport was last laid out at, in the pixels a drawing uses. Both
		/// zero until a frame has placed it.
		/// </summary>
		public static (float Width, float Height) ViewportSize(string name = "main") {
			if (Engine.rda_viewport_size(name, out float width, out float height) == 0)
				Engine.Fail("cannot measure '" + name + "'");
			return (width, height);
		}
	}

	/// <summary>
	/// What to draw in a &lt;viewport&gt;, in its own coordinates: 0,0 is its top-left
	/// corner.
	/// </summary>
	/// <remarks>
	/// Built up here and sent in one call, because every call into the engine crosses to
	/// its loop thread and waits -- a shape at a time would be a frame at a time. What is
	/// sent stays on screen until another drawing replaces it.
	/// <code>
	/// var d = new Drawing();
	/// d.Clear("#11141A");
	/// for (int i = 0; i &lt; values.Length; i++)
	///     d.Rect(i * 12, height - values[i], 8, values[i], "#3A6AD0", radius: 2);
	/// Rda.Draw("chart", d);
	/// </code>
	/// </remarks>
	public sealed class Drawing {
		private const int OpClear = 0, OpRect = 1, OpLine = 2, OpText = 3;

		private readonly List<Engine.DrawCmd> _commands = new List<Engine.DrawCmd>();
		private readonly List<string> _texts = new List<string>();

		public int Count => _commands.Count;

		/// <summary>Fills the whole viewport, whatever size it turned out to be.</summary>
		public Drawing Clear(string color) => Add(OpClear, color);

		public Drawing Rect(float x, float y, float w, float h, string color, float radius = 0f) =>
			Add(OpRect, color, x, y, w, h, radius);

		/// <summary>A rect with equal sides and a radius of half of them. Centred on x, y.</summary>
		public Drawing Circle(float x, float y, float radius, string color) =>
			Add(OpRect, color, x - radius, y - radius, radius * 2, radius * 2, radius);

		public Drawing Line(float x1, float y1, float x2, float y2, string color, float width = 1f) =>
			Add(OpLine, color, x1, y1, x2, y2, width);

		/// <summary>Top-left at x, y. A size of 0 is the interface's own.</summary>
		public Drawing Text(float x, float y, string text, string color, float size = 0f) =>
			Add(OpText, color, x, y, 0f, 0f, size, text);

		private Drawing Add(int op, string color, float a = 0f, float b = 0f, float c = 0f,
		                    float d = 0f, float e = 0f, string text = null) {
			_commands.Add(new Engine.DrawCmd {
				Op = op, Color = PackColor(color), A = a, B = b, C = c, D = d, E = e,
			});
			_texts.Add(text);
			return this;
		}

		/// <summary>A colour as the engine packs them: 0xAABBGGRR. Takes what a theme takes.</summary>
		private static uint PackColor(string value) {
			string text = (value ?? "").TrimStart('#');
			if (text.Length == 3) text = string.Concat(text[0], text[0], text[1], text[1], text[2], text[2]);
			if (text.Length == 6) text += "FF";
			if (text.Length != 8)
				throw new RdaException("a colour is #RGB, #RRGGBB or #RRGGBBAA, not '" + value + "'");
			uint Part(int i) => Convert.ToUInt32(text.Substring(i, 2), 16);
			return Part(0) | (Part(2) << 8) | (Part(4) << 16) | (Part(6) << 24);
		}

		// The strings are allocated for exactly the length of the call and freed after it:
		// the engine copies what it is given before returning, so nothing here has to
		// outlive the round trip.
		internal void Send(string name) {
			Engine.DrawCmd[] block = _commands.ToArray();
			try {
				for (int i = 0; i < block.Length; i++) {
					if (_texts[i] != null) block[i].Text = Marshal.StringToCoTaskMemUTF8(_texts[i]);
				}
				if (Engine.rda_viewport_draw(name, block, block.Length) == 0)
					Engine.Fail("cannot draw in '" + name + "'");
			} finally {
				foreach (Engine.DrawCmd command in block) {
					if (command.Text != IntPtr.Zero) Marshal.FreeCoTaskMem(command.Text);
				}
			}
		}
	}
}
