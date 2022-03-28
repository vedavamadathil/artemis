// Standard headers
#include <cstring>
#include <iostream>
#include <thread>

#define KOBRA_VALIDATION_LAYERS
#define KOBRA_ERROR_ONLY
#define KOBRA_THROW_ERROR

// Local headers
#include "include/logger.hpp"
#include "include/model.hpp"
#include "include/raster/layer.hpp"
#include "include/raster/mesh.hpp"
#include "include/scene.hpp"
#include "include/vertex.hpp"
#include "profiler.hpp"

using namespace kobra;

// Rasterization app
class RasterApp : public BaseApp {
	raster::Layer layer;

	static void mouse_movement(void *user, const io::MouseEvent &event) {
		static const float sensitivity = 0.001f;

		static bool first_movement = true;

		static float px = 0.0f;
		static float py = 0.0f;

		static float yaw = 0.0f;
		static float pitch = 0.0f;

		float dx = event.xpos - px;
		float dy = event.ypos - py;

		px = event.xpos;
		py = event.ypos;
		if (first_movement) {
			first_movement = false;
			return;
		}

		Camera *camera = (Camera *) user;

		yaw -= dx * sensitivity;
		pitch -= dy * sensitivity;

		if (pitch > 89.0f)
			pitch = 89.0f;
		if (pitch < -89.0f)
			pitch = -89.0f;

		camera->transform.rotation.x = pitch;
		camera->transform.rotation.y = yaw;
	}

	Scene scene;
public:
	RasterApp(Vulkan *vk) : BaseApp({
		vk,
		800, 800, 2,
		"Rasterization"
	}, true) {
		// Load meshes
		Model model1("resources/benchmark/bunny_res_1.ply");
		Model model2("resources/benchmark/suzanne.obj");

		// Plane mesh
		Mesh plane(VertexList {
			Vertex {{-10, -1, -10}, {0, 0.7, 0.3}, {0, 0}},
			Vertex {{-10, -1, 10}, {0, 0.7, 0.3}, {0, 1}},
			Vertex {{10, -1, 10}, {0, 0.7, 0.3}, {1, 1}},
			Vertex {{10, -1, -10}, {0, 0.7, 0.3}, {1, 0}}
		}, {
			0, 1, 2,
			0, 2, 3
		});

		Mesh cube = Mesh::make_box({0, 0, 0}, {1, 1.2, 1.2});

		raster::Mesh *mesh1 = new raster::Mesh(window.context, model1[0]);
		raster::Mesh *mesh2 = new raster::Mesh(window.context, model2[0]);
		raster::Mesh *mesh3 = new raster::Mesh(window.context, plane);
		raster::Mesh *mesh4 = new raster::Mesh(window.context, cube);
		raster::Mesh *mesh5 = new raster::Mesh(window.context, cube);
		raster::Mesh *mesh6 = new raster::Mesh(window.context, cube);
		raster::Mesh *mesh7 = new raster::Mesh(window.context, model2[0]);

		mesh5->transform().move(glm::vec3(0, 0, -5));
		mesh6->transform().move(glm::vec3(-5, 0, 0));

		mesh1->transform() = Transform({0.0f, 0.0f, -4.0f});
		mesh1->transform().scale = glm::vec3 {10};

		mesh2->transform() = Transform({0.0f, 4.0f, -4.0f});
		mesh7->transform() = Transform({0.0f, 4.0f, 4.0f});
		// mesh2->transform().scale = glm::vec3(1/10.0f);

		KOBRA_LOG_FILE(notify) << "Loaded all models and meshes\n";

		// Initialize layer
		Camera camera {
			Transform { {0, 0, 4} },
			Tunings { 45.0f, 800, 800 }
		};

		layer = raster::Layer(window, camera, VK_ATTACHMENT_LOAD_OP_CLEAR);
		layer.add(mesh1);
		layer.add(mesh2);
		layer.add(mesh3);
		layer.add(mesh4);
		layer.add(mesh5);
		layer.add(mesh6);
		layer.add(mesh7);

		scene = Scene({
			mesh1,
			mesh2,
			mesh3,
			mesh4,
			mesh5,
			mesh6,
			mesh7
		});

		scene.save("scene.kobra");

		// Input callbacks
		window.mouse_events->subscribe(mouse_movement, &layer.camera());
		window.cursor_mode(GLFW_CURSOR_DISABLED);
	}

	// Override record method
	void record(const VkCommandBuffer &cmd, const VkFramebuffer &framebuffer) override {
		// Start recording command buffer
		Vulkan::begin(cmd);

		// WASDEQ movement
		float speed = 20.0f * frame_time;

		glm::vec3 forward = layer.camera().transform.forward();
		glm::vec3 right = layer.camera().transform.right();
		glm::vec3 up = layer.camera().transform.up();

		if (input.is_key_down(GLFW_KEY_W))
			layer.camera().transform.move(forward * speed);
		else if (input.is_key_down(GLFW_KEY_S))
			layer.camera().transform.move(-forward * speed);

		if (input.is_key_down(GLFW_KEY_A))
			layer.camera().transform.move(-right * speed);
		else if (input.is_key_down(GLFW_KEY_D))
			layer.camera().transform.move(right * speed);

		if (input.is_key_down(GLFW_KEY_E))
			layer.camera().transform.move(up * speed);
		else if (input.is_key_down(GLFW_KEY_Q))
			layer.camera().transform.move(-up * speed);

		// Record commands
		layer.render(cmd, framebuffer);

		// End recording command buffer
		Vulkan::end(cmd);
	}

	// Termination method
	void terminate() override {
		// Check input
		if (window.input->is_key_down(GLFW_KEY_ESCAPE))
			glfwSetWindowShouldClose(surface.window, true);
	}
};

int main()
{
	Vulkan *vulkan = new Vulkan();
	Profiler *pf = new Profiler();
	RasterApp raster_app {vulkan};
	raster_app.run();
	delete vulkan;
}
