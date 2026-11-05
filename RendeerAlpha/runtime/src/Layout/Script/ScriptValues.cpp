#include "ScriptInternal.h"

namespace RDA::Layout {
	namespace ScriptInternal {
		std::string toText(JSContext* ctx, JSValueConst value) {
			const char* raw = JS_ToCString(ctx, value);
			std::string out = raw ? raw : "";
			if (raw) JS_FreeCString(ctx, raw);
			return out;
		}

		JSValue toJS(JSContext* ctx, const Value& value) {
			switch (value.kind) {
			case Value::Kind::Number: return JS_NewFloat64(ctx, value.number);
			case Value::Kind::Bool:   return JS_NewBool(ctx, value.number != 0.0);
			case Value::Kind::Text:   return JS_NewStringLen(ctx, value.text.data(), value.text.size());
			}
			return JS_UNDEFINED;
		}

		// What a function returned, as one of the three things a property can be given.
		// Nothing is "", so a label shows nothing and a flag reads false.
		Value fromJS(JSContext* ctx, JSValueConst value) {
			if (JS_IsBool(value))   return Value::fromBool(JS_ToBool(ctx, value) == 1);
			if (JS_IsNumber(value)) { double n = 0.0; JS_ToFloat64(ctx, &n, value); return Value::fromNumber(n); }
			if (JS_IsNull(value) || JS_IsUndefined(value)) return Value::fromText(std::string());
			return Value::fromText(toText(ctx, value));
		}

		// A value as JSON, the way an action's result and a command's arguments cross the
		// boundary. Nothing JSON can say -- undefined, a function, a promise -- is null.
		std::string jsonOf(JSContext* ctx, JSValueConst value) {
			if (JS_IsUndefined(value) || JS_IsFunction(ctx, value) || JS_IsPromise(value) ||
			    JS_IsSymbol(value)) return "null";
			JSValue text = JS_JSONStringify(ctx, value, JS_UNDEFINED, JS_UNDEFINED);
			if (JS_IsException(text)) {
				JS_FreeValue(ctx, JS_GetException(ctx));   // a cycle, a BigInt: said as null
				return "null";
			}
			std::string out = JS_IsString(text) ? toText(ctx, text) : std::string("null");
			JS_FreeValue(ctx, text);
			return out;
		}

		// The elements of a JavaScript array, owned by the caller.
		std::vector<JSValue> elementsOf(JSContext* ctx, JSValueConst list) {
			std::vector<JSValue> out;
			if (!JS_IsArray(list)) return out;
			int64_t length = 0;
			if (JS_GetLength(ctx, list, &length) < 0) { JS_FreeValue(ctx, JS_GetException(ctx)); return out; }
			for (int64_t i = 0; i < length && i < 255; ++i)
				out.push_back(JS_GetPropertyUint32(ctx, list, static_cast<uint32_t>(i)));
			return out;
		}
	}
}
