#include <RendeerAlpha.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glfw3.h> // for the GLFW_KEY_* codes the input layer indexes by
#include <vector>

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

int main() {
	RDA::obj_ref<RDA::Mesh> cube;
	RDA::Material material;
	float spin = 0.0f;
	bool spinning = true;
	glm::vec3 cameraEye{ 2.5f, 2.0f, 3.5f };

	RDA::AppConfig config;
	config.app.name = "Norciv";
	config.app.appVersion = 1;
	config.threadMode = RDA::ThreadMode::Caller;

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
	};

	// Discrete-event path: dispatched during the poll, on the loop thread.
	config.onUpdate = [&](float dt) {
		// Polled-state path: read the window's Input each frame for continuous input.
		RDA::Input& in = getMainWindow()->input();
		float step = 3.0f * dt;
		if (in.isKeyDown(GLFW_KEY_W)) cameraEye.z -= step;
		if (in.isKeyDown(GLFW_KEY_S)) cameraEye.z += step;
		if (in.isKeyDown(GLFW_KEY_A)) cameraEye.x -= step;
		if (in.isKeyDown(GLFW_KEY_D)) cameraEye.x += step;
		if (in.keyPressed(GLFW_KEY_ESCAPE)) rendeerStop();

		if (spinning) spin += dt;
		glm::mat4 transform = glm::rotate(glm::mat4(1.0f), spin, glm::vec3(0.0f, 1.0f, 0.0f));

		RDA::Scene& scene = RDA::getScene();
		scene.camera.lookAt(cameraEye, { 0.0f, 0.0f, 0.0f });

		// One item this frame: rebuild the draw list each tick so the rotation shows.
		scene.clear();
		scene.add(cube, material, transform);

		// Immediate-mode GUI: re-declared every frame; buttons return their click.
		RDA::Gui& gui = getMainWindow()->gui();
		gui.beginPanel("settings", { 20.0f, 20.0f, 240.0f, 150.0f });
			gui.label("Rendeer Alpha", { 34.0f, 34.0f });
			if (gui.button("spin", spinning ? "Pause Spin" : "Resume Spin", { 34.0f, 66.0f, 212.0f, 34.0f })) {
				spinning = !spinning;
			}
			if (gui.button("quit", "Quit", { 34.0f, 108.0f, 212.0f, 34.0f })) {
				rendeerStop();
			}
		gui.endPanel();
	};

	rendeerRun(config);
	return 0;
}
