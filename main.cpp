#include "include/app.hpp"
#include "include/backend.hpp"
#include "include/common.hpp"
#include "include/ecs.hpp"
#include "include/io/event.hpp"
#include "include/layers/font_renderer.hpp"
#include "include/layers/optix_tracer.cuh"
#include "include/layers/raster.hpp"
#include "include/layers/raytracer.hpp"
#include "include/layers/shape_renderer.hpp"
#include "include/logger.hpp"
#include "include/renderer.hpp"
#include "include/scene.hpp"
#include "include/transform.hpp"
#include "include/types.hpp"
#include "include/ui/button.hpp"
#include "include/ui/color_picker.hpp"
#include "include/ui/slider.hpp"
#include "tinyfiledialogs.h"

#include <stb/stb_image_write.h>

using namespace kobra;

// Scene path
// std::string scene_path = "~/models/sponza/scene.kobra";
std::string scene_path = "/home/venki/models/cornell_box.kobra";
// std::string scene_path = "scenes/ggx.kobra";

// Test app
struct ECSApp : public BaseApp {
	layers::Raster	rasterizer;
	layers::Raytracer raytracer;
	layers::FontRenderer font_renderer;
	layers::ShapeRenderer shape_renderer;
	layers::OptixTracer optix_tracer;

	Scene scene;

	// TODO: will later also need a project manager
	Entity camera;

	// TODO: Gaps are managed by layout manager

	static constexpr glm::vec2 window_size {1900, 1100};
	static constexpr float scene_graph_width = 400;
	static constexpr float component_panel_width = 400;
	static constexpr float project_explorer_height = 300;
	static constexpr glm::vec2 render_min = {scene_graph_width, 5.0f};
	static constexpr glm::vec2 render_max = {
		window_size.x - component_panel_width,
		window_size.y - project_explorer_height + 5.0f
	};

	// TODO: theme manager, with fonts, etc

	// Scene graph
	struct SceneGraph {
		ui::Rect r_background;

		std::vector <ui::Button> b_entities;

		ECS &ecs;
		layers::FontRenderer &fr;
		io::MouseEventQueue &mouse_events;

		SceneGraph(ECS &ecs_, layers::FontRenderer &fr_, io::MouseEventQueue &mouse_events_)
				: ecs(ecs_), fr(fr_), mouse_events(mouse_events_) {
			// TODO: modify this constructor for the rectangle
			r_background.min = glm::vec2 {5.0f, 5.0f};
			r_background.max = glm::vec2 {scene_graph_width - 5, window_size.y - 5};
			r_background.color = glm::vec3 {0.6f};
			r_background.radius = 0.005f;
		}

		int selected_entity = -1;
		std::vector <ui::Rect *> shapes() {
			// TODO: rectangle border color
			std::vector <ui::Rect *> shapes {&r_background};

			// TODO: assuming that ECS didnt change if the size
			// didnt
			if (b_entities.size() != ecs.size()) {
				b_entities.resize(ecs.size());

				float x = 10;
				float y = 10;

				for (int i = 0; i < ecs.size(); i++) {
					auto &e = ecs.get_entity(i);

					struct _on_click {
						int index;

						void operator()(void *user, glm::vec2) {
							int *i = (int *) user;
							*i = index;
						}
					};

					ui::Button::Args button_args {};

					button_args.min = {10, y},
					button_args.max = {scene_graph_width - 10, y + 30.0f},
					button_args.radius = 0.005f,
					button_args.idle = glm::vec3 {0.6, 0.6, 0.7},
					button_args.hover = glm::vec3 {0.7, 0.7, 0.8},
					button_args.pressed = glm::vec3 {0.65, 0.65, 0.75},
					button_args.on_click = {{&selected_entity, _on_click {i}}},

					b_entities[i] = ui::Button(mouse_events, button_args);

					y += 40.0f;
				}
			}

			for (int i = 0; i < b_entities.size(); i++) {
				auto s = b_entities[i].shape();

				if (i == selected_entity)
					s->color = glm::vec3 {0.65, 0.65, 0.75};

				shapes.push_back(s);
			}

			return shapes;
		}

		std::vector <ui::Text> texts() {
			std::vector <ui::Text> texts;

			float x = 10;
			float y = 10;

			float minh = 30.0f;

			for (int i = 0; i < ecs.size(); i++) {
				auto &e = ecs.get_entity(i);

				auto t = ui::Text(
					e.name,
					{x + 5.0f, y},
					glm::vec3 {0.2},
					0.4f
				);

				// Center from y to y + minh
				float h = fr.size(t).y;
				t.anchor.y = y + (minh - h) / 2.0f;

				y += 40.0f;

				texts.push_back(t);
			}

			return texts;
		}
	};

	SceneGraph scene_graph;

	ui::ColorPicker color_picker;
	glm::vec3 color;

	ui::Rect r_background;
	ui::Button capture_button;

