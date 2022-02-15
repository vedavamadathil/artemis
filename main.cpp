// Standard headers
#include <cstring>
#include <iostream>
#include <thread>
#include <vulkan/vulkan_core.h>

// Local headers
#include "global.hpp"

// List of objet materials
Material materials[] {
	glm::vec3 {0.1f, 0.5f, 0.2f},
	glm::vec3 {0.9f, 0.5f, 0.2f},
	glm::vec3 {0.4f, 0.4f, 0.9f},
	glm::vec3 {0.5f, 0.1f, 0.6f},
	glm::vec3 {0.6f, 0.5f, 0.3f},
	glm::vec3 {1.0f, 0.5f, 1.0f},
	{glm::vec3 {1.0f, 1.0f, 1.0f}, SHADING_TYPE_LIGHT},
};

// List of object transforms
Transform transforms[] {
	glm::vec3 {-1.0f, 0.0f, 4.0f},
	glm::vec3 {0.5f, 5.0f, 3.0f},
	glm::vec3 {6.0f, -2.0f, 5.0f},
	glm::vec3 {6.0f, 3.0f, 11.5f},
	glm::vec3 {6.0f, 3.0f, -2.0f},
	glm::vec3 {0.0f, 0.0f, 0.0f}
};

World world {
	// Camera
	Camera {
		Transform {
			glm::vec3(0.0f, 0.0f, -4.0f)
		},
	 
		Tunings {
			45.0f, 800, 600
		}
	},

	// Primitives
	// TODO: later read from file
	std::vector <World::PrimitivePtr> {
		World::PrimitivePtr(new Sphere(0.25f, transforms[0], materials[6])),
		World::PrimitivePtr(new Sphere(1.0f, transforms[0], materials[0])),
		World::PrimitivePtr(new Sphere(3.0f, transforms[1], materials[1])),
		World::PrimitivePtr(new Sphere(6.0f, transforms[2], materials[2])),
		World::PrimitivePtr(new Sphere(2.0f, transforms[3], materials[3])),
		World::PrimitivePtr(new Sphere(2.0f, transforms[4], materials[4])),

		// Cube mesh
		World::PrimitivePtr(new Mesh <VERTEX_TYPE_POSITION> (
			{
				glm::vec3(0.0f, 0.0f, -1.0f),
				glm::vec3(1.0f, 0.0f, -1.0f),
				glm::vec3(1.0f, 1.0f, -1.0f),
				glm::vec3(0.0f, 1.0f, -1.0),
				glm::vec3(0.0f, 0.0f, 1.0f),
				glm::vec3(1.0f, 0.0f, 1.0f),
				glm::vec3(1.0f, 1.0f, 1.0f),
				glm::vec3(0.0f, 1.0f, 1.0f)
			},
			{
				0, 1, 2,	0, 2, 3,
				4, 5, 6,	4, 6, 7,
				0, 4, 7,	0, 7, 3,
				1, 5, 6,	1, 6, 2,
				0, 1, 4,	1, 4, 5,
				2, 6, 7,	2, 7, 3
			},
			materials[1]
		))
	},

	// Lights
	std::vector <World::LightPtr> {
		World::LightPtr(new PointLight(transforms[0], 0.0f))
	}
};

// Print aligned_vec4
// TODO: common header
inline std::ostream& operator<<(std::ostream& os, const glm::vec4 &v)
{
	return (os << "(" << v.x << ", " << v.y
		<< ", " << v.z << ", " << v.w << ")");
}

inline std::ostream &operator<<(std::ostream &os, const aligned_vec4 &v)
{
	return (os << v.data);
}

// TODO: put the following functions into a alloc.cpp file

// Minimum sizes
#define INITIAL_OBJECTS		100UL
#define INITIAL_LIGHTS		100UL
#define INITIAL_MATERIALS	100UL

// Sizes of objects and lights
// are assumed to be the maximum
static const size_t MAX_OBJECT_SIZE = sizeof(Triangle);
static const size_t MAX_LIGHT_SIZE = sizeof(PointLight);


