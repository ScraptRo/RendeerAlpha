#include <Scripting/Script.h>
#include <Scripting/ScriptQueue.h>
#include <Core/LoopWork.h>
#include <RendeerAlpha.h>
#include <GraphicalObjects/Scene.h>
#include <Logger/Logger.h>
#include <vendor/quickjs/quickjs.h>
#include <cmath>
#include <deque>
#include <new>

// The engine's JavaScript classes.
//
// Objects are real classes with prototypes, not opaque handles: `new Vec3(1,2,3)` gives
// something with `.x`, `.length()` and a working `instanceof`, and `scene.sun` returns an
// object whose accessors write straight through to the C++ light. The alternative — an
// integer handle plus free functions — is easier to implement and worse to use, and it
// gives up everything the language already knows how to do with objects.
//
// The pattern each class follows is the same four pieces:
//   1. a JSClassID and a JSClassDef, which is what carries the finalizer
//   2. a prototype object holding the methods and accessors
//   3. a constructor function whose .prototype is that object
//   4. JS_SetOpaque / JS_GetOpaque to tie an instance to its C++ data
//
// Ownership is deliberate. Vec3 owns its payload and frees it in the finalizer. The
// scene-backed classes do not: they point at engine memory that outlives any script, so
// their finalizers free nothing. Mixing those two up is how a scripting layer starts
// double-freeing, so each class says which it is.
namespace RDA {

	namespace {

		// ---- Vec3: a value type the script owns ------------------------------------
		JSClassID gVec3ClassId;

		struct Vec3Data { glm::vec3 v; };

		void vec3Finalizer(JSRuntime* rt, JSValue value) {
			// Script-owned: allocated in the constructor, released here.
			if (auto* data = static_cast<Vec3Data*>(JS_GetOpaque(value, gVec3ClassId))) {
				js_free_rt(rt, data);
			}
		}

		JSClassDef gVec3Class = { "Vec3", vec3Finalizer, nullptr, nullptr, nullptr };

		Vec3Data* vec3Self(JSContext* ctx, JSValueConst self) {
			auto* data = static_cast<Vec3Data*>(JS_GetOpaque2(ctx, self, gVec3ClassId));
			return data;
		}

		// Builds an instance from C++. Takes the prototype from the constructor stored on
		// the class id, so `new Vec3()` and a returned Vec3 share one prototype and
		// instanceof holds for both.
		JSValue makeVec3(JSContext* ctx, const glm::vec3& v) {
			JSValue proto = JS_GetClassProto(ctx, gVec3ClassId);
			JSValue object = JS_NewObjectProtoClass(ctx, proto, gVec3ClassId);
			JS_FreeValue(ctx, proto);
			if (JS_IsException(object)) return object;

			auto* data = static_cast<Vec3Data*>(js_malloc(ctx, sizeof(Vec3Data)));
			if (!data) { JS_FreeValue(ctx, object); return JS_ThrowOutOfMemory(ctx); }
			data->v = v;
			JS_SetOpaque(object, data);
			return object;
		}

		JSValue vec3Constructor(JSContext* ctx, JSValueConst newTarget, int argc, JSValueConst* argv) {
			double x = 0, y = 0, z = 0;
			if (argc > 0) JS_ToFloat64(ctx, &x, argv[0]);
			if (argc > 1) JS_ToFloat64(ctx, &y, argv[1]);
			if (argc > 2) JS_ToFloat64(ctx, &z, argv[2]);

			// Honour the prototype of whatever was called, so a subclass of Vec3 keeps
			// its own prototype chain.
			JSValue proto = JS_GetPropertyStr(ctx, newTarget, "prototype");
			if (JS_IsException(proto)) return proto;
			JSValue object = JS_NewObjectProtoClass(ctx, proto, gVec3ClassId);
			JS_FreeValue(ctx, proto);
			if (JS_IsException(object)) return object;

			auto* data = static_cast<Vec3Data*>(js_malloc(ctx, sizeof(Vec3Data)));
			if (!data) { JS_FreeValue(ctx, object); return JS_ThrowOutOfMemory(ctx); }
			data->v = { static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) };
			JS_SetOpaque(object, data);
			return object;
		}

