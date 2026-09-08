// Finding the engine, and the calls that reach it.
//
// Everything here is P/Invoke against the C ABI in `RendeerC.h`. Nothing to install and
// nothing to build beyond the project itself: the engine is a shared library and this is
// a description of it.
//
// Nothing here is public API. `Rda.cs` is the API; a generated state file calls the
// accessors below by name.
//
// ---- the two things P/Invoke gets wrong by default ----
//
// **Strings are UTF-8 on this ABI, and a bare `string` parameter is not.** The default
// marshalling is ANSI, so anything outside plain ASCII is destroyed on the way in --
// silently, and only for the inputs an English test never uses. Every string going out
// carries [MarshalAs(UnmanagedType.LPUTF8Str)]; every string coming back is read as bytes
// and decoded here. Measured both ways before this file was written.
//
// **A delegate handed to native code must outlive the call that registered it.** The
// garbage collector cannot see that the engine is holding the function pointer, so a
// trampoline that goes out of scope is collected and the next click calls into freed
// memory. Every one is kept in `Alive` for the life of the process.

using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace Rendeer {

	/// <summary>Something the engine refused. The message is the engine's own.</summary>
	public class RdaException : Exception {
		/// <summary>Wraps the engine's own explanation of what it refused.</summary>
		public RdaException(string message) : base(message) { }
	}

	/// <summary>
	/// The seam a generated state file calls. Public because that file compiles into the
	/// application's assembly, not this one -- but it is not the API. See <see cref="Rda"/>.
	/// </summary>
	public static class Engine {
		internal const string Library = "rendeer_c";

		/// <summary>
		/// The revision of RendeerC.h this binding was written against. The engine says
		/// what it offers and how far back it still goes; both have to contain this
		/// number. Checked because the two are built separately: the .csproj copies a
		/// shared library that a different version of the checkout may have produced.
		/// </summary>
		// The RendeerC.h this binding was written against. The major has to match the
		// engine's exactly and the engine's minor has to be at least this one -- the
		// contract is in RendeerC.h.
		internal const int AbiMajor = 1;
		internal const int AbiMinor = 1;
		internal const uint NoSignal = 0xFFFFFFFFu;
		internal const uint NoTable = 0xFFFFFFFFu;

		// Cdecl explicitly. On x64 there is only one convention and it makes no
		// difference; on x86 the default is StdCall and this ABI is not, which is a
		// stack corruption rather than an error message.
		private const CallingConvention Cdecl = CallingConvention.Cdecl;

		internal delegate void Callback(IntPtr user);
		internal delegate void UpdateCallback(float dt, IntPtr user);

		// See the header comment. Never emptied.
		private static readonly List<object> Alive = new List<object>();

		// Where the engine is. .NET looks beside the assembly and then along the system
		// path, which is right for a shipped application and wrong for a developer
		// whose engine is in the checkout's bin/. RDA_ENGINE -- the file, or the folder
		// holding it -- is looked at first, the same escape hatch the Python and Node
		// bindings have. Anything else falls through to the default search.
		static Engine() {
			NativeLibrary.SetDllImportResolver(typeof(Engine).Assembly, (name, assembly, path) => {
				if (name != Library) return IntPtr.Zero;
				string? fromEnv = Environment.GetEnvironmentVariable("RDA_ENGINE");
				if (string.IsNullOrEmpty(fromEnv)) return IntPtr.Zero;
				string candidate = Directory.Exists(fromEnv) ? Path.Combine(fromEnv, FileName) : fromEnv;
				if (File.Exists(candidate) && NativeLibrary.TryLoad(candidate, out IntPtr handle)) return handle;
				return IntPtr.Zero;
			});
		}

		private static string FileName =>
			OperatingSystem.IsWindows() ? "rendeer_c.dll"
			: OperatingSystem.IsMacOS() ? "librendeer_c.dylib"
			: "librendeer_c.so";

		[DllImport(Library, CallingConvention = Cdecl)]
		private static extern int rda_abi_major();

		[DllImport(Library, CallingConvention = Cdecl)]
		private static extern int rda_abi_minor();

		/// <summary>
		/// Refuses an engine this binding cannot talk to. Called before anything else
		/// reaches the library, so a mismatch is one sentence rather than a missing
		/// entry point somewhere further in.
		/// </summary>
		internal static void CheckAbi() {
			string wanted = AbiMajor + "." + AbiMinor;
			int major, minor;
			try {
				major = rda_abi_major();
				minor = rda_abi_minor();
			} catch (EntryPointNotFoundException) {
				throw new RdaException(
					$"this engine predates the ABI version check, so it is older than this " +
					$"binding (which needs {wanted}). Rebuild the engine: " +
					$"scripts/windows-bringup.bat or scripts/linux-bringup.sh in the checkout.");
			}

			string found = major + "." + minor;
			if (major != AbiMajor) {
				// A different major in either direction: the engine has either removed
				// something this binding calls or never had it. Which way it went decides
				// which half moves.
				string which = major < AbiMajor
					? "The engine is behind the binding: rebuild it, or use the binding from the same checkout"
					: "The binding is behind the engine: build against the bindings/csharp in this checkout";
				throw new RdaException(
					$"the engine speaks ABI {found} and this binding speaks {wanted}. A " +
					$"different major number is a different ABI, not an older one, so nothing " +
					$"here will work. {which}.");
			}
			if (minor < AbiMinor) {
				throw new RdaException(
					$"the engine offers ABI {found} and this binding needs {wanted}. Same " +
					$"major, so nothing was removed -- the engine is simply missing what was " +
					$"added since: rebuild it, or use the binding from the same checkout.");
			}
		}

		// ---- what went wrong ----
		[DllImport(Library, CallingConvention = Cdecl)]
		private static extern IntPtr rda_last_error();

		internal static string LastError() {
			IntPtr text = rda_last_error();
			return text == IntPtr.Zero ? "" : Marshal.PtrToStringUTF8(text) ?? "";
		}

		internal static void Fail(string what) {
			string message = LastError();
			throw new RdaException(message.Length > 0 ? what + ": " + message : what);
		}

		// ---- configuration ----
		[DllImport(Library, CallingConvention = Cdecl)]
		internal static extern IntPtr rda_config_new();
		[DllImport(Library, CallingConvention = Cdecl)]
		internal static extern void rda_config_free(IntPtr config);
		[DllImport(Library, CallingConvention = Cdecl)]
		internal static extern void rda_config_set_name(IntPtr config,
			[MarshalAs(UnmanagedType.LPUTF8Str)] string name);
		[DllImport(Library, CallingConvention = Cdecl)]
		internal static extern void rda_config_set_theme(IntPtr config,
			[MarshalAs(UnmanagedType.LPUTF8Str)] string path);
		[DllImport(Library, CallingConvention = Cdecl)]
		internal static extern void rda_config_set_languages(IntPtr config,
			[MarshalAs(UnmanagedType.LPUTF8Str)] string path);
		[DllImport(Library, CallingConvention = Cdecl)]
		internal static extern void rda_config_set_vsync(IntPtr config, int on);
		[DllImport(Library, CallingConvention = Cdecl)]
		internal static extern void rda_config_set_font(IntPtr config,
			[MarshalAs(UnmanagedType.LPUTF8Str)] string path, float height);
		[DllImport(Library, CallingConvention = Cdecl)]
		internal static extern void rda_config_on_start(IntPtr config, Callback fn, IntPtr user);
		[DllImport(Library, CallingConvention = Cdecl)]
		internal static extern void rda_config_on_update(IntPtr config, UpdateCallback fn, IntPtr user);
		[DllImport(Library, CallingConvention = Cdecl)]
		internal static extern void rda_config_on_shutdown(IntPtr config, Callback fn, IntPtr user);

		// ---- lifecycle ----
		[DllImport(Library, CallingConvention = Cdecl)]
		internal static extern int rda_init(IntPtr config);
		[DllImport(Library, CallingConvention = Cdecl)]
		internal static extern int rda_running();
		[DllImport(Library, CallingConvention = Cdecl)]
		internal static extern void rda_stop();
		[DllImport(Library, CallingConvention = Cdecl)]
		internal static extern void rda_wait();

		// ---- the interface ----
		[DllImport(Library, CallingConvention = Cdecl)]
		internal static extern int rda_load_interface(
			[MarshalAs(UnmanagedType.LPUTF8Str)] string blueprint,
			[MarshalAs(UnmanagedType.LPUTF8Str)] string source);

		// An array of UTF-8 strings, which the marshaller will not do on its own -- the
		// pointers are built by hand in Rda.OpenRoutes and freed there.
		[DllImport(Library, CallingConvention = Cdecl)]
		internal static extern int rda_open_routes(IntPtr[] names, IntPtr[] layouts,
			IntPtr[] parameters, int count,
			[MarshalAs(UnmanagedType.LPUTF8Str)] string layoutDir,
			[MarshalAs(UnmanagedType.LPUTF8Str)] string sourceDir);

		[DllImport(Library, CallingConvention = Cdecl)]
		internal static extern void rda_set_transition_ms(float ms);
		[DllImport(Library, CallingConvention = Cdecl)]
		internal static extern int rda_route_back();
		[DllImport(Library, CallingConvention = Cdecl)]
		internal static extern int rda_route_forward();
		[DllImport(Library, CallingConvention = Cdecl)]
		internal static extern int rda_route_can_go_back();
		[DllImport(Library, CallingConvention = Cdecl)]
		internal static extern int rda_route_can_go_forward();

		// ---- declaring ----
		[DllImport(Library, CallingConvention = Cdecl)]
		private static extern uint rda_define_number(
			[MarshalAs(UnmanagedType.LPUTF8Str)] string name, double value);
		[DllImport(Library, CallingConvention = Cdecl)]
		private static extern uint rda_define_bool(
			[MarshalAs(UnmanagedType.LPUTF8Str)] string name, int value);
		[DllImport(Library, CallingConvention = Cdecl)]
		private static extern uint rda_define_text(
			[MarshalAs(UnmanagedType.LPUTF8Str)] string name,
			[MarshalAs(UnmanagedType.LPUTF8Str)] string value);
		[DllImport(Library, CallingConvention = Cdecl)]
		private static extern int rda_define_command(
			[MarshalAs(UnmanagedType.LPUTF8Str)] string name);
		[DllImport(Library, CallingConvention = Cdecl)]
		private static extern int rda_define_table(
			[MarshalAs(UnmanagedType.LPUTF8Str)] string name);
		[DllImport(Library, CallingConvention = Cdecl)]
		private static extern int rda_define_column(
			[MarshalAs(UnmanagedType.LPUTF8Str)] string table,
			[MarshalAs(UnmanagedType.LPUTF8Str)] string column, int type);

		/// <summary>Declares a number signal with its initial value.</summary>
		public static void DefineNumber(string name, double value) => rda_define_number(name, value);
		/// <summary>Declares a boolean signal with its initial value.</summary>
		public static void DefineBool(string name, bool value) => rda_define_bool(name, value ? 1 : 0);
		/// <summary>Declares a text signal with its initial value.</summary>
		public static void DefineText(string name, string value) => rda_define_text(name, value);

		/// <summary>Declares a command the interface may ask for.</summary>
		public static void DefineCommand(string name) {
			if (rda_define_command(name) == 0) Fail("cannot define the command '" + name + "'");
		}
		/// <summary>Declares a table a &lt;list&gt; can show.</summary>
		public static void DefineTable(string name) {
			if (rda_define_table(name) == 0) Fail("cannot define the table '" + name + "'");
		}
		/// <summary>Declares one column of a table. 0 number, 1 bool, 2 text.</summary>
		public static void DefineColumn(string table, string column, int type) {
			if (rda_define_column(table, column, type) == 0)
				Fail("cannot define " + table + "." + column);
		}

		// ---- signals ----
		[DllImport(Library, CallingConvention = Cdecl)]
		private static extern uint rda_signal_find(
			[MarshalAs(UnmanagedType.LPUTF8Str)] string name);
		[DllImport(Library, CallingConvention = Cdecl)]
		private static extern int rda_signal_get_number(uint signal, out double value);
		[DllImport(Library, CallingConvention = Cdecl)]
		private static extern int rda_signal_set_number(uint signal, double value);
		[DllImport(Library, CallingConvention = Cdecl)]
		private static extern int rda_signal_get_bool(uint signal, out int value);
		[DllImport(Library, CallingConvention = Cdecl)]
		private static extern int rda_signal_set_bool(uint signal, int value);
		[DllImport(Library, CallingConvention = Cdecl)]
		private static extern int rda_signal_get_text(uint signal, byte[]? buffer, int capacity);
		[DllImport(Library, CallingConvention = Cdecl)]
		private static extern int rda_signal_set_text(uint signal,
			[MarshalAs(UnmanagedType.LPUTF8Str)] string value);

		// A name lookup is a round trip to the loop thread, so it happens once.
		private static readonly Dictionary<string, uint> Ids = new Dictionary<string, uint>();

		private static uint Signal(string name) {
			if (Ids.TryGetValue(name, out uint found)) return found;
			uint id = rda_signal_find(name);
			if (id == NoSignal) Fail("no signal called '" + name + "'");
			Ids[name] = id;
			return id;
		}

		/// <summary>Reads a number signal.</summary>
		public static double GetNumber(string name) {
			if (rda_signal_get_number(Signal(name), out double value) == 0)
				Fail("cannot read '" + name + "'");
			return value;
		}
		/// <summary>Writes a number signal.</summary>
		public static void SetNumber(string name, double value) {
			if (rda_signal_set_number(Signal(name), value) == 0)
				Fail("cannot write '" + name + "'");
		}
		/// <summary>Reads a boolean signal.</summary>
		public static bool GetBool(string name) {
			if (rda_signal_get_bool(Signal(name), out int value) == 0)
				Fail("cannot read '" + name + "'");
			return value != 0;
		}
		/// <summary>Writes a boolean signal.</summary>
		public static void SetBool(string name, bool value) {
			if (rda_signal_set_bool(Signal(name), value ? 1 : 0) == 0)
				Fail("cannot write '" + name + "'");
		}
		/// <summary>Writes a text signal.</summary>
		public static void SetText(string name, string value) {
			if (rda_signal_set_text(Signal(name), value ?? "") == 0)
				Fail("cannot write '" + name + "'");
		}

		// The engine reports what the text *is* rather than what fitted, so a first
		// guess that was too small costs one more call and never a truncation nobody
		// notices. A guess that was right -- which is nearly always -- costs one call
		// instead of the two a length-first protocol would always pay.
		private const int TextGuess = 256;

		/// <summary>Reads a text signal, retrying once if the first buffer was too small.</summary>
		public static string GetText(string name) {
			uint id = Signal(name);
			byte[] buffer = new byte[TextGuess];
			int length = rda_signal_get_text(id, buffer, buffer.Length);
			if (length < 0) Fail("cannot read '" + name + "'");
			if (length >= buffer.Length) {
				buffer = new byte[length + 1];
				length = rda_signal_get_text(id, buffer, buffer.Length);
				if (length < 0) Fail("cannot read '" + name + "'");
			}
			return Encoding.UTF8.GetString(buffer, 0, length);
		}

		// ---- tables ----
		[DllImport(Library, CallingConvention = Cdecl)]
		private static extern uint rda_table_find(
			[MarshalAs(UnmanagedType.LPUTF8Str)] string name);
		[DllImport(Library, CallingConvention = Cdecl)]
		private static extern int rda_table_column(uint table,
			[MarshalAs(UnmanagedType.LPUTF8Str)] string column);
		[DllImport(Library, CallingConvention = Cdecl)]
		private static extern int rda_table_rows(uint table);
		[DllImport(Library, CallingConvention = Cdecl)]
		private static extern int rda_table_resize(uint table, int rows);
		[DllImport(Library, CallingConvention = Cdecl)]
		private static extern int rda_table_set_numbers(uint table, int column, int first,
			double[] values, int count);
		[DllImport(Library, CallingConvention = Cdecl)]
		private static extern int rda_table_set_bools(uint table, int column, int first,
			int[] values, int count);
		[DllImport(Library, CallingConvention = Cdecl)]
		private static extern int rda_table_set_texts(uint table, int column, int first,
			IntPtr[] values, int count);
		[DllImport(Library, CallingConvention = Cdecl)]
		private static extern int rda_table_get_number(uint table, int column, int row, out double value);
		[DllImport(Library, CallingConvention = Cdecl)]
		private static extern int rda_table_get_bool(uint table, int column, int row, out int value);
		[DllImport(Library, CallingConvention = Cdecl)]
		private static extern int rda_table_get_text(uint table, int column, int row,
			byte[]? buffer, int capacity);

		private static readonly Dictionary<string, uint> TableIds = new Dictionary<string, uint>();
		private static readonly Dictionary<string, int> ColumnIds = new Dictionary<string, int>();

		private static uint Table(string name) {
			if (TableIds.TryGetValue(name, out uint found)) return found;
			uint id = rda_table_find(name);
			if (id == NoTable) Fail("no table called '" + name + "'");
			TableIds[name] = id;
			return id;
		}

		private static int Column(string table, string column) {
			string key = table + " " + column;
			if (ColumnIds.TryGetValue(key, out int found)) return found;
			int index = rda_table_column(Table(table), column);
			if (index < 0) Fail("no column '" + column + "' in '" + table + "'");
			ColumnIds[key] = index;
			return index;
		}

		/// <summary>How many rows a table holds.</summary>
		public static int TableRows(string table) {
			int rows = rda_table_rows(Table(table));
			if (rows < 0) Fail("cannot read the size of '" + table + "'");
			return rows;
		}

		/// <summary>Grows or shrinks a table. New rows are zero, false and empty.</summary>
		public static void TableResize(string table, int rows) {
			if (rda_table_resize(Table(table), rows < 0 ? 0 : rows) == 0)
				Fail("cannot resize '" + table + "'");
		}

		/// <summary>Writes a run of numbers into one column, starting at <paramref name="first"/>.</summary>
		public static void SetNumbers(string table, string column, double[] values, int first = 0) {
			if (values.Length == 0) return;
			if (rda_table_set_numbers(Table(table), Column(table, column), first,
					values, values.Length) == 0)
				Fail("cannot write " + table + "." + column);
		}

		/// <summary>Writes a run of booleans into one column, starting at <paramref name="first"/>.</summary>
		public static void SetBools(string table, string column, bool[] values, int first = 0) {
			if (values.Length == 0) return;
			int[] block = new int[values.Length];
			for (int i = 0; i < values.Length; ++i) block[i] = values[i] ? 1 : 0;
			if (rda_table_set_bools(Table(table), Column(table, column), first,
					block, block.Length) == 0)
				Fail("cannot write " + table + "." + column);
		}

		/// <summary>Writes a run of strings into one column, starting at <paramref name="first"/>.</summary>
		public static void SetTexts(string table, string column, string[] values, int first = 0) {
			if (values.Length == 0) return;
			// An array of UTF-8 strings is the one shape the marshaller will not build,
			// so the pointers are made here and freed in the same breath.
			IntPtr[] pointers = new IntPtr[values.Length];
			try {
				for (int i = 0; i < values.Length; ++i) {
					pointers[i] = Utf8(values[i] ?? "");
				}
				if (rda_table_set_texts(Table(table), Column(table, column), first,
						pointers, pointers.Length) == 0)
					Fail("cannot write " + table + "." + column);
			} finally {
				foreach (IntPtr one in pointers) {
					if (one != IntPtr.Zero) Marshal.FreeHGlobal(one);
				}
			}
		}

		internal static IntPtr Utf8(string value) {
			byte[] bytes = Encoding.UTF8.GetBytes(value);
			IntPtr block = Marshal.AllocHGlobal(bytes.Length + 1);
			Marshal.Copy(bytes, 0, block, bytes.Length);
			Marshal.WriteByte(block, bytes.Length, 0);
			return block;
		}

		/// <summary>Reads one number cell.</summary>
		public static double GetCellNumber(string table, string column, int row) {
			if (rda_table_get_number(Table(table), Column(table, column), row, out double value) == 0)
				Fail("cannot read " + table + "." + column + "[" + row + "]");
			return value;
		}

		/// <summary>Reads one boolean cell.</summary>
		public static bool GetCellBool(string table, string column, int row) {
			if (rda_table_get_bool(Table(table), Column(table, column), row, out int value) == 0)
				Fail("cannot read " + table + "." + column + "[" + row + "]");
			return value != 0;
		}

		/// <summary>Reads one text cell.</summary>
		public static string GetCellText(string table, string column, int row) {
			uint id = Table(table);
			int index = Column(table, column);
			byte[] buffer = new byte[TextGuess];
			int length = rda_table_get_text(id, index, row, buffer, buffer.Length);
			if (length < 0) Fail("cannot read " + table + "." + column + "[" + row + "]");
			if (length >= buffer.Length) {
				buffer = new byte[length + 1];
				length = rda_table_get_text(id, index, row, buffer, buffer.Length);
				if (length < 0) Fail("cannot read " + table + "." + column + "[" + row + "]");
			}
			return Encoding.UTF8.GetString(buffer, 0, length);
		}

		// ---- commands ----
		[DllImport(Library, CallingConvention = Cdecl)]
		private static extern int rda_command_bind(
			[MarshalAs(UnmanagedType.LPUTF8Str)] string name, Callback fn, IntPtr user);
		[DllImport(Library, CallingConvention = Cdecl)]
		private static extern int rda_command_invoke(
			[MarshalAs(UnmanagedType.LPUTF8Str)] string name);

		/// <summary>
		/// Binds a handler to a declared command.
		///
		/// A real callback, unlike Node's: the engine calls this on its own loop thread,
		/// inside the frame the interface asked in, because the CLR is happy to be
		/// entered from a foreign thread. Which means the handler must be quick, and must
		/// not block on anything the loop is responsible for.
		/// </summary>
		public static void OnCommand(string name, Action handler) {
			if (handler == null) throw new RdaException("a handler for '" + name + "' has to be a function");
			Callback trampoline = _ => handler();
			Alive.Add(trampoline);   // see the header comment
			if (rda_command_bind(name, trampoline, IntPtr.Zero) == 0)
				Fail("cannot bind '" + name + "'");
		}

		/// <summary>Asks for a command as if the interface had.</summary>
		public static void Invoke(string name) {
			if (rda_command_invoke(name) == 0) Fail("cannot invoke '" + name + "'");
		}

		[DllImport(Library, CallingConvention = Cdecl)]
		internal static extern int rda_set_theme([MarshalAs(UnmanagedType.LPUTF8Str)] string path);

		// ---- viewports ----
		// Laid out exactly as rda_draw_cmd. The text is an IntPtr rather than a string
		// because the runtime will not marshal a string field inside an array of structs
		// without copying the whole array twice; Drawing allocates and frees them itself.
		[StructLayout(LayoutKind.Sequential)]
		internal struct DrawCmd {
			public int   Op;
			public uint  Color;
			public float A, B, C, D;
			public float E;
			public IntPtr Text;
		}

		[DllImport(Library, CallingConvention = Cdecl)]
		internal static extern int rda_viewport_draw(
			[MarshalAs(UnmanagedType.LPUTF8Str)] string name, [In] DrawCmd[] commands, int count);
		[DllImport(Library, CallingConvention = Cdecl)]
		internal static extern int rda_viewport_size(
			[MarshalAs(UnmanagedType.LPUTF8Str)] string name, out float width, out float height);

		internal static void Keep(object trampoline) => Alive.Add(trampoline);
	}
}