	ECSApp(const vk::raii::PhysicalDevice &phdev, const std::vector <const char *> &extensions)
			: BaseApp(phdev, "ECSApp",
				vk::Extent2D {(uint32_t) window_size.x, (uint32_t) window_size.y},
				extensions, vk::AttachmentLoadOp::eLoad
			),
			rasterizer(get_context(), vk::AttachmentLoadOp::eClear),
			raytracer(get_context(), &sync_queue, vk::AttachmentLoadOp::eClear),
			optix_tracer(get_context(), vk::AttachmentLoadOp::eClear,
				int(render_max.x - render_min.x), int(render_max.y - render_min.y)),
			font_renderer(get_context(), render_pass, "resources/fonts/noto_sans.ttf"),
			shape_renderer(get_context(), render_pass),
			scene_graph(scene.ecs, font_renderer, io.mouse_events) {
		scene.load(get_device(), scene_path);
		// raytracer.environment_map(scene.p_environment_map);
		rasterizer.environment_map("resources/skies/sunrise.jpg");
		raytracer.environment_map("resources/skies/sunrise.jpg");
		optix_tracer.environment_map("resources/skies/sunrise.jpg");

		// Camera
		camera = scene.ecs.get_entity("Camera");

		// TODO: set camera properties (aspect)
		camera.get <Camera> ().aspect = (render_max.x - render_min.x)/(render_max.y - render_min.y);

		// Color picker
		color = glm::vec3 {0.86f, 0.13f, 0.13f};
		color_picker = ui::ColorPicker {
			io.mouse_events,
			ui::ColorPicker::Args {
				.min = {window_size.x - component_panel_width, 200},
				.max = {window_size.x, 200 + 250.0f},
				.label = "diffuse",
				.ref = &color,
				.font_renderer = &font_renderer
			}
		};

		r_background = ui::Rect(
			glm::vec2 {window_size.x - component_panel_width + 5, 5},
			glm::vec2 {window_size.x - 5, window_size.y - 5},
			glm::vec3 {0.6f},
			0.005f
		);

		auto capture_ftn = [&](void *user, glm::vec2) {
			static const std::string path = "capture.png";
			auto *app = (ECSApp *) user;
			std::cout << "Capture button pressed" << std::endl;
			std::vector <uint8_t> pixels = app->optix_tracer.capture();

			stbi_flip_vertically_on_write(true);

			int width = app->optix_tracer.width;
			int height = app->optix_tracer.height;
			std::cout << "\tCapture size: " << width << "x" << height << std::endl;
			stbi_write_png(path.c_str(),
				width, height, 4, pixels.data(),
				width * 4
			);
		};

		ui::Button::Args button_args {
			{window_size.x - component_panel_width + 5, 5},
			{window_size.x - 5, 5 + 30.0f},
			0.005f, 0,
			glm::vec3 {0.7, 0.6, 0.6},
			glm::vec3 {0.8, 0.7, 0.7},
			glm::vec3 {0.75, 0.65, 0.65},
			GLFW_MOUSE_BUTTON_LEFT,
			{{this, capture_ftn}}
		};

		capture_button = ui::Button(io.mouse_events, button_args);

		// Input callbacks
		io.mouse_events.subscribe(mouse_callback, this);

		scene.ecs.info <Mesh> ();
	}

	int mode = 0;	// 0 for raster, 1 for raytracer, 2 for OptiX
	std::vector <std::string> mode_strs {
		"Rasterize", "Raytrace", "OptiX"
	};

	bool tab_pressed = false;

	void active_input() {
		float speed = 20.0f * frame_time;

		// Camera movement
		// TODO: remove transform component from camera?
		auto &transform = camera.get <Transform> ();

		glm::vec3 forward = transform.forward();
		glm::vec3 right = transform.right();
		glm::vec3 up = transform.up();

		if (io.input.is_key_down(GLFW_KEY_W))
			transform.move(forward * speed);
		else if (io.input.is_key_down(GLFW_KEY_S))
			transform.move(-forward * speed);

		if (io.input.is_key_down(GLFW_KEY_A))
			transform.move(-right * speed);
		else if (io.input.is_key_down(GLFW_KEY_D))
			transform.move(right * speed);

		if (io.input.is_key_down(GLFW_KEY_E))
			transform.move(up * speed);
		else if (io.input.is_key_down(GLFW_KEY_Q))
			transform.move(-up * speed);

		// Switch mode on tab
		if (io.input.is_key_down(GLFW_KEY_TAB)) {
			if (!tab_pressed) {
				tab_pressed = true;
				mode = (mode + 1) % mode_strs.size();
			}
		} else {
			tab_pressed = false;
		}

		// Also switch on numbers
		if (io.input.is_key_down(GLFW_KEY_1))
			mode = 0;
		else if (io.input.is_key_down(GLFW_KEY_2))
			mode = 1;
		else if (io.input.is_key_down(GLFW_KEY_3))
			mode = 2;
	}

	float fps = 0;
	float time = 0;

