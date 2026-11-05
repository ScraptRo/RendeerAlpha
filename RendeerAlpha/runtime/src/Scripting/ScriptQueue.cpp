#include <Scripting/ScriptQueue.h>
#include <GraphicalObjects/Scene.h>
#include <GraphicalObjects/Material.h>
#include <Logger/Logger.h>

namespace RDA {

	ScriptQueue& scriptQueue() {
		// Per thread: a transaction belongs to the script that opened it, and two script
		// threads sharing one queue would commit each other's unfinished work.
		thread_local ScriptQueue queue;
		return queue;
	}

	void ScriptQueue::record(const ScriptCommand& command) {
		std::lock_guard<std::mutex> lock(mMutex);
		mCommands.push_back(command);
	}

	bool ScriptQueue::empty() const {
		std::lock_guard<std::mutex> lock(mMutex);
		return mCommands.empty();
	}

	size_t ScriptQueue::size() const {
		std::lock_guard<std::mutex> lock(mMutex);
		return mCommands.size();
	}

	void ScriptQueue::discard() {
		std::lock_guard<std::mutex> lock(mMutex);
		mCommands.clear();
	}

	namespace {
		// Whether the scene still holds this entity.
		//
		// Checked as the queue is applied rather than when the command was recorded,
		// because a command earlier in the same queue may have destroyed it — and a script
		// that destroys something and then writes to it is a mistake worth surviving, not
		// one worth following into freed memory.
		bool stillInScene(const Entity* entity) {
			if (!entity) return false;
			const Scene& scene = getScene();
			for (size_t i = 0; i < scene.entityCount(); ++i) {
				if (scene.entityAt(i) == entity) return true;
			}
			return false;
		}

		void applyMaterialScalar(Material& material, int index, float value) {
			switch (index) {
			case 0: material.params.metallic = value; break;
			case 1: material.params.roughness = value; break;
			case 2: material.params.ambientOcclusion = value; break;
			default: material.params.emissive = value; break;
			}
		}

		void applyLightScalar(DirectionalLight& sun, int index, float value) {
			switch (index) {
			case 0: sun.intensity = value; break;
			case 1: sun.castsShadows = value != 0.0f; break;
			default: sun.shadowExtent = value; break;
			}
		}
	}

	size_t ScriptQueue::apply() {
		// Taken under the lock, run outside it. Applying reaches into the scene and can
		// destroy an entity; doing that while holding a lock the recording thread may be
		// waiting on is how a frame and a script deadlock on each other.
		std::vector<ScriptCommand> batch;
		{
			std::lock_guard<std::mutex> lock(mMutex);
			batch.swap(mCommands);
		}

		Scene& scene = getScene();
		size_t applied = 0;

		for (const ScriptCommand& command : batch) {
			// Everything that names an entity is checked here, once, against the scene as
			// it stands at this moment in the replay.
			const bool needsEntity =
				command.kind == ScriptCommand::Kind::EntityPosition ||
				command.kind == ScriptCommand::Kind::EntityTranslate ||
				command.kind == ScriptCommand::Kind::EntityVisible ||
				command.kind == ScriptCommand::Kind::EntityMaterial ||
				command.kind == ScriptCommand::Kind::EntityDestroy;
			if (needsEntity && !stillInScene(command.entity)) continue;

			switch (command.kind) {
			case ScriptCommand::Kind::EntityPosition:
				// Only the translation column, so rotation and scale survive.
				command.entity->transform[3] = glm::vec4(command.value, 1.0f);
				break;
			case ScriptCommand::Kind::EntityTranslate:
				command.entity->transform[3] += glm::vec4(command.value, 0.0f);
				break;
			case ScriptCommand::Kind::EntityVisible:
				command.entity->visible = command.value.x != 0.0f;
				break;
			case ScriptCommand::Kind::EntityMaterial:
				if (command.material) command.entity->material = command.material;
				break;
			case ScriptCommand::Kind::EntityDestroy:
				scene.destroyEntity(command.entity);
				break;
			case ScriptCommand::Kind::MaterialScalar:
				if (command.material) {
					applyMaterialScalar(*command.material, command.index, command.value.x);
				}
				break;
			case ScriptCommand::Kind::MaterialColor:
				if (command.material) {
					// Alpha is left alone, because nothing yet blends on it.
					command.material->params.baseColor =
						glm::vec4(command.value, command.material->params.baseColor.a);
				}
				break;
			case ScriptCommand::Kind::LightScalar:
				applyLightScalar(scene.sun, command.index, command.value.x);
				break;
			case ScriptCommand::Kind::LightDirection:
				scene.sun.direction = command.value;
				break;
			case ScriptCommand::Kind::LightColor:
				scene.sun.color = command.value;
				break;
			}
			++applied;
		}

		return applied;
	}

