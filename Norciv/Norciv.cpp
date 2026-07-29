#include <RendeerAlpha.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glfw3.h> // for the GLFW_KEY_* codes the input layer indexes by
#include <vector>
#include <chrono>

// A unit cube centered on the origin. Normals are per-face, so each face needs its
// own four vertices (24 total) rather than 8 shared corners.
static void makeCube(std::vector<RDA::Vertex>& vertices, std::vector<uint32_t>& indices) {
	struct Face { glm::vec3 normal; glm::vec3 a, b, c, d; };
	const Face faces[] = {
		{ {  0,  0,  1 }, { -0.5f, -0.5f,  0.5f }, {  0.5f, -0.5f,  0.5f }, {  0.5f,  0.5f,  0.5f }, { -0.5f,  0.5f,  0.5f } },
		{ {  0,  0, -1 }, {  0.5f, -0.5f, -0.5f }, { -0.5f, -0.5f, -0.5f }, { -0.5f,  0.5f, -0.5f }, {  0.5f,  0.5f, -0.5f } },
		{ {  1,  0,  0 }, {  0.5f, -0.5f,  0.5f }, {  0.5f, -0.5f, -0.5f }, {  0.5f,  0.5f, -0.5f }, {  0.5f,  0.5f,  0.5f } },
		{ { -1,  0,  0 }, { -0.5f, -0.5f, -0.5f }, { -0.5f, -0.5f,  0.5f }, { -0.5f,  0.5f,  0.5f }, { -0.5f,  0.5f, -0.5f } },
		{ {  0,  1,  0 }, { -0.5f,  0.5f,  0.5f }, {  0.5f,  0.5f,  0.5f }, {  0.5f,  0.5f, -0.5f }, { -0.5f,  0.5f, -0.5f } },
		{ {  0, -1,  0 }, { -0.5f, -0.5f, -0.5f }, {  0.5f, -0.5f, -0.5f }, {  0.5f, -0.5f,  0.5f }, { -0.5f, -0.5f,  0.5f } },
	};

	for (const Face& f : faces) {
		uint32_t base = static_cast<uint32_t>(vertices.size());
		vertices.push_back({ f.a, f.normal, { 0.0f, 0.0f } });
		vertices.push_back({ f.b, f.normal, { 1.0f, 0.0f } });
		vertices.push_back({ f.c, f.normal, { 1.0f, 1.0f } });
		vertices.push_back({ f.d, f.normal, { 0.0f, 1.0f } });
		indices.insert(indices.end(), { base, base + 1, base + 2, base + 2, base + 3, base });
	}
}

class FPSCounter {
public:
	void update() {
		frameCount++;
		auto now = std::chrono::high_resolution_clock::now();
		double elapsed = std::chrono::duration<double>(now - lastTime).count();

		if (elapsed >= 1.0) {  // update once per second
			fps = frameCount / elapsed;
			frameCount = 0;
			lastTime = now;
		}
	}

	double getFPS() const { return fps; }

private:
	std::chrono::high_resolution_clock::time_point lastTime =
		std::chrono::high_resolution_clock::now();
	int frameCount = 0;
	double fps = 0.0;
};

FPSCounter counter;