// Size of the world data, including indices
size_t world_data_size()
{
	size_t objects = std::max(world.objects.size(), INITIAL_OBJECTS);
	size_t lights = std::max(world.lights.size(), INITIAL_LIGHTS);

	return sizeof(GPUWorld) + 4 * (objects + lights);
}
// Copy buffer helper
struct MapInfo {
	uint8_t *ptr;
	size_t size;
	bool resized;
};

MapInfo map_world_buffer(Vulkan *vk, Buffer &objects, Buffer &lights, Buffer &materials)
{
	// Static (cached) raw memory buffer
	static uint8_t *buffer = nullptr;
	static size_t buffer_size = 0;
	
	static const VkBufferUsageFlags buffer_usage =
		VK_BUFFER_USAGE_TRANSFER_DST_BIT
		| VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

	// Check buffer resize requirement
	size_t size = world_data_size();

	// Resize buffer if required
	if (size > buffer_size) {
		buffer_size = size;
		buffer = (uint8_t *) realloc(buffer, buffer_size);
	}

	// Generate world data and write to buffers
	Indices indices;
	world.write_objects(objects, materials, indices);
	world.write_lights(lights, indices);

	// Copy world and indices
	GPUWorld gworld = world.dump();
	gworld.objects = objects.size();

	memcpy(buffer, &gworld, sizeof(GPUWorld));
	memcpy(buffer + sizeof(GPUWorld), indices.data(),
		4 * indices.size());

	/* Dump contents of the buffers
	// TODO: ImGui option
	std::cout << "=== Objects: " << objects.size() << " ===" << std::endl;
	for (size_t i = 0; i < objects.size(); i++)
		std::cout << i << ":\t" << objects[i] << std::endl;
	std::cout << "Lights: " << lights.size() << std::endl;
	for (size_t i = 0; i < lights.size(); i++)
		std::cout << lights[i] << std::endl;
	std::cout << "Materials: " << materials.size() << std::endl;
	for (size_t i = 0; i < materials.size(); i++)
		std::cout << materials[i] << std::endl;
	std::cout << "Indices: " << indices.size() << std::endl;
	for (size_t i = 0; i < indices.size(); i++)
		std::cout << indices[i] << std::endl;

	// Dump buffer contents (as uvec4)
	std::cout << "=== Buffer contents ===" << std::endl;
	for (size_t i = 0; i < buffer_size; i += 4 * sizeof(uint32_t)) {
		uint32_t *uptr = (uint32_t *) (buffer + i);
		float *fptr = (float *) (buffer + i);

		std::cout << uptr[0] << " " << uptr[1] << " "
			<< uptr[2] << " " << uptr[3]
			<< " -> ("
			<< fptr[0] << ", " << fptr[1] << ", "
			<< fptr[2] << ", " << fptr[3]
			<< ")" << std::endl;
	} */
	
	// Resizing and remaking buffers
	int resized = 0;

	// World
	if (size > world_buffer.size) {
		// Resize vulkan buffer
		vk->destroy_buffer(world_buffer);
		vk->make_buffer(world_buffer, buffer_size, buffer_usage);
		resized++;
	}

	// Objects
	if (sizeof(aligned_vec4) * objects.size() > objects_buffer.size) {
		// Resize vulkan buffer
		vk->destroy_buffer(objects_buffer);
		vk->make_buffer(objects_buffer, objects.size() * sizeof(aligned_vec4), buffer_usage);
		resized++;
	}

	// Lights
	if (sizeof(aligned_vec4) * lights.size() > lights_buffer.size) {
		// Resize vulkan buffer
		vk->destroy_buffer(lights_buffer);
		vk->make_buffer(lights_buffer, lights.size() * sizeof(aligned_vec4), buffer_usage);
		resized++;
	}

	/* Update descriptor sets
	if (resized) {
		for (size_t i = 0; i < vk->swch_images.size(); i++)
			descriptor_set_maker(vk, i);
		vk->set_command_buffers(cmd_buffer_maker);
	} */

	// Return pointer to the buffer
	return {buffer, buffer_size, (resized > 0)};
}

