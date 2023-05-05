#pragma once

// Engine headers
#include "include/app.hpp"
#include "include/backend.hpp"
#include "include/common.hpp"
#include "include/layers/common.hpp"
#include "include/layers/forward_renderer.hpp"
#include "include/layers/image_renderer.hpp"
#include "include/layers/mesh_memory.hpp"
#include "include/layers/objectifier.hpp"
#include "include/layers/ui.hpp"
#include "include/project.hpp"
#include "include/scene.hpp"
#include "include/shader_program.hpp"
#include "include/ui/attachment.hpp"
#include "include/engine/irradiance_computer.hpp"
#include "include/amadeus/armada.cuh"
#include "include/amadeus/path_tracer.cuh"
#include "include/amadeus/restir.cuh"
#include "include/layers/framer.hpp"
#include "include/cuda/color.cuh"
#include "include/layers/denoiser.cuh"
#include "include/daemons/transform.hpp"
#include "include/vertex.hpp"

// Editor headers
#include "gbuffer_rtx_shader.cuh"
#include "path_tracer.cuh"

// Native File Dialog
#include <nfd.h>

// ImPlot headers
#include <implot/implot.h>
#include <implot/implot_internal.h>

// ImGuizmo
#include <ImGuizmo/ImGuizmo.h>

// Extra GLM headers
#include <glm/gtc/type_ptr.hpp>

// Aliasing declarations
using namespace kobra;

// Global communications structure
struct Application {
	Context context;
	float speed = 10.0f;
};

// Render packet information
struct RenderInfo {
        Camera camera;
        RenderArea render_area = RenderArea::full();
        Transform camera_transform;
        std::set <int> highlighted_entities;
        vk::Extent2D extent;
        const vk::raii::CommandBuffer &cmd = nullptr;

        RenderInfo(const vk::raii::CommandBuffer &_cmd) : cmd(_cmd) {}
};

// Menu options
struct MenuOptions {
        Camera *camera = nullptr;
        float *speed = nullptr;
};

// Editor render state
struct RenderState {
        enum {
                eTriangulation,
                eWireframe,
                eNormals,
                eTextureCoordinates,
                eAlbedo,
                eSparseGlobalIllumination,
                ePathTraced,
                // ePathTraced_Amadeus
        } mode = eTriangulation;

        enum {
                eRasterized,
                eRaytraced
        } backend = eRasterized;

        bool bounding_boxes = false;
        bool initialized = false;
};

// Modules within the editor rendering pipeline
struct SimplePipeline {
        vk::raii::PipelineLayout pipeline_layout = nullptr;
        vk::raii::Pipeline pipeline = nullptr;

        vk::raii::DescriptorSetLayout dsl = nullptr;
        vk::raii::DescriptorSet dset = nullptr;
};

// Editor rendering
struct EditorViewport {
        // Vulkan structures
        const vk::raii::Device *device = nullptr;
        const vk::raii::PhysicalDevice *phdev = nullptr;
        const vk::raii::DescriptorPool *descriptor_pool = nullptr;
        const vk::raii::CommandPool *command_pool = nullptr;
        TextureLoader *texture_loader = nullptr;

        // Raytracing structures
        std::shared_ptr <amadeus::System> system = nullptr;
        std::shared_ptr <layers::MeshMemory> mesh_memory = nullptr;

        // Buffers
        struct FramebufferImages {
                ImageData viewport = nullptr;
                ImageData position = nullptr;
                ImageData normal = nullptr;
                ImageData uv = nullptr;
                ImageData material_index = nullptr;

                // Vulkan sampler objects
                vk::raii::Sampler position_sampler = nullptr;
                vk::raii::Sampler normal_sampler = nullptr;
                vk::raii::Sampler uv_sampler = nullptr;
                vk::raii::Sampler material_index_sampler = nullptr;

                // CUDA surface objects for write
                cudaSurfaceObject_t cu_position_surface = 0;
                cudaSurfaceObject_t cu_normal_surface = 0;
                cudaSurfaceObject_t cu_uv_surface = 0;
                cudaSurfaceObject_t cu_material_index_surface = 0;
                // cudaSurfaceObject_t cu_color_surface = 0;
        } framebuffer_images;

        DepthBuffer depth_buffer = nullptr;

        vk::raii::RenderPass gbuffer_render_pass = nullptr;
        vk::raii::RenderPass present_render_pass = nullptr;

        vk::raii::Framebuffer gbuffer_fb = nullptr;
        vk::raii::Framebuffer viewport_fb = nullptr;
        // TODO: store the viewport image here instead of in the App...

        // Pipeline resources
        using MeshIndex = std::pair <int, int>; // Entity, mesh index
       
        // G-buffer with rasterization
        struct GBuffer_Rasterized {
                vk::raii::PipelineLayout pipeline_layout = nullptr;
                vk::raii::Pipeline pipeline = nullptr;
       
                vk::raii::DescriptorSetLayout dsl = nullptr;
                std::map <MeshIndex, int> dset_refs;
                std::vector <vk::raii::DescriptorSet> dsets;
        } gbuffer;

        // Common raytracing resources
        struct {
                std::vector <optix::Record <Hit>> records;
                std::map <MeshIndex, int> record_refs;

                std::vector <cuda::_material> materials;
                CUdeviceptr dev_materials = 0;

                Timer timer;
                bool clk_rise = true;
        } common_rtx;

