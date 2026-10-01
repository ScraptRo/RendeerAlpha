#pragma once
#include <cstdint>
#include <GraphicalObjects/GuiTypes.h>
#include <string>
#include <unordered_map>
#include <vector>

// A widget's geometry, authored rather than drawn in C++.
//
// The shape of every widget in this engine is a literal in a .cpp: a button is a rounded
// rect because Gui::button draws one, a dock tab is a filled strip with an "x" because
// Docking.cpp draws that. Every colour of it is themeable and none of its *shape* is, so
// a look that is not made of rectangles needs an engine change.
//
// A mesh asset is that shape as data. It is built from a TypeScript graph at compile
// time and it cannot see anything at run time except the rectangle the layout gave the
// widget -- which is the constraint that makes it cheap:
//
//     pos.x = rect.x + ax * rect.w + ox
//     pos.y = rect.y + ay * rect.h + oy
//
// Two fused multiply-adds per vertex. Nine-slice scaling generalised from nine
// rectangles to any mesh. `ax` is what decides whether a feature stretches with the
// widget or keeps its size: a corner at ax=0 with ox=+8 stays an 8-pixel radius at any
// width, and the same corner at ax=0.04 goes elliptical as soon as the button is wide.
//
// What it deliberately cannot do is change its topology. Every state -- normal, hover,
// pressed -- is the same vertices at different places, which is exactly what lets the
// runtime *blend* between them instead of swapping: a mesh that could grow a vertex
// could not be interpolated, and the animation is most of the point.
namespace RDA {

	// One vertex, as it sits on disk and in memory. Resolved against a rect into an
	// ordinary GuiVertex when it is drawn -- see WidgetMesh::resolve.
	struct MeshVertex {
		float    ax = 0.0f, ay = 0.0f;   // anchor within the chosen rect
		float    ox = 0.0f, oy = 0.0f;   // pixel offset; may fall outside the rect
		float    u = 0.0f, v = 0.0f;     // atlas coordinates
		uint32_t groups = 0;             // 32 vertex groups, one bit each
		// Which rect this anchors to. Always 0 today -- a mesh sees one rectangle. The
		// byte is reserved because a mesh that reaches into its neighbour's rect is the
		// next thing this wants to do, and widening a vertex afterwards means rebuilding
		// every asset in every project that has one.
		uint8_t  rect = 0;
		// Which of the mesh's parameters colours it, or kNoParam for the colour the
		// widget would have used anyway.
		uint8_t  param = 255;
		uint16_t flags = 0;
	};

	static constexpr uint8_t kNoMeshParam = 255;

	// A named rectangle beside the geometry, anchored the same way. The mesh does not own
	// input -- it says where input goes, and the widget still runs its own hit test.
	enum class MeshRegionKind : uint32_t { Hit = 0, Text = 1, Clip = 2 };

	struct MeshRegion {
		std::string    name;
		MeshRegionKind kind = MeshRegionKind::Hit;
		// left, top, right, bottom -- each an anchor and a pixel offset.
		float          box[8] = { 0, 0, 0, 0, 1, 0, 1, 0 };
	};

	// A value the theme supplies. Declared by the mesh rather than by the engine: a
	// mesh-drawn dock wants nothing like `tabHeight` and wants five things the engine
	// never imagined, and a closed schema cannot express that.
	enum class MeshParamKind : uint32_t { Colour = 0, Number = 1 };

	struct MeshParam {
		std::string   name;
		std::string   doc;
		MeshParamKind kind = MeshParamKind::Colour;
		uint32_t      colour = 0;   // the default, when kind == Colour
		float         number = 0.0f;
	};

	class WidgetMesh {
	public:
		bool load(const std::string& path);
		bool parse(const uint8_t* bytes, size_t size, std::string& error);
		bool valid() const { return !mStates.empty(); }

		const std::vector<MeshParam>&  params() const { return mParams; }
		const std::vector<MeshRegion>& regions() const { return mRegions; }
		const std::vector<uint16_t>&   indices() const { return mIndices; }
		const std::vector<std::string>& stateNames() const { return mStates; }
		const std::vector<std::string>& groupNames() const { return mGroups; }
		uint32_t vertsPerState() const { return mVertsPerState; }

		// Which state that name is, or -1. Resolved once by a caller and kept, because a
		// name lookup per frame is exactly the cost this whole design exists to avoid.
		int stateIndex(const std::string& name) const;
		int paramIndex(const std::string& name) const;
		int regionIndex(const std::string& name) const;

		// The rectangle a region covers, given the widget's.
		Rect regionRect(size_t index, const Rect& rect) const;

		// Resolve `from` -- or the blend of `from` and `into` at `t` -- against `rect`,
		// appending to `out`. `colours` is one colour per parameter, already resolved by
		// the theme; `base` is what a vertex with no parameter uses.
		void resolve(int from, int into, float t, const Rect& rect,
		             const uint32_t* colours, size_t colourCount, uint32_t base,
		             std::vector<GuiVertex>& out) const;

	private:
		std::vector<MeshVertex>  mVertices;   // every state, one after another
		std::vector<uint16_t>    mIndices;    // shared: the topology is fixed
		std::vector<std::string> mStates;
		std::vector<std::string> mGroups;
		std::vector<MeshRegion>  mRegions;
		std::vector<MeshParam>   mParams;
		uint32_t mVertsPerState = 0;
	};

	// Meshes by path, loaded once. A theme names the same mesh from several variants and
	// every widget drawn with one asks for it every frame, so the lookup has to be a hash
	// and the file has to be read once.
	class WidgetMeshLibrary {
	public:
		// Null when the file will not load. The failure is logged once per path -- a
		// missing mesh should say so, not say so sixty times a second.
		const WidgetMesh* find(const std::string& path);
		void clear();

	private:
		struct Entry { WidgetMesh mesh; bool ok = false; };
		std::unordered_map<std::string, Entry> mByPath;
	};

	WidgetMeshLibrary& widgetMeshes();
}