		// One getter and one setter for all three components, told apart by `magic`.
		JSValue vec3GetComponent(JSContext* ctx, JSValueConst self, int magic) {
			Vec3Data* data = vec3Self(ctx, self);
			if (!data) return JS_EXCEPTION;
			return JS_NewFloat64(ctx, data->v[magic]);
		}
		JSValue vec3SetComponent(JSContext* ctx, JSValueConst self, JSValueConst value, int magic) {
			Vec3Data* data = vec3Self(ctx, self);
			if (!data) return JS_EXCEPTION;
			double f = 0;
			if (JS_ToFloat64(ctx, &f, value)) return JS_EXCEPTION;
			data->v[magic] = static_cast<float>(f);
			return JS_UNDEFINED;
		}

		JSValue vec3Length(JSContext* ctx, JSValueConst self, int, JSValueConst*) {
			Vec3Data* data = vec3Self(ctx, self);
			if (!data) return JS_EXCEPTION;
			return JS_NewFloat64(ctx, glm::length(data->v));
		}
		JSValue vec3Add(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
			Vec3Data* a = vec3Self(ctx, self);
			if (!a) return JS_EXCEPTION;
			if (argc < 1) return JS_ThrowTypeError(ctx, "add expects a Vec3");
			auto* b = static_cast<Vec3Data*>(JS_GetOpaque2(ctx, argv[0], gVec3ClassId));
			if (!b) return JS_EXCEPTION;
			return makeVec3(ctx, a->v + b->v);
		}
		JSValue vec3ToString(JSContext* ctx, JSValueConst self, int, JSValueConst*) {
			Vec3Data* data = vec3Self(ctx, self);
			if (!data) return JS_EXCEPTION;
			char buffer[96];
			std::snprintf(buffer, sizeof(buffer), "Vec3(%g, %g, %g)",
				data->v.x, data->v.y, data->v.z);
			return JS_NewString(ctx, buffer);
		}

		const JSCFunctionListEntry gVec3Proto[] = {
			JS_CGETSET_MAGIC_DEF("x", vec3GetComponent, vec3SetComponent, 0),
			JS_CGETSET_MAGIC_DEF("y", vec3GetComponent, vec3SetComponent, 1),
			JS_CGETSET_MAGIC_DEF("z", vec3GetComponent, vec3SetComponent, 2),
			JS_CFUNC_DEF("length", 0, vec3Length),
			JS_CFUNC_DEF("add", 1, vec3Add),
			JS_CFUNC_DEF("toString", 0, vec3ToString),
		};

		// ---- Light: a view onto engine memory --------------------------------------
		// The opaque pointer is the DirectionalLight inside the engine's Scene. Nothing
		// is owned here, so the finalizer is absent: freeing it would free the scene's
		// light. The lifetime is safe because the Scene outlives the script context.
		JSClassID gLightClassId;
		JSClassDef gLightClass = { "DirectionalLight", nullptr, nullptr, nullptr, nullptr };

		DirectionalLight* lightSelf(JSContext* ctx, JSValueConst self) {
			return static_cast<DirectionalLight*>(JS_GetOpaque2(ctx, self, gLightClassId));
		}

		// magic: 0 intensity, 1 castsShadows, 2 shadowExtent
		JSValue lightGet(JSContext* ctx, JSValueConst self, int magic) {
			DirectionalLight* light = lightSelf(ctx, self);
			if (!light) return JS_EXCEPTION;
			// Through the queue, so a script reads back what it has just written even
			// though nothing has been applied yet.
			const float value = pendingLightScalar(*light, magic);
			if (magic == 1) return JS_NewBool(ctx, value != 0.0f);
			return JS_NewFloat64(ctx, value);
		}
		// magic: 0 direction, 1 color. Both read through the queue.
		JSValue lightGetDirection(JSContext* ctx, JSValueConst self, int magic) {
			DirectionalLight* light = lightSelf(ctx, self);
			if (!light) return JS_EXCEPTION;
			return makeVec3(ctx, magic == 0 ? pendingLightDirection(*light)
			                                : pendingLightColor(*light));
		}

		JSValue lightSet(JSContext* ctx, JSValueConst self, JSValueConst value, int magic) {
			DirectionalLight* light = lightSelf(ctx, self);
			if (!light) return JS_EXCEPTION;
			double f = 0;
			if (JS_ToFloat64(ctx, &f, value)) return JS_EXCEPTION;
			ScriptCommand command;
			command.kind = ScriptCommand::Kind::LightScalar;
			command.index = magic;
			command.value.x = static_cast<float>(f);
			scriptQueue().record(command);
			return JS_UNDEFINED;
		}