        // G-buffer with raytracing
        struct GBuffer_Raytraced {
                // TODO: compute the tangents and bitangents to correctly do
                // G-buffer raytracing and get valid normals...
                // Program and pipeline
                OptixPipeline pipeline = 0;
                OptixModule module = 0;

                OptixProgramGroup ray_generation = 0;
                OptixProgramGroup closest_hit = 0;
                OptixProgramGroup miss = 0;

                // SBT related resources
                OptixShaderBindingTable sbt = {};

                // std::map <MeshIndex, int> record_refs;
                // std::vector <optix::Record <Hit>> linearized_records;

                // Launch parameters
                GBufferParameters launch_params;
                CUdeviceptr dev_launch_params;
        } gbuffer_rtx;

        // Path tracer
        struct PathTracer {
                // Program and pipeline
                OptixPipeline pipeline = 0;
                OptixModule module = 0;

                OptixProgramGroup ray_generation = 0;
                OptixProgramGroup closest_hit = 0;
                OptixProgramGroup miss = 0;

                // SBT related resources
                OptixShaderBindingTable sbt = {};

                // Lighting information
                std::vector <AreaLight> lights;

                // Launch parameters
                PathTracerParameters launch_params;
                CUdeviceptr dev_launch_params;
               
                // Storage and blitting
                layers::Framer framer;

                CUdeviceptr dev_traced;
                std::vector <uint8_t> traced;
                
                int32_t depth;
        } path_tracer;

        // Path tracer (Amadeus)
        // struct AmadeusPathTracer {
        //         std::shared_ptr <amadeus::ArmadaRTX> armada;
        //         layers::Framer framer;
        //
        //         // TODO: plus denoisers... maybe in different struct?
        //
        //         CUdeviceptr dev_traced;
        //         std::vector <uint8_t> traced;
        //
        //         int32_t depth;
        // } amadeus_path_tracer;

        // Albedo (rasterization)
        struct Albedo {
                vk::raii::PipelineLayout pipeline_layout = nullptr;
                vk::raii::Pipeline pipeline = nullptr;
        
                vk::raii::DescriptorSetLayout dsl = nullptr;
                std::map <MeshIndex, int> dset_refs;
                std::vector <vk::raii::DescriptorSet> dsets;
        } albedo;

        // Rendering bounding boxes (rasterization)
        struct Boxes {
                vk::raii::PipelineLayout pipeline_layout = nullptr;
                vk::raii::Pipeline pipeline = nullptr;

                std::map <std::pair <int, int>, BoundingBox> cache;

                BufferData buffer = nullptr;
        } bounding_box;

        // Sobel filter (computer shader)
        struct SobelFilter {
                vk::raii::PipelineLayout pipeline_layout = nullptr;
                vk::raii::Pipeline pipeline = nullptr;
        
                vk::raii::DescriptorSetLayout dsl = nullptr;
                vk::raii::DescriptorSet dset = nullptr;
                
                ImageData output = nullptr;
                vk::raii::Sampler output_sampler = nullptr;
        } sobel;

        // Simple pipelines
        SimplePipeline normal;
        SimplePipeline uv;
        SimplePipeline triangulation;
        SimplePipeline highlight;

        // Current viewport extent
        vk::Extent2D extent;
       
        // Miscelaneous resources
        BufferData presentation_mesh_buffer = nullptr;
        BufferData index_staging_buffer = nullptr;
        std::vector <uint32_t> index_staging_data;

        // Rendering mode and parameters
        RenderState render_state;

        // TODO: table mapping render_state to function for presenting

        EditorViewport() = delete;
        EditorViewport(const Context &,
                const std::shared_ptr <amadeus::System> &,
                const std::shared_ptr <layers::MeshMemory> &);

        // Configuration methods
        void configure_present();
        void configure_gbuffer_pipeline();
        void configure_albedo_pipeline();
        void configure_normals_pipeline();
        void configure_uv_pipeline();
        void configure_triangulation_pipeline();
        void configure_bounding_box_pipeline();
        void configure_sobel_pipeline();
        void configure_highlight_pipeline();

        void configure_gbuffer_rtx();
        // void configure_amadeus_path_tracer(const Context &);
        void configure_path_tracer(const Context &);

        void resize(const vk::Extent2D &);

        // Rendering methods
        void render_gbuffer(const RenderInfo &, const std::vector <Entity> &);
        // void render_amadeus_path_traced(const RenderInfo &, const std::vector <Entity> &, daemons::Transform &);
        void render_path_traced(const RenderInfo &, const std::vector <Entity> &);
        void render_albedo(const RenderInfo &, const std::vector <Entity> &);
        void render_normals(const RenderInfo &);
        void render_uv(const RenderInfo &);
        void render_triangulation(const RenderInfo &);
        void render_bounding_box(const RenderInfo &, const std::vector <Entity> &);
        void render_highlight(const RenderInfo &, const std::vector <Entity> &);

        void render(const RenderInfo &, const std::vector <Entity> &, daemons::Transform &);

        // Other methods
        void prerender_raytrace(const std::vector <Entity> &);

        // Properties
        ImageData &viewport() {
                return framebuffer_images.viewport;
        }

        vk::raii::Image &viewport_image() {
                return framebuffer_images.viewport.image;
        }
        
        vk::raii::ImageView &viewport_image_view() {
                return framebuffer_images.viewport.view;
        }

        // Querying objects
        std::vector <std::pair <int, int>>
        selection_query(const std::vector <Entity> &, const glm::vec2 &);
};

// Functions
void show_menu(const std::shared_ptr <EditorViewport> &, const MenuOptions &);