// Map all the buffers
// TODO: deal with resizing buffers
bool map_buffers(Vulkan *vk)
{
	// Create and write to buffers
	Buffer objects;
	Buffer lights;
	Buffer materials;

	auto wb = map_world_buffer(vk, objects, lights, materials);

	// Map buffers
	vk->map_buffer(&world_buffer, wb.ptr, wb.size);
	vk->map_buffer(&objects_buffer, objects.data(), sizeof(aligned_vec4) * objects.size());
	vk->map_buffer(&materials_buffer, materials.data(), sizeof(Material) * materials.size());
	vk->map_buffer(&lights_buffer, lights.data(), sizeof(aligned_vec4) * lights.size());

	return wb.resized;
}

// Allocate buffers
void allocate_buffers(Vulkan &vulkan)
{
	// Allocate buffers
	size_t pixel_size = 4 * 800 * 600;
	size_t world_size = world_data_size();
	size_t objects_size = MAX_OBJECT_SIZE * std::max(world.objects.size(), INITIAL_OBJECTS);
	size_t lights_size = MAX_LIGHT_SIZE * std::max(world.lights.size(), INITIAL_LIGHTS);
	size_t materials_size = sizeof(Material) * INITIAL_MATERIALS;

	static const VkBufferUsageFlags buffer_usage =
		VK_BUFFER_USAGE_TRANSFER_DST_BIT
		| VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

	// Create buffers
	vulkan.make_buffer(pixel_buffer,   pixel_size,   buffer_usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
	vulkan.make_buffer(world_buffer,   world_size,   buffer_usage);
	vulkan.make_buffer(objects_buffer, objects_size, buffer_usage);
	vulkan.make_buffer(lights_buffer,  lights_size,  buffer_usage);
	vulkan.make_buffer(materials_buffer, materials_size, buffer_usage);
	
	// Add all buffers to deletion queue
	vulkan.push_deletion_task(
		[&](Vulkan *vk) {
			vk->destroy_buffer(pixel_buffer);
			vk->destroy_buffer(world_buffer);
			vk->destroy_buffer(objects_buffer);
			vk->destroy_buffer(lights_buffer);
			vk->destroy_buffer(materials_buffer);
			Logger::ok("[main] Deleted buffers");
		}
	);
}

// App
class SampleScene : public mercury::App {
	// TODO: some of these member should be moved back to App
	VkPhysicalDevice		physical_device; // TODO: goes to App
	Vulkan::Device			device;

	VkRenderPass			render_pass;
	VkCommandPool			command_pool;

	std::vector <VkCommandBuffer>	command_buffers;

	VkDescriptorPool		descriptor_pool;
	VkDescriptorSetLayout		descriptor_set_layout;
	VkDescriptorSet			descriptor_set;

	VkShaderModule			compute_shader;

	// Sync objects
	std::vector <VkFence>		in_flight_fences;
	std::vector <VkFence>		images_in_flight;

	std::vector <VkSemaphore>	smph_image_available;
	std::vector <VkSemaphore>	smph_render_finished;

	// Profiler
	mercury::Profiler		profiler;

	// Fill out command buffer
	void maker(const Vulkan *vk, size_t i) {
		// Get image at current index
		VkImage image = swapchain.images[i];

		// Transition image layout to present
		// TODO: method to transition image layout,
		//	includes pipeline barrier functoin exeution
		VkImageMemoryBarrier image_memory_barrier {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = image,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
		};

		vkCmdPipelineBarrier(
			command_buffers[i],
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			0,
			0,
			nullptr,
			0,
			nullptr,
			1,
			&image_memory_barrier
		);

		// Buffer copy regions
		VkBufferImageCopy buffer_copy_region {
			.bufferOffset = 0,
			.bufferRowLength = 0,
			.bufferImageHeight = 0,
			.imageSubresource = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = 0,
				.baseArrayLayer = 0,
				.layerCount = 1
			},
			.imageOffset = {0, 0, 0},
			.imageExtent = {
				swapchain.extent.width,
				swapchain.extent.height,
				1
			}
		};

		// Copy buffer to image
		vkCmdCopyBufferToImage(
			command_buffers[i],
			pixel_buffer.buffer,
			image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&buffer_copy_region
		);
		
		// Render pass creation info
		VkRenderPassBeginInfo render_pass_info {
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = render_pass,
			.framebuffer = swapchain.framebuffers[i],
			.renderArea = {
				.offset = {0, 0},
				.extent = swapchain.extent
			},
			.clearValueCount = 0,
			.pClearValues = nullptr
		};

		// Render pass creation
		// TODO: use method to being and end render pass
		vkCmdBeginRenderPass(
			command_buffers[i],
			&render_pass_info,
			VK_SUBPASS_CONTENTS_INLINE
		);

			// Create pipeline
			VkPipelineLayoutCreateInfo pipeline_layout_info {
				.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
				.setLayoutCount = 1,
				.pSetLayouts = &descriptor_set_layout,
				.pushConstantRangeCount = 0,
				.pPushConstantRanges = nullptr
			};

			VkPipelineLayout pipeline_layout;

			VkResult res = vkCreatePipelineLayout(
				vk->device,
				&pipeline_layout_info,
				nullptr,
				&pipeline_layout
			);

			if (res != VK_SUCCESS) {
				std::cerr << "Failed to create pipeline layout" << std::endl;
				return;
			}

			// Execute compute shader on the pixel buffer
			VkPipeline pipeline;

			VkComputePipelineCreateInfo compute_pipeline_info {
				.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
				.stage = {
					.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
					.stage = VK_SHADER_STAGE_COMPUTE_BIT,
					.module = compute_shader,
					.pName = "main"
				},
				.layout = pipeline_layout
			};

			res = vkCreateComputePipelines(
				vk->device,
				VK_NULL_HANDLE,
				1,
				&compute_pipeline_info,
				nullptr,
				&pipeline
			);

			if (res != VK_SUCCESS) {
				std::cerr << "Failed to create compute pipeline" << std::endl;
				return;
			}

			// Bind pipeline
			vkCmdBindPipeline(
				command_buffers[i],
				VK_PIPELINE_BIND_POINT_COMPUTE,
				pipeline
			);

			// Bind buffer
			vkCmdBindDescriptorSets(
				command_buffers[i],
				VK_PIPELINE_BIND_POINT_COMPUTE,
				pipeline_layout,
				0, 1, &descriptor_set,
				0, nullptr
			);

		// TODO: use the methods
		vkCmdEndRenderPass(command_buffers[i]);
			
		// Dispatch
		vkCmdDispatch(
			command_buffers[i],
			swapchain.extent.width,
			swapchain.extent.height, 1
		);
	}
public:
	// Constructor
	SampleScene(Vulkan *vk) : mercury::App({
		.ctx = vk,
		.width = 800,
		.height = 600,
		.name = "Mercury - Sample Scene",
	}) {
		// Select the physical device
		physical_device = ctx->select_phdev(surface);

		// Create a logical device
		device = ctx->make_device(physical_device, surface);

		// Create render pass
		render_pass = ctx->make_render_pass(
			VK_ATTACHMENT_LOAD_OP_LOAD,
			VK_ATTACHMENT_STORE_OP_STORE
		);

		// Create framebuffers
		ctx->make_framebuffers(swapchain, render_pass);

		// Create command pool
		command_pool = ctx->make_command_pool(
			VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
		);

		// Create descriptor pool
		descriptor_pool = ctx->make_descriptor_pool();

		// Create descriptor set layout
		descriptor_set_layout = ctx->make_descriptor_set_layout(dsl_bindings);

		// Create descriptor set
		descriptor_set = ctx->make_descriptor_set(
			descriptor_pool,
			descriptor_set_layout
		);

		// Load compute shader
		compute_shader = ctx->make_shader("shaders/pixel.spv");

		// Create sync objects
		// TODO: use max frames in flight
		images_in_flight.resize(swapchain.images.size(), VK_NULL_HANDLE);
		for (size_t i = 0; i < 2; i++) {
			in_flight_fences.push_back(ctx->make_fence(VK_FENCE_CREATE_SIGNALED_BIT));
			smph_image_available.push_back(ctx->make_semaphore());
			smph_render_finished.push_back(ctx->make_semaphore());
		}

		// GLFW callbacks
		glfwSetKeyCallback(surface.window, key_callback);
		glfwSetInputMode(surface.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		glfwSetCursorPosCallback(surface.window, mouse_callback);
	}

	// Update the world
	void update_world() {
		static float time = 0.0f;

		// Update light position
		float amplitude = 7.0f;
		glm::vec3 position {
			amplitude * sin(time), 7.0f,
			amplitude * cos(time)
		};

		world.objects[0]->transform.position = position;
		world.lights[0]->transform.position = position;

		// Map buffers
		if (map_buffers(ctx)) {
			update_descriptor_set();
			update_command_buffers();
		}

		// Update time
		// ImGuiIO &io = ImGui::GetIO();
		// time += io.DeltaTime;
		time += 0.01f;
		// TODO: use delta time from App class
	}

	// Present the frame
	void present() {
		// Wait for the next image in the swap chain
		vkWaitForFences(
			ctx->device, 1,
			&in_flight_fences[frame_index],
			VK_TRUE, UINT64_MAX
		);

		// Acquire the next image from the swap chain
		uint32_t image_index;
		VkResult result = vkAcquireNextImageKHR(
			ctx->device, swapchain.swch, UINT64_MAX,
			smph_image_available[frame_index],
			VK_NULL_HANDLE, &image_index
		);

		// Check if the swap chain is no longer valid
		if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			// TODO: recreate swap chain
			// _remk_swapchain();
			return;
		} else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
			Logger::error("[Vulkan] Failed to acquire swap chain image!");
			throw (-1);
		}

		// Check if the image is being used by the current frame
		profiler.frame("Acquire image");
		if (images_in_flight[image_index] != VK_NULL_HANDLE) {
			vkWaitForFences(
				ctx->device, 1,
				&images_in_flight[image_index],
				VK_TRUE, UINT64_MAX
			);
		}
		profiler.end();

		// Mark the image as in use by this frame
		images_in_flight[image_index] = in_flight_fences[frame_index];

		// Frame submission and synchronization info
		VkSemaphore wait_semaphores[] = {
			smph_image_available[frame_index]
		};

		VkPipelineStageFlags wait_stages[] = {
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
		};

		VkSemaphore signal_semaphores[] = {
			smph_render_finished[frame_index]
		};

		// Create information
		// TODO: method
		VkSubmitInfo submit_info {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = wait_semaphores,
			.pWaitDstStageMask = wait_stages,

			.commandBufferCount = 1,
			.pCommandBuffers = &command_buffers[image_index],

			.signalSemaphoreCount = 1,
			.pSignalSemaphores = signal_semaphores
		};

		// Submit the command buffer
		vkResetFences(ctx->device, 1, &in_flight_fences[frame_index]);
	
		profiler.frame("Queue submit");
		result = vkQueueSubmit(
			ctx->graphics_queue, 1, &submit_info,
			in_flight_fences[frame_index]
		);
		profiler.end();

		if (result != VK_SUCCESS) {
			Logger::error("[Vulkan] Failed to submit draw command buffer!");
			throw (-1);
		}
		
		// Present the image to the swap chain
		VkSwapchainKHR swchs[] = {swapchain.swch};
		
		VkPresentInfoKHR present_info {
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = signal_semaphores,
			.swapchainCount = 1,
			.pSwapchains = swchs,
			.pImageIndices = &image_index,
			.pResults = nullptr
		};

		profiler.frame("vkQueuePresentKHR");
		result = vkQueuePresentKHR(ctx->present_queue, &present_info);
		profiler.end();
		
		/* if (result == VK_ERROR_OUT_OF_DATE_KHR
				|| result == VK_SUBOPTIMAL_KHR
				|| framebuffer_resized) {
			framebuffer_resized = false;
			_remk_swapchain();
		} else*/
		
		// TODO: check resizing (in app)
		if (result != VK_SUCCESS) {
			Logger::error("[Vulkan] Failed to present swap chain image!");
			throw (-1);
		}
	}

	void frame() override {
		// Start profiling a new frame
		profiler.frame("Frame");

		// Update world
		profiler.frame("Update world");
			update_world();
		profiler.end();

		// Present the frame
		profiler.frame("Present frame");
			present();
		profiler.end();

		// End profiling the frame and display it
		profiler.end();

		auto frame = profiler.pop();
		std::cout << mercury::Profiler::pretty(frame) << std::endl;
	}

	void update_command_buffers() {
		// Set command buffers
		auto ftn = [this](const Vulkan *ctx, size_t i) {
			// TODO: maker should be a virtual function
			this->maker(ctx, i);
		};

		ctx->set_command_buffers(
			swapchain, command_pool,
			command_buffers, ftn
		);
	}

	// TODO: make some cleaner method
	void update_descriptor_set() {
		VkDescriptorBufferInfo pb_info {
			.buffer = pixel_buffer.buffer,
			.offset = 0,
			.range = pixel_buffer.size
		};
		
		VkDescriptorBufferInfo wb_info {
			.buffer = world_buffer.buffer,
			.offset = 0,
			.range = world_buffer.size
		};
		
		VkDescriptorBufferInfo ob_info {
			.buffer = objects_buffer.buffer,
			.offset = 0,
			.range = objects_buffer.size
		};

		VkDescriptorBufferInfo lb_info {
			.buffer = lights_buffer.buffer,
			.offset = 0,
			.range = lights_buffer.size
		};
		
		VkDescriptorBufferInfo mt_info {
			.buffer = materials_buffer.buffer,
			.offset = 0,
			.range = materials_buffer.size
		};

		VkWriteDescriptorSet pb_write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = descriptor_set,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo = &pb_info
		};
		
		VkWriteDescriptorSet wb_write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = descriptor_set,
			.dstBinding = 1,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo = &wb_info
		};
		
		VkWriteDescriptorSet ob_write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = descriptor_set,
			.dstBinding = 2,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo = &ob_info
		};

		VkWriteDescriptorSet lb_write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = descriptor_set,
			.dstBinding = 3,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo = &lb_info
		};

		VkWriteDescriptorSet mt_write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = descriptor_set,
			.dstBinding = 4,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo = &mt_info
		};

		VkWriteDescriptorSet writes[] = {
			pb_write,
			wb_write,
			ob_write,
			lb_write,
			mt_write
		};

		vkUpdateDescriptorSets(
			ctx->device, 5,
			&writes[0],
			0, nullptr
		);
	}

	// Desctiptor set layout bindings
	static const std::vector <VkDescriptorSetLayoutBinding> dsl_bindings;
};

