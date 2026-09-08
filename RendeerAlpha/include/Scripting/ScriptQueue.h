#pragma once
#include <cstdint>
#include <Core/Datatypes.h>
#include <mutex>
#include <vector>

namespace RDA {
	struct Entity;
	struct DirectionalLight;
	class Material;

	// What a script asked the engine to change, recorded rather than performed.
	//
	// The bindings used to write straight through: `entity.position = ...` assigned into
	// `Entity::transform` on the spot. That works, and only because a cell runs inline on
	// the loop thread — the moment a script runs anywhere else, an assignment lands in the
	// middle of a frame that is already being walked. Recording instead gives the change a
	// defined moment to happen at, and gives the engine somewhere to check it first.
	//
	// Two things fall out of that which are worth having on their own:
	//
	//   * A script that fails changes nothing. The queue is discarded when the script
	//     throws, so a cell that dies halfway no longer leaves half a scene behind.
	//   * A destroyed entity cannot be written to. Targets are re-checked against the
	//     scene as the queue is applied, not when the command was recorded — which is the
	//     only order that survives one command destroying what a later one names.
	//
	// Reads are unaffected: a getter replays whatever is queued for its target on top of
	// the engine's value, so a script still sees what it just wrote. That is what keeps
	// the queue from turning ordinary code into a puzzle.
	//
	// What is *not* queued is resource creation — loadTexture, createMaterial. Those hand
	// back something the next line uses, so they cannot be deferred without changing what
	// they mean. They are also the calls that would need a reply rather than a command if
	// scripts ever move off this thread, which is a different mechanism from this one.
	struct ScriptCommand {
		enum class Kind : uint8_t {
			EntityPosition,     // entity, value.xyz
			EntityTranslate,    // entity, value.xyz (composes on whatever came before)
			EntityVisible,      // entity, value.x != 0
			EntityMaterial,     // entity, material
			EntityDestroy,      // entity
			MaterialScalar,     // material, index, value.x
			MaterialColor,      // material, value.xyz
			LightScalar,        // index, value.x
			LightDirection,     // value.xyz
			LightColor,         // value.xyz
		};

		Kind      kind{};
		Entity*   entity = nullptr;
		Material* material = nullptr;
		int       index = 0;      // which scalar, for the scalar kinds
		glm::vec3 value{ 0.0f };
	};

	// The commands one script has built up but not yet had applied.
	//
	// One queue per thread, not one for the engine. Two script threads sharing a queue
	// would each commit the other's half-finished work -- whichever finished first would
	// apply everything recorded so far, including commands the other was still in the
	// middle of building. A transaction belongs to the script that opened it, and a
	// thread is what a script has.
	//
	// The lock is for the one moment two threads do touch the same queue: the owner
	// records, then hands its queue to the loop thread to apply while it waits. Nothing
	// is concurrent today because the owner blocks -- the lock is what makes that a
	// property of the code rather than of the caller remembering to block.
	class ScriptQueue {
	public:
		void record(const ScriptCommand& command);
		bool empty() const;
		size_t size() const;

		// Performs everything recorded, in order, and empties the queue. Returns how many
		// commands were applied -- which can be fewer than were recorded, because a target
		// that has gone is skipped rather than followed.
		//
		// This touches the scene, so it belongs on the loop thread. The commit hook sends
		// it there through LoopWork rather than calling it wherever the script happened to
		// be running.
		size_t apply();

		// Throws the queue away. What a failed script gets.
		void discard();

		// Runs `visit` over the pending commands, in order, under the lock.
		//
		// A visitor rather than a reference to the vector: handing one out would be handing
		// out something another thread may be draining. `visit` must not touch this queue
		// again -- the lock is not recursive, and the read-through helpers below do not.
		template <typename Visitor>
		void forEach(Visitor visit) const {
			std::lock_guard<std::mutex> lock(mMutex);
			for (const ScriptCommand& command : mCommands) visit(command);
		}

	private:
		mutable std::mutex         mMutex;
		std::vector<ScriptCommand> mCommands;
	};

	// This thread's queue. See the note above about why it is per thread.
	ScriptQueue& scriptQueue();

	// ---- reading through the queue --------------------------------------------------
	// A getter asks these rather than the engine directly, so a script sees what it wrote
	// even though nothing has been written yet. Each starts from the engine's value and
	// replays whatever is pending for that target, in order.
	glm::vec3 pendingPosition(const Entity& entity);
	bool      pendingVisible(const Entity& entity);
	const Material* pendingMaterial(const Entity& entity);
	float     pendingMaterialScalar(const Material& material, int index);
	glm::vec3 pendingMaterialColor(const Material& material);
	float     pendingLightScalar(const DirectionalLight& light, int index);
	glm::vec3 pendingLightDirection(const DirectionalLight& light);
	glm::vec3 pendingLightColor(const DirectionalLight& light);

	// True once a command has destroyed it, so a binding can refuse to touch an entity
	// that is still in the scene only because the queue has not been applied yet.
	bool pendingDestroyed(const Entity& entity);
}
