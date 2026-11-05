#include "ScriptInternal.h"
#include <Core/Tables.h>

namespace RDA::Layout {
	namespace ScriptInternal {
		void recordSignal(uint32_t id) {
			if (ScriptHost::Frame* frame = scriptHost().frame()) frame->noteSignal(id);
		}

		void recordTable(uint32_t id) {
			if (ScriptHost::Frame* frame = scriptHost().frame()) frame->noteTable(id, tables().at(id).version());
		}

		bool readOnly() {
			ScriptHost::Frame* frame = scriptHost().frame();
			return frame && frame->mode() == ScriptHost::Mode::Value;
		}
	}

	ScriptHost::Frame::Frame(Mode mode) : mMode(mode) {
		ScriptHost& host = scriptHost();
		mOuter = host.mFrame;
		host.mFrame = this;
	}

	ScriptHost::Frame::~Frame() {
		scriptHost().mFrame = mOuter;
	}
}
