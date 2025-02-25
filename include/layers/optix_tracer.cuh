#ifndef KOBRA_LAYERS_OPTIX_TRACER_H_
#define KOBRA_LAYERS_OPTIX_TRACER_H_

// OptiX headers
#include <optix.h>
#include <optix_stubs.h>

// Engine headers
#include "../backend.hpp"
#include "../cuda/buffer_data.cuh"
#include "../cuda/alloc.cuh"
#include "../ecs.hpp"
#include "../timer.hpp"
#include "../optix/options.cuh"

namespace kobra {

namespace layers {

class OptixTracer {
	// Vulkan context
	Context _ctx;

	// Other Vulkan structures
	vk::raii::RenderPass		_render_pass = nullptr;
	vk::raii::PipelineLayout	_ppl = nullptr;
	vk::raii::Pipeline		_pipeline = nullptr;
	
	// Descriptor set, layout and bindings
	vk::raii::DescriptorSet		_ds_render = nullptr;
	vk::raii::DescriptorSetLayout	_dsl_render = nullptr;
	static const std::vector <DSLB> _dslb_render;

	// OptiX structures
	OptixDeviceContext		_optix_ctx = nullptr;
	OptixModule			_optix_module = nullptr;
	OptixPipeline			_optix_pipeline = nullptr;
	OptixShaderBindingTable		_optix_sbt;
	OptixDenoiser			_optix_denoiser = nullptr;

	CUstream			_optix_stream;

	// OptiX acceleration structure
	OptixTraversableHandle		_optix_traversable = 0;

	// OptiX program groups
	struct {
		OptixProgramGroup	raygen = nullptr;
		OptixProgramGroup	hit_radiance = nullptr;
		OptixProgramGroup	hit_shadow = nullptr;
		OptixProgramGroup	miss_radiance = nullptr;
		OptixProgramGroup	miss_shadow = nullptr;
	} _programs;

	CUdeviceptr			_optix_hg_sbt = 0;
	CUdeviceptr			_optix_miss_sbt = 0;

	// Buffers
	struct {
		CUdeviceptr		quad_lights;
		CUdeviceptr		tri_lights;

		CUdeviceptr		pbuffer; // Color buffer
		CUdeviceptr		nbuffer; // Normal buffer
		CUdeviceptr		abuffer; // Albedo buffer

		CUdeviceptr		fbuffer; // Final buffer

		CUdeviceptr		truncated;

		CUdeviceptr		xoffset;
		CUdeviceptr		yoffset;

		CUdeviceptr		reservoirs;
		CUdeviceptr		prev_reservoirs;
		CUdeviceptr		spatial_reservoirs;
		CUdeviceptr		prev_spatial_reservoirs;
		CUdeviceptr		sampling_radius;

		cuda::BufferData	denoiser_state;
		cuda::BufferData	denoiser_scratch;

		std::vector <float>	h_xoffsets;
		std::vector <float>	h_yoffsets;
	} _buffers;

	// Output resources
	std::vector <uint32_t>		_output;

	// Images
	// TODO: wrap in struct
	cudaExternalMemory_t		_dext_env_map = nullptr;
	CUdeviceptr			_d_environment_map = 0;
	cudaTextureObject_t		_tex_env_map = 0;
	const ImageData			*_v_environment_map = nullptr;

	// Cached entity data
	struct {
		std::vector <const Light *> lights {};
		std::vector <const Transform *> light_transforms {};
		Transform camera_transform {};

		std::vector <const Submesh *> submeshes {};
		std::vector <const Transform *> submesh_transforms {};

		unsigned int num_instances;
	} _cached;

	std::vector <const kobra::Raytracer *>
					_c_raytracers;
	std::vector <Transform>		_c_transforms;

	// Initialize Optix globally
	void _initialize_optix();

	// Allocate necessary CUDA resources
	void _allocate_cuda_resources();

	struct Viewport {
		uint width;
		uint height;
	};

	// Initialize Vulkan resources
	void _initialize_vulkan_structures(const vk::AttachmentLoadOp &load) {
		// Render pass
		_render_pass = make_render_pass(*_ctx.device,
			{_ctx.swapchain_format}, {load},
			_ctx.depth_format, load
		);

		// Create descriptor set layouts
		_dsl_render = make_descriptor_set_layout(*_ctx.device, _dslb_render);

		// Create descriptor sets
		std::array <vk::DescriptorSetLayout, 1> dsls {
			*_dsl_render
		};

		auto dsets = vk::raii::DescriptorSets {
			*_ctx.device,
			{**_ctx.descriptor_pool, dsls}
		};

		_ds_render = std::move(dsets.front());

		// Load all shaders
		auto shaders = make_shader_modules(*_ctx.device, {
			"shaders/bin/generic/postproc_vert.spv",
			"shaders/bin/generic/postproc_frag.spv"
		});

		// Postprocess pipeline
		// TODO: is this even needed?
		auto pcr = vk::PushConstantRange {
			vk::ShaderStageFlagBits::eVertex,
			0, sizeof(Viewport)
		};

		_ppl = vk::raii::PipelineLayout {
			*_ctx.device,
			{{}, *_dsl_render, pcr}
		};

		GraphicsPipelineInfo grp_info {
			*_ctx.device, _render_pass,
			std::move(shaders[0]), nullptr,
			std::move(shaders[1]), nullptr,
			{}, {},
			_ppl
		};

		grp_info.no_bindings = true;
		grp_info.depth_test = false;
		grp_info.depth_write = false;

		_pipeline = make_graphics_pipeline(grp_info);
	}

	// Other helper methods
	void _optix_build();
	void _optix_update_materials();
	void _optix_trace(const Camera &, const Transform &);
public:
	// Default constructor
	OptixTracer() = default;

	int width;
	int height;

	int samples_per_pixel = 1;
	int tonemapping = 0;

	// Resulting vulkan image
	ImageData _result = nullptr;
	vk::raii::Sampler _sampler = nullptr;

	// Staging buffer for OptiX output
	BufferData _staging = nullptr;

	Timer timer;
	int _accumulated = 0;

	// Options
	// TODO: struct
	bool denoiser_enabled = false;
	optix::SamplingStrategies sampling_strategy =
		optix::SamplingStrategies::eDefault;

	// Constructor
	OptixTracer(const Context &, const vk::AttachmentLoadOp &,
		int = 1000, int = 1000);

	// Set environment map
	void environment_map(const std::string &);

	// Perform an interation of path tracing
	void compute(const ECS &);

	// Render
	void render(const vk::raii::CommandBuffer &,
			const vk::raii::Framebuffer &,
			const RenderArea & = {{-1, -1}, {-1, -1}});

	// Capture to buffer
	void capture(std::vector <uint8_t> &);
};

}

}

#endif