		JSValue lightSetDirection(JSContext* ctx, JSValueConst self, JSValueConst value, int magic) {
			DirectionalLight* light = lightSelf(ctx, self);
			if (!light) return JS_EXCEPTION;
			auto* v = static_cast<Vec3Data*>(JS_GetOpaque2(ctx, value, gVec3ClassId));
			if (!v) return JS_EXCEPTION;
			ScriptCommand command;
			command.kind = magic == 0 ? ScriptCommand::Kind::LightDirection
			                          : ScriptCommand::Kind::LightColor;
			command.value = v->v;
			scriptQueue().record(command);
			return JS_UNDEFINED;
		}

		const JSCFunctionListEntry gLightProto[] = {
			JS_CGETSET_MAGIC_DEF("intensity", lightGet, lightSet, 0),
			JS_CGETSET_MAGIC_DEF("castsShadows", lightGet, lightSet, 1),
			JS_CGETSET_MAGIC_DEF("shadowExtent", lightGet, lightSet, 2),
			JS_CGETSET_MAGIC_DEF("direction", lightGetDirection, lightSetDirection, 0),
			JS_CGETSET_MAGIC_DEF("color", lightGetDirection, lightSetDirection, 1),
		};

		JSValue makeLight(JSContext* ctx, DirectionalLight* light) {
			JSValue proto = JS_GetClassProto(ctx, gLightClassId);
			JSValue object = JS_NewObjectProtoClass(ctx, proto, gLightClassId);
			JS_FreeValue(ctx, proto);
			if (!JS_IsException(object)) JS_SetOpaque(object, light);
			return object;
		}

		// ---- Texture: a reference to an engine-owned image ---------------------------
		// Owns its handle, unlike the scene-backed classes: obj_ref is counted, so the
		// script holding one keeps the image alive and the finalizer lets it go. A
		// material built from it keeps its own reference, so a texture whose JS object has
		// been collected is still there for as long as something draws with it.
		JSClassID gTextureClassId;

		void textureFinalizer(JSRuntime*, JSValue value) {
			delete static_cast<obj_ref<Texture>*>(JS_GetOpaque(value, gTextureClassId));
		}
		JSClassDef gTextureClass = { "Texture", textureFinalizer, nullptr, nullptr, nullptr };

		obj_ref<Texture>* textureSelf(JSContext* ctx, JSValueConst self) {
			return static_cast<obj_ref<Texture>*>(JS_GetOpaque2(ctx, self, gTextureClassId));
		}

		JSValue textureGetWidth(JSContext* ctx, JSValueConst self) {
			obj_ref<Texture>* t = textureSelf(ctx, self);
			if (!t) return JS_EXCEPTION;
			return JS_NewUint32(ctx, t->IsValid() ? (*t)->extent().width : 0);
		}
		JSValue textureGetHeight(JSContext* ctx, JSValueConst self) {
			obj_ref<Texture>* t = textureSelf(ctx, self);
			if (!t) return JS_EXCEPTION;
			return JS_NewUint32(ctx, t->IsValid() ? (*t)->extent().height : 0);
		}

		const JSCFunctionListEntry gTextureProto[] = {
			JS_CGETSET_DEF("width", textureGetWidth, nullptr),
			JS_CGETSET_DEF("height", textureGetHeight, nullptr),
		};

		JSValue makeTexture(JSContext* ctx, const obj_ref<Texture>& ref) {
			JSValue proto = JS_GetClassProto(ctx, gTextureClassId);
			JSValue object = JS_NewObjectProtoClass(ctx, proto, gTextureClassId);
			JS_FreeValue(ctx, proto);
			if (JS_IsException(object)) return object;
			JS_SetOpaque(object, new obj_ref<Texture>(ref));
			return object;
		}

		// ---- Material: a view onto an engine-owned material -------------------------
		JSClassID gMaterialClassId;
		JSClassDef gMaterialClass = { "Material", nullptr, nullptr, nullptr, nullptr };

		Material* materialSelf(JSContext* ctx, JSValueConst self) {
			return static_cast<Material*>(JS_GetOpaque2(ctx, self, gMaterialClassId));
		}