int main() {
	RDA::obj_ref<RDA::Mesh> cube;
	RDA::Material material;
	float spin = 0.0f;
	float spinSpeed = 1.0f;
	bool spinning = true;
	glm::vec3 cameraEye{ 2.5f, 2.0f, 3.5f };

	RDA::AppConfig config;
	config.app.name = "Norciv";
	config.app.appVersion = 1;
	config.threadMode = RDA::ThreadMode::Caller;
	config.viewportMode = RDA::ViewportMode::Widget; // scene renders into the Scene dock below
	// Render only when something changed. The GUI is tracked by the engine; the spinning
	// cube is not, so onUpdate asks for a frame while it is animating. Untick "Spin cube"
	// and the app goes fully idle — no GPU work at all until something moves again.
	config.redrawMode = RDA::RedrawMode::OnDemand;
	config.gui.themePath = "res/themes/norciv.xml";         // named widget variants
	config.gui.languagesPath = "res/themes/languages.xml"; // extra highlighting languages

	config.onStart = [&]() {
		std::vector<RDA::Vertex> vertices;
		std::vector<uint32_t> indices;
		makeCube(vertices, indices);

		cube = RDA::createMesh();
		cube->upload(vertices, indices);
		material = RDA::createForwardMaterial();

		RDA::Window* window = getMainWindow();
		VkExtent2D extent = window->cachedExtent();
		float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);

		RDA::Scene& scene = RDA::getScene();
		scene.camera.setPerspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);

		RDA::Gui& gui = getMainWindow()->gui();

		// Define container prototypes — described once, not shown until spawned. Each
		// spawn() makes an independent instance with its own widget state.
		gui.docking().define("inspector", "Inspector", [](RDA::DockContainer& c) {
			RDA::Label* l = c.add<RDA::Label>("lbl", "Inspector");
			l->rect = { 12.0f, 10.0f, 0.0f, 0.0f };
			RDA::Slider* s = c.add<RDA::Slider>("val");
			s->rect = { 12.0f, 40.0f, 244.0f, 16.0f };
			s->minValue = 0.0f; s->maxValue = 1.0f; s->value = 0.5f;
			s->variant = "warm";
			RDA::Checkbox* cb = c.add<RDA::Checkbox>("flag", "Enabled");
			cb->rect = { 12.0f, 70.0f, 200.0f, 20.0f };
			cb->value = true;
			cb->variant = "accent"; // spawned instances carry the variant too
		});
		gui.docking().define("notes", "Notes", [](RDA::DockContainer& c) {
			RDA::TextField* t = c.add<RDA::TextField>("text", RDA::TextFieldMode::Document);
			t->variant = "notes";             // rounded, bordered prose area
			t->text = "type notes here...";   // no rect => fills the container, resizes with it
		});

		// Dockable "Controls" container — starts docked to the left edge. Drag its title
		// bar to float it or re-dock it to another edge; drag its inner edge to resize.
		RDA::DockContainer* controls = gui.docking().add("controls", "Controls");
		controls->dock = RDA::DockSide::Left;
		controls->dockSize = 244.0f;

		// Widgets pick a theme variant by name (defined in res/themes/norciv.xml). An
		// unset variant stays "default", so only the styled ones below change.
		RDA::Checkbox* spinBox = controls->add<RDA::Checkbox>("spin", "Spin cube");
		spinBox->rect = { 14.0f, 12.0f, 200.0f, 20.0f };
		spinBox->value = spinning;
		spinBox->variant = "accent"; // green check
		spinBox->onChange = [&spinning](bool v) { spinning = v; };

		RDA::Label* speedLabel = controls->add<RDA::Label>("speedlbl", "Speed");
		speedLabel->rect = { 14.0f, 42.0f, 0.0f, 0.0f };
		speedLabel->variant = "muted";
		RDA::Slider* speed = controls->add<RDA::Slider>("speed");
		speed->rect = { 14.0f, 64.0f, 214.0f, 16.0f };
		speed->minValue = 0.0f; speed->maxValue = 4.0f; speed->value = spinSpeed;
		speed->variant = "warm"; // orange fill
		speed->onChange = [&spinSpeed](float v) { spinSpeed = v; };

		RDA::Label* nameLabel = controls->add<RDA::Label>("namelbl", "Name (Line input)");
		nameLabel->rect = { 14.0f, 92.0f, 0.0f, 0.0f };
		nameLabel->variant = "muted";
		RDA::TextField* name = controls->add<RDA::TextField>("name", RDA::TextFieldMode::Line);
		name->text = "cube";
		name->rect = { 14.0f, 114.0f, 214.0f, 26.0f };

		// Spawn buttons: each click adds a new instance of a prototype to the screen.
		RDA::Button* addInspector = controls->add<RDA::Button>("addinsp", "New Inspector");
		addInspector->rect = { 14.0f, 150.0f, 214.0f, 26.0f };
		addInspector->variant = "primary";
		addInspector->onClick = []() { getMainWindow()->gui().docking().spawn("inspector"); };

		RDA::Button* addNotes = controls->add<RDA::Button>("addnotes", "New Notes");
		addNotes->rect = { 14.0f, 182.0f, 214.0f, 26.0f };
		addNotes->variant = "ghost"; // outlined variant: border + rounding
		addNotes->onClick = []() { getMainWindow()->gui().docking().spawn("notes"); };

		RDA::Button* quit = controls->add<RDA::Button>("quit", "Quit");
		quit->rect = { 14.0f, 220.0f, 214.0f, 28.0f };
		quit->variant = "danger"; // red destructive action
		quit->onClick = []() { rendeerStop(); };

		// Dockable "Scene" container — docked Center; its Viewport widget displays the 3D
		// scene (rendered offscreen because viewportMode == Widget) and fills the body.
		RDA::DockContainer* view = gui.docking().add("viewport", "Scene");
		view->dock = RDA::DockSide::Center;
		view->add<RDA::Viewport>("vp"); // no rect => fills the container body

		// Dockable "Editor" container — starts floating; drag it to an edge to dock it.
		// Holds a Code-style (IDE) text field with a line-number gutter.
		RDA::DockContainer* editor = gui.docking().add("editor", "Editor");
		editor->dock = RDA::DockSide::Floating;
		editor->floatingRect = { 300.0f, 48.0f, 320.0f, 300.0f };
		RDA::TextField* code = editor->add<RDA::TextField>("code", RDA::TextFieldMode::Code);
		// The "ide" variant carries the whole editor look *and* its language, so the
		// mode set above is replaced by the themed one. Swap to "ide-cpp" for C++.
		code->variant = "ide";
		// No rect => fills the container body and resizes with it.
		code->text =
			"# drag my title bar to dock me\n"
			"# resize me from the bottom-right grip\n"
			"import math\n"
			"\n"
			"@dataclass\n"
			"class Orbit:\n"
			"    \"\"\"A body on a circular orbit.\n"
			"    Triple-quoted strings span lines.\n"
			"    \"\"\"\n"
			"    radius: float = 1.5\n"
			"    speed:  float = 0x1F\n"
			"\n"
			"    def position(self, t):\n"
			"        angle = t * self.speed * 1e-3\n"
			"        if angle > math.pi and not self.locked:\n"
			"            return None\n"
			"        return (math.cos(angle), math.sin(angle))\n";

		// Restore where things were docked last run (no-op the first time).
		gui.docking().loadLayoutFromFile("dock_layout.ini");
	};

	// Persist the dock layout on exit, so rearranging survives a restart.
	config.onShutdown = []() {
		getMainWindow()->gui().docking().saveLayoutToFile("dock_layout.ini");
	};

	config.onUpdate = [&](float dt) {
		RDA::Gui& gui = getMainWindow()->gui();
		RDA::Input& in = getMainWindow()->input();

		// Polled camera control — suppressed while a text field has focus, so typing
		// WASD into the editor doesn't also fly the camera.
		if (!gui.hasKeyboardFocus()) {
			float step = 3.0f * dt;
			if (in.isKeyDown(GLFW_KEY_W)) cameraEye.z -= step;
			if (in.isKeyDown(GLFW_KEY_S)) cameraEye.z += step;
			if (in.isKeyDown(GLFW_KEY_A)) cameraEye.x -= step;
			if (in.isKeyDown(GLFW_KEY_D)) cameraEye.x += step;
			if (in.keyPressed(GLFW_KEY_ESCAPE)) rendeerStop();
		}

		// The engine cannot see that the scene is animating (in Widget mode the GUI just
		// samples the same texture every frame), so ask for a frame while the cube turns.
		if (spinning) {
			spin += dt * spinSpeed;
			rendeerRequestRedraw();
		}
		glm::mat4 transform = glm::rotate(glm::mat4(1.0f), spin, glm::vec3(0.0f, 1.0f, 0.0f));

		RDA::Scene& scene = RDA::getScene();
		// Match the camera to the Viewport widget's aspect so the scene isn't stretched.
		RDA::Rect vp = gui.viewportRect();
		float aspect = (vp.w > 0.0f && vp.h > 0.0f) ? vp.w / vp.h : 4.0f / 3.0f;
		scene.camera.setPerspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);
		scene.camera.lookAt(cameraEye, { 0.0f, 0.0f, 0.0f });

		// One item this frame: rebuild the draw list each tick so the rotation shows.
		scene.clear();
		scene.add(cube, material, transform);

		counter.update();
		// Hybrid: an immediate-mode line over everything, like a debug overlay.
		VkExtent2D ext = getMainWindow()->cachedExtent();
		gui.label(std::to_string(counter.getFPS()).c_str(), {ext.width * 0.5f - 28.0f, 8.0f}, RDA::rgba(150, 200, 150));
	};

	rendeerRun(config);
	return 0;
}