const std::vector <VkDescriptorSetLayoutBinding> SampleScene::dsl_bindings = {
	VkDescriptorSetLayoutBinding {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
		.pImmutableSamplers = nullptr
	},

	VkDescriptorSetLayoutBinding {
		.binding = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
		.pImmutableSamplers = nullptr
	},
	
	VkDescriptorSetLayoutBinding {
		.binding = 2,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
		.pImmutableSamplers = nullptr
	},
	
	VkDescriptorSetLayoutBinding {
		.binding = 3,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
		.pImmutableSamplers = nullptr
	},

	VkDescriptorSetLayoutBinding {
		.binding = 4,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
		.pImmutableSamplers = nullptr
	}
};

int main()
{
	// Redirect logger to file
	// Logger::switch_file("mercury.log");

	/* mercury::Model <VERTEX_TYPE_POSITION> model("resources/suzanne.obj");
	model[0] = materials[2];

	world.objects.push_back(std::shared_ptr <mercury::Model <VERTEX_TYPE_POSITION>> (
		new mercury::Model <VERTEX_TYPE_POSITION> (model)
	));

	Logger::ok() << "[main] Loaded model with "
		<< model.mesh_count() << " meshe(s), "
		<< model.mesh(0).vertex_count() << " vertices, "
		<< model.mesh(0).triangle_count() << " triangles" << std::endl; */

	// Initialize Vulkan
	Vulkan vulkan;

	// Initialize the buffers
	allocate_buffers(vulkan);

	// Create sample scene
	SampleScene scene(&vulkan);

	/* Compute shader descriptor
	compute_shader = vulkan.make_shader("shaders/pixel.spv");

	// Add shader to deletion queue'
	vulkan.push_deletion_task(
		[&](Vulkan *vk) {
			vkDestroyShaderModule(vk->device, compute_shader, nullptr);
			Logger::ok("[main] Deleted compute shader");
		}
	); */

	scene.update_descriptor_set();
	scene.update_command_buffers();
	scene.run();
}