		// magic: 0 metallic, 1 roughness, 2 ambientOcclusion, 3 emissive
		JSValue materialGet(JSContext* ctx, JSValueConst self, int magic) {
			Material* m = materialSelf(ctx, self);
			if (!m) return JS_EXCEPTION;
			return JS_NewFloat64(ctx, pendingMaterialScalar(*m, magic));
		}
		JSValue materialSet(JSContext* ctx, JSValueConst self, JSValueConst value, int magic) {
			Material* m = materialSelf(ctx, self);
			if (!m) return JS_EXCEPTION;
			double f = 0;
			if (JS_ToFloat64(ctx, &f, value)) return JS_EXCEPTION;
			ScriptCommand command;
			command.kind = ScriptCommand::Kind::MaterialScalar;
			command.material = m;
			command.index = magic;
			command.value.x = static_cast<float>(f);
			scriptQueue().record(command);
			return JS_UNDEFINED;
		}

		// baseColor is a Vec3 of the RGB; alpha is left alone because nothing yet blends.
		JSValue materialGetColor(JSContext* ctx, JSValueConst self) {
			Material* m = materialSelf(ctx, self);
			if (!m) return JS_EXCEPTION;
			return makeVec3(ctx, pendingMaterialColor(*m));
		}
		JSValue materialSetColor(JSContext* ctx, JSValueConst self, JSValueConst value) {
			Material* m = materialSelf(ctx, self);
			if (!m) return JS_EXCEPTION;
			auto* v = static_cast<Vec3Data*>(JS_GetOpaque2(ctx, value, gVec3ClassId));
			if (!v) return JS_EXCEPTION;
			ScriptCommand command;
			command.kind = ScriptCommand::Kind::MaterialColor;
			command.material = m;
			command.value = v->v;
			scriptQueue().record(command);
			return JS_UNDEFINED;
		}

		const JSCFunctionListEntry gMaterialProto[] = {
			JS_CGETSET_MAGIC_DEF("metallic", materialGet, materialSet, 0),
			JS_CGETSET_MAGIC_DEF("roughness", materialGet, materialSet, 1),
			JS_CGETSET_MAGIC_DEF("ambientOcclusion", materialGet, materialSet, 2),
			JS_CGETSET_MAGIC_DEF("emissive", materialGet, materialSet, 3),
			JS_CGETSET_DEF("baseColor", materialGetColor, materialSetColor),
		};

		JSValue makeMaterial(JSContext* ctx, const Material* material) {
			if (!material) return JS_NULL;
			JSValue proto = JS_GetClassProto(ctx, gMaterialClassId);
			JSValue object = JS_NewObjectProtoClass(ctx, proto, gMaterialClassId);
			JS_FreeValue(ctx, proto);
			// The script mutates the material, so the const is cast away here rather than
			// making every scene reference non-const.
			if (!JS_IsException(object)) JS_SetOpaque(object, const_cast<Material*>(material));
			return object;
		}

		// Materials a script asked for.
		//
		// An Entity holds a bare `const Material*`, so something has to own the ones a
		// script builds and give them a stable address. A deque, because it never moves
		// what it already holds: an entity pointing at the third material must not be
		// invalidated by the fourth being made.
		//
		// They live until releaseScriptMaterials(), rather than being collected with the
		// JS object that named them: the scene may still be drawing with one long after
		// the script has stopped mentioning it.
		std::deque<Material> gScriptMaterials;

		// Reads an optional number off an options object, leaving `into` alone if absent.
		void readOptionalFloat(JSContext* ctx, JSValueConst options, const char* name, float& into) {
			JSValue value = JS_GetPropertyStr(ctx, options, name);
			double number = 0.0;
			if (JS_IsNumber(value) && JS_ToFloat64(ctx, &number, value) == 0) {
				into = static_cast<float>(number);
			}
			JS_FreeValue(ctx, value);
		}

		void readOptionalTexture(JSContext* ctx, JSValueConst options, const char* name,
		                         obj_ref<Texture>& into) {
			JSValue value = JS_GetPropertyStr(ctx, options, name);
			// GetOpaque rather than GetOpaque2: a missing or wrong-typed map is skipped so
			// the neutral default stands in, which is what an absent map means anyway.
			if (auto* ref = static_cast<obj_ref<Texture>*>(JS_GetOpaque(value, gTextureClassId))) {
				into = *ref;
			}
			JS_FreeValue(ctx, value);
		}