	void record(const vk::raii::CommandBuffer &cmd,
			const vk::raii::Framebuffer &framebuffer) override {
		// Time things
		if (frame_time > 0)
			fps = (fps + 1.0f/frame_time) / 2.0f;

		time += frame_time;

		// Text things
		std::vector <ui::Text> texts {
			ui::Text(
				common::sprintf("%s mode: %.2f fps", mode_strs[mode].c_str(), fps),
				{scene_graph_width + 5, 5},
				glm::vec3 {1.0f}, 0.4f
			),
		};

		for (auto &t : scene_graph.texts())
			texts.push_back(t);

		for (auto &t : color_picker.texts())
			texts.push_back(t);

		// Shape things
		std::vector <ui::Rect *> rects {&r_background};

		for (auto &s : scene_graph.shapes())
			rects.push_back(s);

		for (auto &s : color_picker.shapes())
			rects.push_back(s);

		rects.push_back(capture_button.shape());

		// Input
		active_input();

		// Begin command buffer
		cmd.begin({});

		// TODO: pass camera
		if (mode == 0)
			rasterizer.render(cmd, framebuffer, scene.ecs, {render_min, render_max});
		else if (mode == 1)
			raytracer.render(cmd, framebuffer, scene.ecs, {render_min, render_max});
		else if (mode == 2)
			optix_tracer.render(cmd, framebuffer, scene.ecs, {render_min, render_max});

		// Start render pass
		std::array <vk::ClearValue, 2> clear_values = {
			vk::ClearValue {
				vk::ClearColorValue {
					std::array <float, 4> {0.0f, 0.0f, 0.0f, 1.0f}
				}
			},
			vk::ClearValue {
				vk::ClearDepthStencilValue {
					1.0f, 0
				}
			}
		};

		cmd.beginRenderPass(
			vk::RenderPassBeginInfo {
				*render_pass,
				*framebuffer,
				vk::Rect2D {
					vk::Offset2D {0, 0},
					extent,
				},
				static_cast <uint32_t> (clear_values.size()),
				clear_values.data()
			},
			vk::SubpassContents::eInline
		);

		shape_renderer.render(cmd, rects);
		font_renderer.render(cmd, texts);

		// TODO: ui layer  will have a push interface each frame

		cmd.endRenderPass();

		cmd.end();

		// V-sync
		sync_queue.push([]() {});
	}

	void terminate() override {
		if (io.input.is_key_down(GLFW_KEY_ESCAPE))
			glfwSetWindowShouldClose(window.handle, true);
	}

	// Mouse callback
	static void mouse_callback(void *us, const io::MouseEvent &event) {
		static const int pan_button = GLFW_MOUSE_BUTTON_MIDDLE;
		static const int alt_pan_button = GLFW_MOUSE_BUTTON_LEFT;
		static const int select_button = GLFW_MOUSE_BUTTON_LEFT;

		static const float sensitivity = 0.001f;

		static bool first_movement = true;
		static bool dragging = false;
		static bool dragging_select = false;
		static bool gizmo_dragging = false;

		static float px = 0.0f;
		static float py = 0.0f;

		static glm::vec2 previous_dir {0.0f, 0.0f};

		static float yaw = 0.0f;
		static float pitch = 0.0f;

		auto &app = *static_cast <ECSApp *> (us);
		auto &transform = app.camera.get <Transform> ();

		// Deltas and directions
		float dx = event.xpos - px;
		float dy = event.ypos - py;
		glm::vec2 dir {dx, dy};

		// Dragging only with the drag button
		// TODO: alt left dragging as ewll
		bool is_drag_button = (event.button == pan_button);
		if (event.action == GLFW_PRESS && is_drag_button)
			dragging = true;
		else if (event.action == GLFW_RELEASE && is_drag_button)
			dragging = false;

		// Pan only when draggign
		if (dragging) {
			yaw -= dx * sensitivity;
			pitch -= dy * sensitivity;

			if (pitch > 89.0f)
				pitch = 89.0f;
			if (pitch < -89.0f)
				pitch = -89.0f;

			transform.rotation.x = pitch;
			transform.rotation.y = yaw;
		}

		// Update previous position
		px = event.xpos;
		py = event.ypos;

		previous_dir = dir;
	}
};

int main()
{
	auto extensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
		VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
		VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
	};

	auto predicate = [&extensions](const vk::raii::PhysicalDevice &dev) {
		return physical_device_able(dev, extensions);
	};

	// Choose a physical device
	// TODO: static lambda (FIRST)
	auto phdev = pick_physical_device(predicate);

	/* auto camera = Camera {
		Transform { {2, 2, 6}, {-0.1, 0.3, 0} },
		Tunings { 45.0f, 800, 800 }
	}; */

	std::cout << "Extensions:" << std::endl;
	for (auto str : extensions)
		std::cout << "\t" << str << std::endl;

	// Create the app and run it
	ECSApp app(phdev, {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
		VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
	});
	// RTApp app(phdev, extensions);
	// engine::RTCapture app(phdev, {1000, 1000}, extensions, scene_path, camera);

	// Run the app
	app.run();
}