	// ---- reading through the queue ----------------------------------------------------
	//
	// Each of these starts from what the engine holds now and replays the pending commands
	// for that target in order. Replaying rather than taking the last write is what makes
	// translate() compose properly: two nudges and an assignment have to end up where the
	// same three calls would have put it.

	glm::vec3 pendingPosition(const Entity& entity) {
		glm::vec3 position = glm::vec3(entity.transform[3]);
		scriptQueue().forEach([&](const ScriptCommand& command) {
			if (command.entity != &entity) return;
			if (command.kind == ScriptCommand::Kind::EntityPosition) position = command.value;
			else if (command.kind == ScriptCommand::Kind::EntityTranslate) position += command.value;
		});
		return position;
	}

	bool pendingVisible(const Entity& entity) {
		bool visible = entity.visible;
		scriptQueue().forEach([&](const ScriptCommand& command) {
			if (command.entity == &entity && command.kind == ScriptCommand::Kind::EntityVisible) {
				visible = command.value.x != 0.0f;
			}
		});
		return visible;
	}

	const Material* pendingMaterial(const Entity& entity) {
		const Material* material = entity.material;
		scriptQueue().forEach([&](const ScriptCommand& command) {
			if (command.entity == &entity && command.kind == ScriptCommand::Kind::EntityMaterial) {
				if (command.material) material = command.material;
			}
		});
		return material;
	}

	float pendingMaterialScalar(const Material& material, int index) {
		const MaterialParams& params = material.params;
		float value = index == 0 ? params.metallic
			: index == 1 ? params.roughness
			: index == 2 ? params.ambientOcclusion
			: params.emissive;
		scriptQueue().forEach([&](const ScriptCommand& command) {
			if (command.material == &material && command.index == index &&
			    command.kind == ScriptCommand::Kind::MaterialScalar) {
				value = command.value.x;
			}
		});
		return value;
	}

	glm::vec3 pendingMaterialColor(const Material& material) {
		glm::vec3 colour = glm::vec3(material.params.baseColor);
		scriptQueue().forEach([&](const ScriptCommand& command) {
			if (command.material == &material && command.kind == ScriptCommand::Kind::MaterialColor) {
				colour = command.value;
			}
		});
		return colour;
	}

	float pendingLightScalar(const DirectionalLight& light, int index) {
		float value = index == 0 ? light.intensity
			: index == 1 ? (light.castsShadows ? 1.0f : 0.0f)
			: light.shadowExtent;
		scriptQueue().forEach([&](const ScriptCommand& command) {
			if (command.kind == ScriptCommand::Kind::LightScalar && command.index == index) {
				value = command.value.x;
			}
		});
		return value;
	}

	glm::vec3 pendingLightDirection(const DirectionalLight& light) {
		glm::vec3 direction = light.direction;
		scriptQueue().forEach([&](const ScriptCommand& command) {
			if (command.kind == ScriptCommand::Kind::LightDirection) direction = command.value;
		});
		return direction;
	}

	glm::vec3 pendingLightColor(const DirectionalLight& light) {
		glm::vec3 colour = light.color;
		scriptQueue().forEach([&](const ScriptCommand& command) {
			if (command.kind == ScriptCommand::Kind::LightColor) colour = command.value;
		});
		return colour;
	}

	bool pendingDestroyed(const Entity& entity) {
		bool destroyed = false;
		scriptQueue().forEach([&](const ScriptCommand& command) {
			if (command.entity == &entity && command.kind == ScriptCommand::Kind::EntityDestroy) {
				destroyed = true;
			}
		});
		return destroyed;
	}
}