		// loadTexture(path, srgb = true) -> Texture, or null when the file is not there.
		//
		// Colour maps are sRGB; data maps — normals, roughness — are not, and decoding
		// one as though it were a photograph is a quiet wrongness rather than an error,
		// so the flag is explicit.
		JSValue jsLoadTexture(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
			if (argc < 1) return JS_ThrowTypeError(ctx, "loadTexture(path, srgb) needs a path");
			const char* path = JS_ToCString(ctx, argv[0]);
			if (!path) return JS_EXCEPTION;
			const bool srgb = (argc < 2) || JS_ToBool(ctx, argv[1]) != 0;

			// Reading a file and putting it on the device is the loop thread's job. From
			// the loop thread this simply runs; from anywhere else it is handed over and
			// waited for, which is what lets this binding survive moving off that thread.
			obj_ref<Texture> loaded;
			const std::string owned(path);
			JS_FreeCString(ctx, path);
			if (!loopWork().request([&] { loaded = loadTexture(owned, srgb); })) {
				RDA_LOG_WARNING("loadTexture: the engine loop did not service the request");
				return JS_NULL;
			}
			// Null rather than an exception: a missing file is something a cell can test
			// for, and throwing would stop a notebook mid-way through setting a scene up.
			if (!loaded.IsValid() || !loaded->isValid()) return JS_NULL;
			return makeTexture(ctx, loaded);
		}

		// createMaterial({ albedo, normal, metallicRoughness,
		//                  metallic, roughness, ambientOcclusion, emissive, baseColor })
		//
		// Every field is optional. A missing map falls back to the engine's neutral
		// default, so the shader never branches on whether one is there.
		//
		// This builds a *new* material rather than editing an existing one, because the
		// maps are baked into a descriptor set when the material is made — changing them
		// afterwards would mean rebuilding that set underneath whatever is drawing with
		// it. Assign the result to an entity to use it.
		JSValue jsCreateMaterial(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
			MaterialTextures maps;
			if (argc >= 1 && JS_IsObject(argv[0])) {
				readOptionalTexture(ctx, argv[0], "albedo", maps.albedo);
				readOptionalTexture(ctx, argv[0], "normal", maps.normal);
				readOptionalTexture(ctx, argv[0], "metallicRoughness", maps.metallicRoughness);
			}

			// Allocating a descriptor set is the loop thread's job too, for the same
			// reason, and by the same route.
			Material material;
			if (!loopWork().request([&] { material = createForwardMaterial(maps); })) {
				RDA_LOG_WARNING("createMaterial: the engine loop did not service the request");
				return JS_NULL;
			}
			if (argc >= 1 && JS_IsObject(argv[0])) {
				readOptionalFloat(ctx, argv[0], "metallic", material.params.metallic);
				readOptionalFloat(ctx, argv[0], "roughness", material.params.roughness);
				readOptionalFloat(ctx, argv[0], "ambientOcclusion", material.params.ambientOcclusion);
				readOptionalFloat(ctx, argv[0], "emissive", material.params.emissive);

				JSValue colour = JS_GetPropertyStr(ctx, argv[0], "baseColor");
				if (auto* v = static_cast<Vec3Data*>(JS_GetOpaque(colour, gVec3ClassId))) {
					material.params.baseColor = glm::vec4(v->v, material.params.baseColor.a);
				}
				JS_FreeValue(ctx, colour);
			}

			gScriptMaterials.push_back(material);
			return makeMaterial(ctx, &gScriptMaterials.back());
		}

		// ---- Entity: a retained scene object -----------------------------------------
		// Backed by a Scene::Entity, whose address is stable precisely so a script can
		// hold on to it. Destroying one from script invalidates any other JS object
		// pointing at it, which is checked below rather than trusted.
		JSClassID gEntityClassId;
		JSClassDef gEntityClass = { "Entity", nullptr, nullptr, nullptr, nullptr };

