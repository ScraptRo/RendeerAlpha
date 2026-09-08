#include <GraphicalObjects/Viewports.h>
#include <Logger/Logger.h>
#include <chrono>

namespace RDA {

	Viewports& viewports() {
		static Viewports sViewports;
		return sViewports;
	}

	Viewports::Slot* Viewports::find(const std::string& name) {
		for (auto& entry : mSlots) {
			if (entry.first == name) return &entry.second;
		}
		return nullptr;
	}

	const Viewports::Slot* Viewports::find(const std::string& name) const {
		for (const auto& entry : mSlots) {
			if (entry.first == name) return &entry.second;
		}
		return nullptr;
	}

	void Viewports::onDraw(const std::string& name, std::function<void(const ViewportFrame&)> draw) {
		Slot* slot = find(name);
		if (!slot) {
			mSlots.emplace_back(name, Slot{});
			slot = &mSlots.back().second;
		}
		slot->draw = std::move(draw);
	}

	void Viewports::stopDrawing(const std::string& name) {
		if (Slot* slot = find(name)) slot->draw = nullptr;
	}

	bool Viewports::drawnByCode(const std::string& name) const {
		const Slot* slot = find(name);
		return slot && slot->draw != nullptr;
	}

	void Viewports::setClearColor(const std::string& name, uint32_t rgba) {
		Slot* slot = find(name);
		if (!slot) {
			mSlots.emplace_back(name, Slot{});
			slot = &mSlots.back().second;
		}
		slot->clearColor = rgba;
		++mRevision;
	}

	uint32_t Viewports::clearColor(const std::string& name) const {
		const Slot* slot = find(name);
		return slot ? slot->clearColor : 0xFF080505u;
	}

	void Viewports::setCommands(const std::string& name, std::vector<DrawCommand> commands) {
		Slot* slot = find(name);
		if (!slot) {
			mSlots.emplace_back(name, Slot{});
			slot = &mSlots.back().second;
		}
		slot->commands = std::move(commands);
		++mRevision;
		// An empty list is still a list: it means "draw nothing", which is different from
		// "nobody has ever drawn here", where the engine's own scene should show instead.
		slot->hasCommands = true;
	}

	const std::vector<DrawCommand>* Viewports::commands(const std::string& name) const {
		const Slot* slot = find(name);
		if (!slot || !slot->hasCommands) return nullptr;
		return &slot->commands;
	}

	glm::vec2 Viewports::size(const std::string& name) const {
		const Slot* slot = find(name);
		if (!slot) return { 0.0f, 0.0f };
		return { slot->rect.w, slot->rect.h };
	}

	void Viewports::beginFrame() {
		mPlacedThisFrame = false;
	}

	void Viewports::reportPlacement(const std::string& name, const Rect& rect) {
		Slot* slot = find(name);
		if (!slot) {
			mSlots.emplace_back(name, Slot{});
			slot = &mSlots.back().second;
		}
		slot->rect = rect;

		// The first placement of a frame is what starts the question over. Anything
		// reported after it this frame is a second viewport competing for the target.
		if (!mPlacedThisFrame) {
			mPlacedThisFrame = true;
			mGpuViewport.clear();
		}

		// There is one offscreen target, so one viewport can hold the GPU: the first one
		// painted that is not drawing 2D commands. First-painted rather than
		// first-registered because paint order is what the reader sees, and it is the
		// same every frame.
		if (slot->hasCommands) return;
		if (mGpuViewport.empty()) { mGpuViewport = name; return; }
		if (mGpuViewport != name && !mWarnedAboutSharing) {
			mWarnedAboutSharing = true;
			RDA_LOG_WARNING("viewport: '" << name << "' and '" << mGpuViewport
			                << "' both want the GPU and there is one target; '" << mGpuViewport
			                << "' has it. Give the other one 2D commands, or one viewport.");
		}
	}

	void Viewports::forget() {
		mSlots.clear();
		mGpuViewport.clear();
		mPlacedThisFrame = false;
		mClockStarted = false;
		mWarnedAboutSharing = false;
		++mRevision; // whatever was drawn is gone, and the GUI has to walk again to see it
	}

	void Viewports::record(VkCommandBuffer cmd, VkRenderPass pass, VkExtent2D extent,
	                       uint32_t frameIndex) {
		if (mGpuViewport.empty()) return;
		const Slot* slot = find(mGpuViewport);
		if (!slot || !slot->draw) return;

		// The engine's own clock rather than the loop's dt, so a callback that wants to
		// animate gets a time that keeps going whether or not anything else moved.
		using clock = std::chrono::steady_clock;
		const clock::time_point now = clock::now();
		if (!mClockStarted) {
			mClockStarted = true;
			mStarted = now;
			mPrevious = now;
		}

		ViewportFrame frame;
		frame.cmd = cmd;
		frame.renderPass = pass;
		frame.extent = extent;
		frame.frameIndex = frameIndex;
		frame.seconds = std::chrono::duration<float>(now - mStarted).count();
		frame.delta = std::chrono::duration<float>(now - mPrevious).count();
		mPrevious = now;

		// Copied before the call: a callback is free to register another one (a scene
		// swapping its renderer), and that would otherwise be rewriting the std::function
		// currently on the stack.
		auto draw = slot->draw;
		draw(frame);
	}
}