		// Confirms the entity is still in the scene before touching it, so a reference
		// kept past destroyEntity throws instead of writing to freed memory.
		Entity* entitySelf(JSContext* ctx, JSValueConst self) {
			auto* entity = static_cast<Entity*>(JS_GetOpaque2(ctx, self, gEntityClassId));
			if (!entity) return nullptr;
			// Destroyed by an earlier command in this same script: still in the scene,
			// because the queue has not been applied, but already gone as far as the
			// script is concerned. Saying so here is what keeps the two views agreeing.
			if (pendingDestroyed(*entity)) {
				JS_ThrowReferenceError(ctx, "this entity has been destroyed");
				return nullptr;
			}
			const Scene& scene = getScene();
			for (size_t i = 0; i < scene.entityCount(); ++i) {
				if (scene.entityAt(i) == entity) return entity;
			}
			JS_ThrowReferenceError(ctx, "this entity has been destroyed");
			return nullptr;
		}

		JSValue entityGetPosition(JSContext* ctx, JSValueConst self) {
			Entity* e = entitySelf(ctx, self);
			if (!e) return JS_EXCEPTION;
			return makeVec3(ctx, pendingPosition(*e));
		}
		JSValue entitySetPosition(JSContext* ctx, JSValueConst self, JSValueConst value) {
			Entity* e = entitySelf(ctx, self);
			if (!e) return JS_EXCEPTION;
			auto* v = static_cast<Vec3Data*>(JS_GetOpaque2(ctx, value, gVec3ClassId));
			if (!v) return JS_EXCEPTION;
			ScriptCommand command;
			command.kind = ScriptCommand::Kind::EntityPosition;
			command.entity = e;
			command.value = v->v;
			scriptQueue().record(command);
			return JS_UNDEFINED;
		}

		JSValue entityGetVisible(JSContext* ctx, JSValueConst self) {
			Entity* e = entitySelf(ctx, self);
			if (!e) return JS_EXCEPTION;
			return JS_NewBool(ctx, pendingVisible(*e));
		}
		JSValue entitySetVisible(JSContext* ctx, JSValueConst self, JSValueConst value) {
			Entity* e = entitySelf(ctx, self);
			if (!e) return JS_EXCEPTION;
			ScriptCommand command;
			command.kind = ScriptCommand::Kind::EntityVisible;
			command.entity = e;
			command.value.x = JS_ToBool(ctx, value) != 0 ? 1.0f : 0.0f;
			scriptQueue().record(command);
			return JS_UNDEFINED;
		}

		JSValue entityGetName(JSContext* ctx, JSValueConst self) {
			Entity* e = entitySelf(ctx, self);
			if (!e) return JS_EXCEPTION;
			return JS_NewString(ctx, e->name.c_str());
		}
		JSValue entityGetMaterial(JSContext* ctx, JSValueConst self) {
			Entity* e = entitySelf(ctx, self);
			if (!e) return JS_EXCEPTION;
			return makeMaterial(ctx, pendingMaterial(*e));
		}

		// Points the entity at another material. The entity borrows it — whoever owns the
		// material has to outlive the entity, which holds for one createMaterial() made
		// (they live until releaseScriptMaterials) and for one the application built and
		// keeps. It does not hold for a material belonging to something being torn down.
		JSValue entitySetMaterial(JSContext* ctx, JSValueConst self, JSValueConst value) {
			Entity* e = entitySelf(ctx, self);
			if (!e) return JS_EXCEPTION;
			auto* material = static_cast<Material*>(JS_GetOpaque2(ctx, value, gMaterialClassId));
			if (!material) return JS_EXCEPTION;
			ScriptCommand command;
			command.kind = ScriptCommand::Kind::EntityMaterial;
			command.entity = e;
			command.material = material;
			scriptQueue().record(command);
			return JS_UNDEFINED;
		}

		JSValue entityTranslate(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
			Entity* e = entitySelf(ctx, self);
			if (!e) return JS_EXCEPTION;
			if (argc < 1) return JS_ThrowTypeError(ctx, "translate expects a Vec3");
			auto* v = static_cast<Vec3Data*>(JS_GetOpaque2(ctx, argv[0], gVec3ClassId));
			if (!v) return JS_EXCEPTION;
			ScriptCommand command;
			command.kind = ScriptCommand::Kind::EntityTranslate;
			command.entity = e;
			command.value = v->v;
			scriptQueue().record(command);
			return JS_DupValue(ctx, self); // chainable
		}

		JSValue entityDestroy(JSContext* ctx, JSValueConst self, int, JSValueConst*) {
			Entity* e = entitySelf(ctx, self);
			if (!e) return JS_EXCEPTION;
			ScriptCommand command;
			command.kind = ScriptCommand::Kind::EntityDestroy;
			command.entity = e;
			scriptQueue().record(command);
			return JS_UNDEFINED;
		}

		JSValue entityToString(JSContext* ctx, JSValueConst self, int, JSValueConst*) {
			Entity* e = entitySelf(ctx, self);
			if (!e) return JS_EXCEPTION;
			const glm::vec3 p = pendingPosition(*e);
			char buffer[160];
			std::snprintf(buffer, sizeof(buffer), "Entity('%s' at %g, %g, %g)",
				e->name.c_str(), p.x, p.y, p.z);
			return JS_NewString(ctx, buffer);
		}

		const JSCFunctionListEntry gEntityProto[] = {
			JS_CGETSET_DEF("position", entityGetPosition, entitySetPosition),
			JS_CGETSET_DEF("visible", entityGetVisible, entitySetVisible),
			JS_CGETSET_DEF("name", entityGetName, nullptr),
			JS_CGETSET_DEF("material", entityGetMaterial, entitySetMaterial),
			JS_CFUNC_DEF("translate", 1, entityTranslate),
			JS_CFUNC_DEF("destroy", 0, entityDestroy),
			JS_CFUNC_DEF("toString", 0, entityToString),
		};

		JSValue makeEntity(JSContext* ctx, Entity* entity) {
			if (!entity) return JS_NULL;
			JSValue proto = JS_GetClassProto(ctx, gEntityClassId);
			JSValue object = JS_NewObjectProtoClass(ctx, proto, gEntityClassId);
			JS_FreeValue(ctx, proto);
			if (!JS_IsException(object)) JS_SetOpaque(object, entity);
			return object;
		}

		// ---- the `scene` global ------------------------------------------------------
		JSValue sceneEntities(JSContext* ctx, JSValueConst, int, JSValueConst*) {
			const Scene& scene = getScene();
			JSValue array = JS_NewArray(ctx);
			for (size_t i = 0; i < scene.entityCount(); ++i) {
				JS_SetPropertyUint32(ctx, array, static_cast<uint32_t>(i),
					makeEntity(ctx, scene.entityAt(i)));
			}
			return array;
		}

		JSValue sceneFind(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
			if (argc < 1) return JS_ThrowTypeError(ctx, "find expects a name");
			const char* name = JS_ToCString(ctx, argv[0]);
			if (!name) return JS_EXCEPTION;
			Entity* entity = getScene().findEntity(name);
			JS_FreeCString(ctx, name);
			return makeEntity(ctx, entity); // null when there is no such entity
		}

		JSValue sceneGetSun(JSContext* ctx, JSValueConst) {
			return makeLight(ctx, &getScene().sun);
		}
		JSValue sceneGetCount(JSContext* ctx, JSValueConst) {
			return JS_NewInt32(ctx, static_cast<int32_t>(getScene().items().size()));
		}
		JSValue sceneGetCamera(JSContext* ctx, JSValueConst) {
			return makeVec3(ctx, getScene().camera.position);
		}

		const JSCFunctionListEntry gSceneProps[] = {
			JS_CGETSET_DEF("sun", sceneGetSun, nullptr),
			JS_CGETSET_DEF("drawCount", sceneGetCount, nullptr),
			JS_CGETSET_DEF("cameraPosition", sceneGetCamera, nullptr),
			JS_CFUNC_DEF("entities", 0, sceneEntities),
			JS_CFUNC_DEF("find", 1, sceneFind),
		};

		// For engine-owned types. The class is still published so its name resolves and
		// `instanceof` works, but calling it is refused: a script reaches these objects
		// through the engine, it does not make them.
		JSValue notConstructible(JSContext* ctx, JSValueConst, int, JSValueConst*) {
			return JS_ThrowTypeError(ctx, "this type is created by the engine, not by script");
		}

		// Registers one class: id, definition, prototype, and (optionally) a constructor
		// published on the global object.
		void registerClass(JSContext* ctx, JSValue global, JSClassID* id, JSClassDef* def,
		                   const JSCFunctionListEntry* proto, size_t protoCount,
		                   JSCFunction* ctor, const char* name, int ctorArgs) {
			JS_NewClassID(JS_GetRuntime(ctx), id);
			JS_NewClass(JS_GetRuntime(ctx), *id, def);

			JSValue prototype = JS_NewObject(ctx);
			JS_SetPropertyFunctionList(ctx, prototype, proto, static_cast<int>(protoCount));
			JS_SetClassProto(ctx, *id, prototype);

			if (ctor) {
				JSValue constructor = JS_NewCFunction2(ctx, ctor,
					name, ctorArgs, JS_CFUNC_constructor, 0);
				// Links constructor.prototype to the class prototype in both directions,
				// which is what makes `instanceof` work.
				JS_SetConstructor(ctx, constructor, prototype);
				JS_SetPropertyStr(ctx, global, name, constructor);
			}
		}
	}

	void releaseScriptMaterials() {
		if (gScriptMaterials.empty()) return;
		// Their descriptor sets may still be bound by a frame in flight.
		rendeerWaitIdle();
		for (Material& material : gScriptMaterials) releaseForwardMaterial(material);
		gScriptMaterials.clear();
	}

	bool ScriptEngine::bindEngine() {
		if (!mContext) return false;
		JSContext* ctx = mContext;
		JSValue global = JS_GetGlobalObject(ctx);

		registerClass(ctx, global, &gVec3ClassId, &gVec3Class,
			gVec3Proto, sizeof(gVec3Proto) / sizeof(gVec3Proto[0]),
			vec3Constructor, "Vec3", 3);

		// Published so `scene.sun instanceof DirectionalLight` holds and the type shows up
		// in a debugger, but not constructible — the engine owns the only light.
		registerClass(ctx, global, &gLightClassId, &gLightClass,
			gLightProto, sizeof(gLightProto) / sizeof(gLightProto[0]),
			notConstructible, "DirectionalLight", 0);

		// Not constructible: a Texture comes from loadTexture(), which is what actually
		// reads a file and puts it on the device.
		registerClass(ctx, global, &gTextureClassId, &gTextureClass,
			gTextureProto, sizeof(gTextureProto) / sizeof(gTextureProto[0]),
			notConstructible, "Texture", 0);

		registerClass(ctx, global, &gMaterialClassId, &gMaterialClass,
			gMaterialProto, sizeof(gMaterialProto) / sizeof(gMaterialProto[0]),
			notConstructible, "Material", 0);

		registerClass(ctx, global, &gEntityClassId, &gEntityClass,
			gEntityProto, sizeof(gEntityProto) / sizeof(gEntityProto[0]),
			notConstructible, "Entity", 0);

		JSValue scene = JS_NewObject(ctx);
		JS_SetPropertyFunctionList(ctx, scene, gSceneProps,
			sizeof(gSceneProps) / sizeof(gSceneProps[0]));
		JS_SetPropertyStr(ctx, global, "scene", scene);

		// Free functions rather than constructors: both do real work — reading a file,
		// allocating a descriptor set — and `new` reads like neither.
		JS_SetPropertyStr(ctx, global, "loadTexture",
			JS_NewCFunction(ctx, jsLoadTexture, "loadTexture", 2));
		JS_SetPropertyStr(ctx, global, "createMaterial",
			JS_NewCFunction(ctx, jsCreateMaterial, "createMaterial", 1));

		// From here a script's changes are recorded rather than performed, and this is
		// what performs them: on success in one step, on failure not at all.
		mCommit = [](bool succeeded) -> size_t {
			ScriptQueue& queue = scriptQueue();   // this script's own, not the engine's
			if (!succeeded) {
				queue.discard();
				return 0;
			}
			// Applying reaches into the scene, so it happens on the loop thread. Called
			// from there — which is every script today — this is the apply itself; called
			// from a script thread it is handed over and waited for. Either way the change
			// lands somewhere it is safe to land.
			size_t applied = 0;
			if (!loopWork().request([&] { applied = queue.apply(); })) {
				RDA_LOG_WARNING("script commands were not applied: the engine loop did not "
				                "service the commit");
				queue.discard();   // the transaction failed; it does not get to half-happen
				return 0;
			}
			return applied;
		};

		JS_FreeValue(ctx, global);
		return true;
	}
}
