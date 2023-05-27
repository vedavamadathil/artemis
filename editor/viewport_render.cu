#include "editor_viewport.cuh"
#include "include/cuda/cast.cuh"
#include "include/cuda/error.cuh"
#include "include/cuda/interop.cuh"
#include "include/daemons/material.hpp"
#include "push_constants.hpp"

static cuda::_material convert_material(const Material &material, TextureLoader &texture_loader, const vk::raii::Device &device)
{
        cuda::_material mat;

        // Scalar/vector values
        mat.diffuse = cuda::to_f3(material.diffuse);
        mat.specular = cuda::to_f3(material.specular);
        mat.emission = cuda::to_f3(material.emission);
        // mat.ambient = cuda::to_f3(material.ambient);
        // mat.shininess = material.shininess;
        mat.roughness = material.roughness;
        mat.refraction = material.refraction;
        mat.type = material.type;

        // Textures
        if (material.has_albedo()) {
                const ImageData &diffuse = texture_loader
                        .load_texture(material.diffuse_texture);

                mat.textures.diffuse
                        = cuda::import_vulkan_texture(device, diffuse);
                mat.textures.has_diffuse = true;
        }

        if (material.has_normal()) {
                const ImageData &normal = texture_loader
                        .load_texture(material.normal_texture);

                mat.textures.normal
                        = cuda::import_vulkan_texture(device, normal);
                mat.textures.has_normal = true;
        }

        if (material.has_specular()) {
                const ImageData &specular = texture_loader
                        .load_texture(material.specular_texture);

                mat.textures.specular
                        = cuda::import_vulkan_texture(device, specular);
                mat.textures.has_specular = true;
        }

        if (material.has_emission()) {
                const ImageData &emission = texture_loader
                        .load_texture(material.emission_texture);

                mat.textures.emission
                        = cuda::import_vulkan_texture(device, emission);
                mat.textures.has_emission = true;
        }

        if (material.has_roughness()) {
                const ImageData &roughness = texture_loader
                        .load_texture(material.roughness_texture);

                mat.textures.roughness
                        = cuda::import_vulkan_texture(device, roughness);
                mat.textures.has_roughness = true;
        }

        return mat;
}

// TODO: store texture loader and device...
void update_materials(CommonRaytracing *crtx, const MaterialDaemon *md, TextureLoader *texture_loader, const vk::raii::Device &device)
{
        // TODO: only update if necessary
        std::cout << "Updating materials" << std::endl;
        // if ()
        // TODO: only update the materials that have changed
        crtx->materials.clear();
        for (const auto &material : md->materials) {
                crtx->materials.push_back(
                        convert_material(material, *texture_loader, device)
                );
        }

        if (crtx->dev_materials)
                cuda::free(crtx->dev_materials);

        crtx->dev_materials = cuda::make_buffer_ptr(crtx->materials);
}

// Prerender process for raytracing
void EditorViewport::prerender_raytrace(const std::vector <Entity> &entities,
                const TransformDaemon *td,
                const MaterialDaemon *md)
{
        if (!common_rtx.clk_rise)
                return;

        // Load SBT information
        bool rebuild_tlas = false;
        bool rebuild_sbt = false;

        for (const auto &entity : entities) {
                int id = entity.id;
                const auto &renderable = entity.get <Renderable> ();
                const auto &transform = entity.get <Transform> ();

                if (td->changed(id))
                        rebuild_tlas |= true;

                // Generate cache data
                mesh_memory->cache_cuda(entity);

                // Create SBT record for each submesh
                const auto &meshes = renderable.mesh->submeshes;
                for (int i = 0; i < meshes.size(); i++) {
                        MeshIndex index { id, i };

                        if (common_rtx.record_refs.find(index) == common_rtx.record_refs.end()) {
                                int new_index = common_rtx.records.size();
                                common_rtx.record_refs[index] = new_index;
                                common_rtx.records.emplace_back();

                                optix::Record <Hit> record;
                                // optix::pack_header(gbuffer_rtx.closest_hit, record);

                                auto cachelet = mesh_memory->get(entity.id, i);

                                record.data.vertices = cachelet.m_cuda_vertices;
                                record.data.triangles = (uint3 *) cachelet.m_cuda_triangles;
                                record.data.model = transform.matrix();
                                record.data.index = meshes[i].material_index;

                                common_rtx.records[new_index] = record;

                                // If this is an area light, also add
                                // information...
                                // const Material &material = Material::all[meshes[i].material_index];
                                const Material &material = md->materials[meshes[i].material_index];
                                if (glm::length(material.emission) > 1e-3f) {
                                        // TODO: do we really need another
                                        // buffer for this?
                                        std::cout << "Adding area light: # of triangles = "
                                                << meshes[i].triangles() << ": "
                                                << meshes[i].vertices.size() << ", "
                                                << meshes[i].indices.size() << std::endl;
                                        
                                        for (int j = 0; j < meshes[i].triangles(); j++) {
                                                int i0 = meshes[i].indices[3 * j];
                                                int i1 = meshes[i].indices[3 * j + 1];
                                                int i2 = meshes[i].indices[3 * j + 2];
                                        }

                                        AreaLight light;
                                        light.model = transform.matrix();
                                        light.vertices = (Vertex *) cachelet.m_cuda_vertices;
                                        light.indices = (uint3 *) cachelet.m_cuda_triangles;
                                        light.triangles = meshes[i].triangles();
                                        light.emission = cuda::to_f3(material.emission);
                                        path_tracer.lights.push_back(light);
                                }

                                rebuild_tlas = true;
                                rebuild_sbt = true;
                        }

                        // TODO: checking for transformations...
                }
        }

        // Trigger update for System
        system->update(entities);

        if (rebuild_tlas) {
                OptixTraversableHandle handle = system->build_tlas(1, common_rtx.record_refs);
                gbuffer_rtx.launch_params.handle = handle;
                path_tracer.launch_params.handle = handle;
                sparse_gi.launch_params.handle = handle;
        }

        if (rebuild_sbt) {
                printf("Rebuilding SBT\n");

                // G-buffer hit SBT
                for (auto &record : common_rtx.records)
                        optix::pack_header(gbuffer_rtx.closest_hit, record);

                if (gbuffer_rtx.sbt.hitgroupRecordBase)
                        cuda::free(gbuffer_rtx.sbt.hitgroupRecordBase);
        
                gbuffer_rtx.sbt.hitgroupRecordBase = cuda::make_buffer_ptr(common_rtx.records);
                gbuffer_rtx.sbt.hitgroupRecordStrideInBytes = sizeof(optix::Record <Hit>);
                gbuffer_rtx.sbt.hitgroupRecordCount = common_rtx.records.size();

                // Sparse GI hit SBT
                for (auto &record : common_rtx.records)
                        optix::pack_header(sparse_gi.closest_hit, record);

                if (sparse_gi.sbt.hitgroupRecordBase)
                        cuda::free(sparse_gi.sbt.hitgroupRecordBase);

                sparse_gi.sbt.hitgroupRecordBase = cuda::make_buffer_ptr(common_rtx.records);
                sparse_gi.sbt.hitgroupRecordStrideInBytes = sizeof(optix::Record <Hit>);
                sparse_gi.sbt.hitgroupRecordCount = common_rtx.records.size();

                // Path tracer hit SBT
                for (auto &record : common_rtx.records)
                        optix::pack_header(path_tracer.closest_hit, record);

                if (path_tracer.sbt.hitgroupRecordBase)
                        cuda::free(path_tracer.sbt.hitgroupRecordBase);

                path_tracer.sbt.hitgroupRecordBase = cuda::make_buffer_ptr(common_rtx.records);
                path_tracer.sbt.hitgroupRecordStrideInBytes = sizeof(optix::Record <Hit>);
                path_tracer.sbt.hitgroupRecordCount = common_rtx.records.size();

                // Load all materials
                // common_rtx.materials.clear();
                // // for (const auto &material : Material::all) {
                // for (const auto &material : md->materials) {
                //         common_rtx.materials.push_back(
                //                 convert_material(material, *texture_loader, *device)
                //         );
                // }
                //
                // common_rtx.dev_materials = cuda::make_buffer_ptr(common_rtx.materials);

                // Update lights for the sparse GI algorithm
                sparse_gi.launch_params.area.lights = 0;
                sparse_gi.launch_params.area.count = path_tracer.lights.size();
                if (sparse_gi.launch_params.area.count > 0)
                        sparse_gi.launch_params.area.lights = cuda::make_buffer(path_tracer.lights);

                uint triangles = 0;
                for (const auto &light : path_tracer.lights)
                        triangles += light.triangles;

                sparse_gi.launch_params.area.triangle_count = triangles;
                
                // Update lights for the path tracer
                path_tracer.launch_params.area.lights = 0;
                path_tracer.launch_params.area.count = path_tracer.lights.size();
                if (path_tracer.lights.size() > 0)
                        path_tracer.launch_params.area.lights = cuda::make_buffer(path_tracer.lights);
               
                // TODO: do we really need this?
                // uint triangles = 0;
                // for (const auto &light : path_tracer.lights)
                //         triangles += light.triangles;
                path_tracer.launch_params.area.triangle_count = triangles;
        }

        // Update the material buffer
        update_materials(&common_rtx, md, texture_loader, *device);

        // Turn off signal
        common_rtx.clk_rise = false;
}

// TODO: pass transform daemon for the raytracing backend
void EditorViewport::render_gbuffer(const RenderInfo &render_info,
                                    const std::vector <Entity> &entities,
                                    const TransformDaemon *td,
                                    const MaterialDaemon *md)
{
        if (render_state.backend == RenderState::eRasterized) {
                // The given entities are assumed to have all the necessary
                // components (Transform and Renderable)

                for (const auto &entity : entities) {
                        int id = entity.id;
                        const auto &renderable = entity.get <Renderable> ();

                        // Allocat descriptor sets if needed
                        const auto &meshes = renderable.mesh->submeshes;

                        for (int i = 0; i < meshes.size(); i++) {
                                MeshIndex index { id, i };

                                if (gbuffer.dset_refs.find(index) == gbuffer.dset_refs.end()) {
                                        int new_index = gbuffer.dsets.size();
                                        gbuffer.dset_refs[index] = new_index;
                                        
                                        gbuffer.dsets.emplace_back(
                                                std::move(
                                                        vk::raii::DescriptorSets {
                                                                *device,
                                                                { **descriptor_pool, *gbuffer.dsl }
                                                        }.front()
                                                )
                                        );

                                        // Bind inforation to descriptor set
                                        // Material material = Material::all[meshes[i].material_index];
                                        assert(meshes[i].material_index >= 0);
                                        assert(meshes[i].material_index < md->materials.size());
                                        Material material = md->materials[meshes[i].material_index];
                
                                        std::string albedo = "blank";
                                        if (material.has_albedo())
                                                albedo = material.diffuse_texture;

                                        std::string normal = "blank";
                                        if (material.has_normal())
                                                normal = material.normal_texture;

                                        texture_loader->bind(gbuffer.dsets[new_index], albedo, 0);
                                        texture_loader->bind(gbuffer.dsets[new_index], normal, 1);
                                }
                        }
                }

                // Render to G-buffer
                RenderArea::full().apply(render_info.cmd, extent);

                std::vector <vk::ClearValue> clear_values {
                        vk::ClearColorValue { std::array <float, 4> { 0.0f, 0.0f, 0.0f, 0.0f } },
                        vk::ClearColorValue { std::array <float, 4> { 0.0f, 0.0f, 0.0f, 0.0f } },
                        vk::ClearColorValue { std::array <float, 4> { 0.0f, 0.0f, 0.0f, 0.0f } },
                        vk::ClearColorValue { std::array <int32_t, 4> { -1, -1, -1, -1 }},
                        vk::ClearDepthStencilValue { 1.0f, 0 }
                };

                render_info.cmd.beginRenderPass(
                        vk::RenderPassBeginInfo {
                                *gbuffer_render_pass,
                                *gbuffer_fb,
                                vk::Rect2D { vk::Offset2D {}, extent },
                                clear_values
                        },
                        vk::SubpassContents::eInline
                );

                // Start the pipeline
                render_info.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *gbuffer.pipeline);

                GBuffer_PushConstants push_constants;

                push_constants.view = render_info.camera.view_matrix(render_info.camera_transform);
                push_constants.projection = render_info.camera.perspective_matrix();

                for (auto &entity : entities) {
                        int id = entity.id;
                        const auto &transform = entity.get <Transform> ();
                        const auto &renderable = entity.get <Renderable> ();
                        const auto &meshes = renderable.mesh->submeshes;

                        for (int i = 0; i < meshes.size(); i++) {
                                MeshIndex index { id, i };

                                // Bind descriptor set
                                int dset_index = gbuffer.dset_refs[index];
                                render_info.cmd.bindDescriptorSets(
                                        vk::PipelineBindPoint::eGraphics,
                                        *gbuffer.pipeline_layout,
                                        0, { *gbuffer.dsets[dset_index] }, {}
                                );

                                // Bind push constants
                                push_constants.model = transform.matrix();

                                int material_index = meshes[i].material_index;
                                push_constants.material_index = material_index;

                                // Material material = Material::all[material_index];
                                Material material = md->materials[material_index];
                                
                                int texture_status = 0;
                                texture_status |= (material.has_albedo());
                                texture_status |= (material.has_normal() << 1);

                                push_constants.texture_status = texture_status;

                                render_info.cmd.pushConstants <GBuffer_PushConstants> (
                                        *gbuffer.pipeline_layout,
                                        vk::ShaderStageFlagBits::eVertex,
                                        0, push_constants
                                );

                                // Draw
                                render_info.cmd.bindVertexBuffers(0, { *renderable.vertex_buffer[i].buffer }, { 0 });
                                render_info.cmd.bindIndexBuffer(*renderable.index_buffer[i].buffer, 0, vk::IndexType::eUint32);
                                render_info.cmd.drawIndexed(renderable.index_count[i], 1, 0, 0, 0);
                        }
                }

                render_info.cmd.endRenderPass();
        } else if (render_state.backend == RenderState::eRaytraced) {
                // Prerender
                prerender_raytrace(entities, td, md);

                // Configure launch parameters
                auto uvw = uvw_frame(render_info.camera, render_info.camera_transform);

                gbuffer_rtx.launch_params.U = cuda::to_f3(uvw.u);
                gbuffer_rtx.launch_params.V = cuda::to_f3(uvw.v);
                gbuffer_rtx.launch_params.W = cuda::to_f3(uvw.w);
                
                gbuffer_rtx.launch_params.origin = cuda::to_f3(render_info.camera_transform.position);
                gbuffer_rtx.launch_params.resolution = { extent.width, extent.height };
                
                gbuffer_rtx.launch_params.position_surface = framebuffer_images.cu_position_surface;
                gbuffer_rtx.launch_params.normal_surface = framebuffer_images.cu_normal_surface;
                gbuffer_rtx.launch_params.uv_surface = framebuffer_images.cu_uv_surface;
                gbuffer_rtx.launch_params.index_surface = framebuffer_images.cu_material_index_surface;

                gbuffer_rtx.launch_params.materials = (cuda::_material *) common_rtx.dev_materials;

                // cuda::copy(gbuffer_rtx.dev_launch_params, &gbuffer_rtx.launch_params, 1);
                GBufferParameters *dev_params = (GBufferParameters *) gbuffer_rtx.dev_launch_params;
                CUDA_CHECK(cudaMemcpy(dev_params, &gbuffer_rtx.launch_params, sizeof(GBufferParameters), cudaMemcpyHostToDevice));
                
                OPTIX_CHECK(
                        optixLaunch(
                                gbuffer_rtx.pipeline, 0,
                                gbuffer_rtx.dev_launch_params,
                                sizeof(GBufferParameters),
                                &gbuffer_rtx.sbt,
                                extent.width, extent.height, 1
                        )
                );

                CUDA_SYNC_CHECK();

                std::string output = optix_io_read(&gbuffer_rtx.launch_params.io);
                optix_io_clear(&gbuffer_rtx.launch_params.io);
        }

        // Transition layout for position and normal map
        framebuffer_images.material_index.layout = vk::ImageLayout::eUndefined;
        framebuffer_images.material_index.transition_layout(render_info.cmd, vk::ImageLayout::eGeneral);

        sobel.output.transition_layout(render_info.cmd, vk::ImageLayout::eGeneral);

        // Now apply Sobel filterto get edges
        render_info.cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *sobel.pipeline);
        render_info.cmd.bindDescriptorSets(
                vk::PipelineBindPoint::eCompute,
                *sobel.pipeline_layout,
                0, { *sobel.dset }, {}
        );

        render_info.cmd.dispatch(
                (extent.width + 15) / 16,
                (extent.height + 15) / 16,
                1
        );

        // Transition framebuffer images to shader read for later stages
        // TODO: some way to avoid this hackery
        framebuffer_images.position.layout = vk::ImageLayout::eUndefined;
        framebuffer_images.normal.layout = vk::ImageLayout::eUndefined;
        framebuffer_images.uv.layout = vk::ImageLayout::eUndefined;
        framebuffer_images.material_index.layout = vk::ImageLayout::eGeneral;

        framebuffer_images.position.transition_layout(render_info.cmd, vk::ImageLayout::eShaderReadOnlyOptimal);
        framebuffer_images.normal.transition_layout(render_info.cmd, vk::ImageLayout::eShaderReadOnlyOptimal);
        framebuffer_images.uv.transition_layout(render_info.cmd, vk::ImageLayout::eShaderReadOnlyOptimal);
        framebuffer_images.material_index.transition_layout(render_info.cmd, vk::ImageLayout::eShaderReadOnlyOptimal);
        
        sobel.output.transition_layout(render_info.cmd, vk::ImageLayout::eShaderReadOnlyOptimal);
}

void EditorViewport::render_path_traced
                        (const RenderInfo &render_info,
                        const std::vector <Entity> &entities,
                        const MaterialDaemon *md)
{
        // Make sure the prerender step runs regardless of backend
        // prerender_raytrace(entities, md);

        // Configure launch parameters
        path_tracer.launch_params.color = common_rtx.dev_color;
        path_tracer.launch_params.time = common_rtx.timer.elapsed_start();
        path_tracer.launch_params.depth = path_tracer.depth;
        path_tracer.launch_params.samples += 1;
        if (render_info.camera_transform_dirty)
                path_tracer.launch_params.samples = 0;

        auto uvw = uvw_frame(render_info.camera, render_info.camera_transform);

        path_tracer.launch_params.U = cuda::to_f3(uvw.u);
        path_tracer.launch_params.V = cuda::to_f3(uvw.v);
        path_tracer.launch_params.W = cuda::to_f3(uvw.w);
        
        path_tracer.launch_params.origin = cuda::to_f3(render_info.camera_transform.position);
        path_tracer.launch_params.resolution = { extent.width, extent.height };
        
        path_tracer.launch_params.position_surface = framebuffer_images.cu_position_surface;
        path_tracer.launch_params.normal_surface = framebuffer_images.cu_normal_surface;
        path_tracer.launch_params.uv_surface = framebuffer_images.cu_uv_surface;
        path_tracer.launch_params.index_surface = framebuffer_images.cu_material_index_surface;

        path_tracer.launch_params.materials = (cuda::_material *) common_rtx.dev_materials;

        path_tracer.launch_params.sky.texture = environment_map.texture;
        path_tracer.launch_params.sky.enabled = environment_map.valid;
        
        PathTracerParameters *dev_params = (PathTracerParameters *) path_tracer.dev_launch_params;
        CUDA_CHECK(cudaMemcpy(dev_params, &path_tracer.launch_params, sizeof(PathTracerParameters), cudaMemcpyHostToDevice));
        
        OPTIX_CHECK(
                optixLaunch(
                        path_tracer.pipeline, 0,
                        path_tracer.dev_launch_params,
                        sizeof(PathTracerParameters),
                        &path_tracer.sbt,
                        extent.width, extent.height, 1
                )
        );
}

void EditorViewport::render_albedo(const RenderInfo &render_info,
                const std::vector <Entity> &entities,
                const MaterialDaemon *md)
{
        // The given entities are assumed to have all the necessary
        // components (Transform and Renderable)

        for (const auto &entity : entities) {
                int id = entity.id;
                const auto &renderable = entity.get <Renderable> ();

                // Allocat descriptor sets if needed
                const auto &meshes = renderable.mesh->submeshes;

                for (int i = 0; i < meshes.size(); i++) {
                        MeshIndex index { id, i };

                        if (albedo.dset_refs.find(index) == albedo.dset_refs.end()) {
                                int new_index = albedo.dsets.size();
                                albedo.dset_refs[index] = new_index;
                                
                                albedo.dsets.emplace_back(
                                        std::move(
                                                vk::raii::DescriptorSets {
                                                        *device,
                                                        { **descriptor_pool, *albedo.dsl }
                                                }.front()
                                        )
                                );

                                // Bind inforation to descriptor set
                                // Material material = Material::all[meshes[i].material_index];
                                Material material = md->materials[meshes[i].material_index];
        
                                std::string albedo_src = "blank";
                                if (material.has_albedo())
                                        albedo_src = material.diffuse_texture;

                                texture_loader->bind(albedo.dsets[new_index], albedo_src, 0);
                        }
                }
        }

        // Render to G-buffer
        // render_info.render_area.apply(render_info.cmd, render_info.extent);
        RenderArea::full().apply(render_info.cmd, render_info.extent);

        std::vector <vk::ClearValue> clear_values {
                vk::ClearColorValue { std::array <float, 4> { 0.0f, 0.0f, 0.0f, 1.0f } },
                vk::ClearDepthStencilValue { 1.0f, 0 }
        };

        render_info.cmd.beginRenderPass(
                vk::RenderPassBeginInfo {
                        *present_render_pass,
                        *viewport_fb,
                        vk::Rect2D { vk::Offset2D {}, render_info.extent },
                        clear_values
                },
                vk::SubpassContents::eInline
        );

        // Start the pipeline
        render_info.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *albedo.pipeline);

        Albedo_PushConstants push_constants;

        push_constants.view = render_info.camera.view_matrix(render_info.camera_transform);
        push_constants.projection = render_info.camera.perspective_matrix();

        for (auto &entity : entities) {
                int id = entity.id;
                const auto &transform = entity.get <Transform> ();
                const auto &renderable = entity.get <Renderable> ();
                const auto &meshes = renderable.mesh->submeshes;

                for (int i = 0; i < meshes.size(); i++) {
                        MeshIndex index { id, i };

                        // Bind descriptor set
                        int dset_index = albedo.dset_refs[index];
                        render_info.cmd.bindDescriptorSets(
                                vk::PipelineBindPoint::eGraphics,
                                *albedo.pipeline_layout,
                                0, { *albedo.dsets[dset_index] }, {}
                        );

                        // Bind push constants
                        push_constants.model = transform.matrix();

                        int material_index = meshes[i].material_index;
                        // Material material = Material::all[material_index];
                        Material material = md->materials[material_index];

                        push_constants.albedo = glm::vec4 { material.diffuse, 1.0f };
                        push_constants.has_albedo = material.has_albedo();

                        render_info.cmd.pushConstants <Albedo_PushConstants> (
                                *albedo.pipeline_layout,
                                vk::ShaderStageFlagBits::eVertex,
                                0, push_constants
                        );

                        // Draw
                        render_info.cmd.bindVertexBuffers(0, { *renderable.vertex_buffer[i].buffer }, { 0 });
                        render_info.cmd.bindIndexBuffer(*renderable.index_buffer[i].buffer, 0, vk::IndexType::eUint32);
                        render_info.cmd.drawIndexed(renderable.index_count[i], 1, 0, 0, 0);
                }
        }

        // render_info.cmd.endRenderPass();
}

void EditorViewport::render_normals(const RenderInfo &render_info)
{
        // TODO: pass different render options

        // Render to screen
        render_info.render_area.apply(render_info.cmd, render_info.extent);

        std::vector <vk::ClearValue> clear_values {
                vk::ClearColorValue { std::array <float, 4> { 0.0f, 0.0f, 0.0f, 1.0f } },
                vk::ClearDepthStencilValue { 1.0f, 0 }
        };

        // TODO: remove depht buffer from this...

        // Transition framebuffer images to shader read
        // TODO: some way to avoid this hackery
        // framebuffer_images.normal.layout = vk::ImageLayout::ePresentSrcKHR;
        // framebuffer_images.normal.transition_layout(render_info.cmd, vk::ImageLayout::eShaderReadOnlyOptimal);

        render_info.cmd.beginRenderPass(
                vk::RenderPassBeginInfo {
                        *present_render_pass,
                        *viewport_fb,
                        vk::Rect2D { vk::Offset2D {}, render_info.extent },
                        clear_values
                },
                vk::SubpassContents::eInline
        );

        // Start the pipeline
        render_info.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *normal.pipeline);

        // Bind descriptor set
        render_info.cmd.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                *normal.pipeline_layout,
                0, { *normal.dset }, {}
        );

        // Draw
        render_info.cmd.bindVertexBuffers(0, { *presentation_mesh_buffer.buffer }, { 0 });
        render_info.cmd.draw(6, 1, 0, 0);

        // render_info.cmd.endRenderPass();
}

void EditorViewport::render_uv(const RenderInfo &render_info)
{
        // Render to screen
        render_info.render_area.apply(render_info.cmd, render_info.extent);

        std::vector <vk::ClearValue> clear_values {
                vk::ClearColorValue { std::array <float, 4> { 0.0f, 0.0f, 0.0f, 1.0f } },
                vk::ClearDepthStencilValue { 1.0f, 0 }
        };

        render_info.cmd.beginRenderPass(
                vk::RenderPassBeginInfo {
                        *present_render_pass,
                        *viewport_fb,
                        vk::Rect2D { vk::Offset2D {}, render_info.extent },
                        clear_values
                },
                vk::SubpassContents::eInline
        );

        // Start the pipeline
        render_info.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *uv.pipeline);

        // Bind descriptor set
        render_info.cmd.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                *uv.pipeline_layout,
                0, { *uv.dset }, {}
        );

        // Draw
        render_info.cmd.bindVertexBuffers(0, { *presentation_mesh_buffer.buffer }, { 0 });
        render_info.cmd.draw(6, 1, 0, 0);
}

void EditorViewport::render_triangulation(const RenderInfo &render_info)
{
        // Render to screen
        render_info.render_area.apply(render_info.cmd, render_info.extent);

        // WARNING: there is an issue where the alpha channel is not being written to the framebuffer
        // the current workaround is to use a clear value of 1.0f for the alpha channel
        std::vector <vk::ClearValue> clear_values {
                vk::ClearColorValue { std::array <float, 4> { 0.0f, 0.0f, 0.0f, 1.0f } },
                vk::ClearDepthStencilValue { 1.0f, 0 }
        };

        // TODO: remove depht buffer from this...

        render_info.cmd.beginRenderPass(
                vk::RenderPassBeginInfo {
                        *present_render_pass,
                        *viewport_fb,
                        vk::Rect2D { vk::Offset2D {}, render_info.extent },
                        // {}
                        clear_values
                },
                vk::SubpassContents::eInline
        );

        // Start the pipeline
        render_info.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *triangulation.pipeline);

        // Bind descriptor set
        render_info.cmd.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                *triangulation.pipeline_layout,
                0, { *triangulation.dset }, {}
        );

        // Draw
        render_info.cmd.bindVertexBuffers(0, { *presentation_mesh_buffer.buffer }, { 0 });
        render_info.cmd.draw(6, 1, 0, 0);
}

void EditorViewport::render_bounding_box(const RenderInfo &render_info, const std::vector <Entity> &entities)
{
        // NOTE: Showing bounding boxes does not start a new render pass

        // Start the pipeline
        render_info.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *bounding_box.pipeline);

        // Push constants
        BoundingBox_PushConstants push_constants;
        
        push_constants.view = render_info.camera.view_matrix(render_info.camera_transform);
        push_constants.projection = render_info.camera.perspective_matrix();
        push_constants.color = glm::vec4 { 0.0f, 1.0f, 0.0f, 1.0f };

        // Draw
        // TODO: instancing...
        for (auto &entity : entities) {
                const auto &transform = entity.get <Transform> ();
                const auto &renderable = entity.get <Renderable> ();
                const auto &meshes = renderable.mesh->submeshes;

                for (int i = 0; i < meshes.size(); i++) {
                        // Bind push constants

                        // TODO: cache the bounding box...
                        BoundingBox bbox;

                        auto index = std::make_pair(entity.id, i);
                        if (bounding_box.cache.find(index) != bounding_box.cache.end()) {
                                bbox = bounding_box.cache[index];
                        } else {
                                bbox = meshes[i].bbox();
                                bounding_box.cache[index] = bbox;
                        }

                        bbox = bbox.transform(transform);

                        Transform mesh_transform;
                        mesh_transform.position = (bbox.min + bbox.max)/2.0f;
                        mesh_transform.scale = (bbox.max - bbox.min)/2.0f;

                        // TODO: bbox per entity or submesh?
                        push_constants.model = mesh_transform.matrix();

                        render_info.cmd.pushConstants <BoundingBox_PushConstants> (
                                *bounding_box.pipeline_layout,
                                vk::ShaderStageFlagBits::eVertex,
                                0, push_constants
                        );

                        // Draw
                        render_info.cmd.bindVertexBuffers(0, { *bounding_box.buffer.buffer }, { 0 });
                        render_info.cmd.draw(bounding_box.buffer.size/sizeof(Vertex), 1, 0, 0);
                }
        }
}

void EditorViewport::render_highlight(const RenderInfo &render_info, const std::vector <Entity> &entities)
{
        // If none are highlighted, return
        if (render_info.highlighted_entities.empty()) {
                render_info.cmd.endRenderPass();
                return;
        }

        // Push constants
        Highlight_PushConstants push_constants;

        push_constants.color = { 0.89, 0.73, 0.33, 0.5 };
        if (render_state.mode == RenderState::eTriangulation)
                push_constants.color = { 0.0, 0.0, 0.0, 0.3 };
        else if (render_state.mode == RenderState::eNormals)
                push_constants.color = { 0.0, 0.0, 0.0, 0.3 };

        push_constants.material_index = -1;
        push_constants.material_index = *render_info.highlighted_entities.begin();

        // Start the pipeline
        render_info.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *highlight.pipeline);
        
        // Bind descriptor set
        render_info.cmd.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                *highlight.pipeline_layout,
                0, { *highlight.dset }, {}
        );

        // Bind push constants
        render_info.cmd.pushConstants <Highlight_PushConstants> (
                *highlight.pipeline_layout,
                vk::ShaderStageFlagBits::eFragment,
                0, push_constants
        );

        // Draw
        render_info.cmd.bindVertexBuffers(0, { *presentation_mesh_buffer.buffer }, { 0 });
        render_info.cmd.draw(6, 1, 0, 0);

        render_info.cmd.endRenderPass();
}

void EditorViewport::render(const RenderInfo &render_info,
                const std::vector <Entity> &entities,
                TransformDaemon &td,
                const MaterialDaemon *md)
{
        // TODO: pass camera as an entity, and then check if its moved using the
                                // daemon...
        // Set state
        common_rtx.clk_rise = true;

        // TODO: if a click is detected, then we need to run the G-buffer pass
        // at least once to keep up to date with the latest changes
        // NOTE: this is done anyways...
        switch (render_state.mode) {
        case RenderState::eTriangulation:
                render_gbuffer(render_info, entities, &td, md);
                render_triangulation(render_info);
                break;

        case RenderState::eNormals:
                render_gbuffer(render_info, entities, &td, md);
                render_normals(render_info);
                break;
        
        case RenderState::eTextureCoordinates:
                render_gbuffer(render_info, entities, &td, md);
                render_uv(render_info);
                break;

        case RenderState::eAlbedo:
                render_gbuffer(render_info, entities, &td, md);
                render_albedo(render_info, entities, md);
                break;
        
        case RenderState::eSparseGlobalIllumination:
                prerender_raytrace(entities, &td, md);
                render_gbuffer(render_info, entities, &td, md);
                ::render(this, &sparse_gi, render_info, entities, md);
                blit_raytraced(this, render_info);
                break;
        
        case RenderState::ePathTraced:
                prerender_raytrace(entities, &td, md);
                render_gbuffer(render_info, entities, &td, md);
                render_path_traced(render_info, entities, md);
                blit_raytraced(this, render_info);
                break;
        default:
                printf("ERROR: EditorViewport::render called in invalid mode\n");
        }

        // If bounding boxes are enabled, render them
        if (render_state.bounding_boxes)
                render_bounding_box(render_info, entities);

        // Render the highlight
        render_highlight(render_info, entities);
}

std::vector <std::pair <int, int>>
EditorViewport::selection_query(const std::vector <Entity> &entities, const glm::vec2 &loc)
{
        // Copy the material index image
        vk::raii::Queue queue {*device, 0, 0};

        submit_now(*device, queue, *command_pool,
                [&](const vk::raii::CommandBuffer &cmd) {
                        transition_image_layout(cmd,
                                *framebuffer_images.material_index.image,
                                framebuffer_images.material_index.format,
                                vk::ImageLayout::eShaderReadOnlyOptimal,
                                vk::ImageLayout::eTransferSrcOptimal
                        );

                        copy_image_to_buffer(cmd,
                                framebuffer_images.material_index.image,
                                index_staging_buffer.buffer,
                                framebuffer_images.material_index.format,
                                extent.width, extent.height
                        );

                        transition_image_layout(cmd,
                                *framebuffer_images.material_index.image,
                                framebuffer_images.material_index.format,
                                vk::ImageLayout::eTransferSrcOptimal,
                                vk::ImageLayout::eShaderReadOnlyOptimal
                        );
                }
        );

        // Download to host
        index_staging_data.resize(index_staging_buffer.size/sizeof(uint32_t));
        index_staging_buffer.download(index_staging_data);

        // Find the ID of the entity at the location
        glm::ivec2 iloc = loc * glm::vec2(extent.width, extent.height);
        uint32_t id = index_staging_data[iloc.y * extent.width + iloc.x];
        uint32_t material_id = id & 0xFFFF; // Lower 16 bits

        // Find the index of the entity in the selection
        std::vector <std::pair <int, int>> entity_indices;
        for (auto &entity : entities) {
                const Renderable &renderable = entity.get <Renderable> ();

                auto &meshes = renderable.mesh->submeshes;
                for (int j = 0; j < meshes.size(); j++) {
                        if (meshes[j].material_index == material_id) {
                                entity_indices.push_back({ entity.id, j });
                                break;
                        }
                }
        }

        return entity_indices;
}

static void show_mode_menu(RenderState *render_state)
{
        if (ImGui::BeginMenu("Mode")) {
                if (ImGui::MenuItem("Triangulation"))
                        render_state->mode = RenderState::eTriangulation;

                // if (ImGui::MenuItem("Wireframe"))
                //         render_state->mode = RenderState::eWireframe;
                
                if (ImGui::MenuItem("Normals"))
                        render_state->mode = RenderState::eNormals;
                
                if (ImGui::MenuItem("Texture Coordinates"))
                        render_state->mode = RenderState::eTextureCoordinates;

                if (ImGui::MenuItem("Albedo"))
                        render_state->mode = RenderState::eAlbedo;
                
                if (ImGui::MenuItem("Sparse Global Illumination")) {
                        render_state->mode = RenderState::eSparseGlobalIllumination;
                        render_state->sparse_gi_reset = true;
                }

                if (ImGui::MenuItem("Path Traced (G-buffer)"))
                        render_state->mode = RenderState::ePathTraced;

                if (ImGui::Checkbox("Show bounding boxes", &render_state->bounding_boxes));

                ImGui::EndMenu();
        }
}

struct _submenu_args {
        EditorViewport::PathTracer *path_tracer;
        SparseGI *sparse_gi;
};

static void show_mode_submenu(RenderState *render_state, const _submenu_args &args)
{
        static const std::map <decltype(RenderState::mode), const char *> modes = {
                { RenderState::eTriangulation, "Triangulation" },
                { RenderState::eNormals, "Normals" },
                { RenderState::eTextureCoordinates, "Texture Coordinates" },
                { RenderState::eAlbedo, "Albedo" },
                { RenderState::eSparseGlobalIllumination, "Sparse Global Illumination" },
                { RenderState::ePathTraced, "Path Traced (G-buffer)" },
        };
		
        // Mode-specific settings
        std::string mode_string = "?";
        if (modes.count(render_state->mode))
                mode_string = modes.at(render_state->mode);

        if (ImGui::BeginMenu(mode_string.c_str())) {
                if (render_state->mode == RenderState::ePathTraced)
                        if (ImGui::SliderInt("Depth", &args.path_tracer->depth, 1, 10));
                
                if (render_state->mode == RenderState::eSparseGlobalIllumination)
                        if (ImGui::SliderInt("Depth", &args.sparse_gi->depth, 1, 10));

                ImGui::EndMenu();
        }
}

static void show_backend_menu(RenderState *render_state)
{
        if (ImGui::BeginMenu("Backend")) {
                bool rasterizer = render_state->backend == RenderState::eRasterized;
                bool raytraced = render_state->backend == RenderState::eRaytraced;

                if (ImGui::Checkbox("Rasterizer", &rasterizer))
                        render_state->backend = RenderState::eRasterized;

                if (ImGui::Checkbox("Raytraced", &raytraced))
                        render_state->backend = RenderState::eRaytraced;

                ImGui::EndMenu();
        }
}

void show_menu(const std::shared_ptr <EditorViewport> &ev, const MenuOptions &options)
{
        if (ImGui::BeginMenuBar()) {
                show_mode_menu(&ev->render_state);
                show_mode_submenu(&ev->render_state, { &ev->path_tracer, &ev->sparse_gi });

                // Camera settings
                if (ImGui::BeginMenu("Camera")) {
                        if (ImGui::SliderFloat("Speed", options.speed, 0.1f, 100.0f));
                        if (ImGui::SliderFloat("FOV", &options.camera->fov, 1.0f, 179.0f));

                        ImGui::EndMenu();
                }

                show_backend_menu(&ev->render_state);

                // TODO: overlay # of samples...
                ImGui::EndMenuBar();
        }
}
        
void blit_raytraced(EditorViewport *ev, const RenderInfo &render_info)
{
        // Blitting
        float4 *buffer = ev->common_rtx.dev_color;

        kobra::cuda::hdr_to_ldr(
                buffer,
                (uint32_t *) ev->common_rtx.dev_traced,
                ev->extent.width, ev->extent.height,
                kobra::cuda::eTonemappingACES
        );

        kobra::cuda::copy(
                ev->common_rtx.traced, ev->common_rtx.dev_traced,
                ev->extent.width * ev->extent.height * sizeof(uint32_t)
        );

        ev->common_rtx.framer.pre_render(
                render_info.cmd,
                kobra::RawImage {
                        .data = ev->common_rtx.traced,
                        .width = ev->extent.width,
                        .height = ev->extent.height,
                        .channels = 4
                }
        );
        
        // Start render pass and blit
        // TODO: function to start render pass for presentation
        render_info.render_area.apply(render_info.cmd, render_info.extent);

        std::vector <vk::ClearValue> clear_values {
                vk::ClearColorValue { std::array <float, 4> { 0.0f, 0.0f, 0.0f, 1.0f } },
                vk::ClearDepthStencilValue { 1.0f, 0 }
        };

        render_info.cmd.beginRenderPass(
                vk::RenderPassBeginInfo {
                        *ev->present_render_pass,
                        *ev->viewport_fb,
                        vk::Rect2D { vk::Offset2D {}, ev->extent },
                        clear_values
                },
                vk::SubpassContents::eInline
        );

        // TODO: import CUDA to Vulkan and render straight to the image
        ev->common_rtx.framer.render(render_info.cmd);
}

// Rendering Sparse GI
void render(EditorViewport *ev, SparseGI *sparse_gi,
                const RenderInfo &render_info,
                const std::vector <Entity> &entities,
                const MaterialDaemon *md)
{
        const Camera &camera = render_info.camera;
        const Transform &camera_transform = render_info.camera_transform;

        // Configure launch parameters
        sparse_gi->launch_params.color = ev->common_rtx.dev_color;
        sparse_gi->launch_params.time = ev->common_rtx.timer.elapsed_start();
        sparse_gi->launch_params.dirty = render_info.camera_transform_dirty;
        sparse_gi->launch_params.reset = ev->render_state.sparse_gi_reset;

        ev->render_state.sparse_gi_reset = false;
        if (ev->render_state.sparse_gi_reset) {
                sparse_gi->launch_params.previous_view = camera.view_matrix(camera_transform);
                sparse_gi->launch_params.previous_projection = camera.perspective_matrix();
        }

        auto uvw = uvw_frame(camera, camera_transform);

        sparse_gi->launch_params.U = cuda::to_f3(uvw.u);
        sparse_gi->launch_params.V = cuda::to_f3(uvw.v);
        sparse_gi->launch_params.W = cuda::to_f3(uvw.w);
        
        sparse_gi->launch_params.origin = cuda::to_f3(render_info.camera_transform.position);
        sparse_gi->launch_params.resolution = { ev->extent.width, ev->extent.height };
        
        sparse_gi->launch_params.position_surface = ev->framebuffer_images.cu_position_surface;
        sparse_gi->launch_params.normal_surface = ev->framebuffer_images.cu_normal_surface;
        sparse_gi->launch_params.uv_surface = ev->framebuffer_images.cu_uv_surface;
        sparse_gi->launch_params.index_surface = ev->framebuffer_images.cu_material_index_surface;

        sparse_gi->launch_params.materials = (cuda::_material *) ev->common_rtx.dev_materials;

        sparse_gi->launch_params.sky.texture = ev->environment_map.texture;
        sparse_gi->launch_params.sky.enabled = ev->environment_map.valid;
        
        SparseGIParameters *dev_params = (SparseGIParameters *) sparse_gi->dev_launch_params;
        CUDA_CHECK(cudaMemcpy(dev_params, &sparse_gi->launch_params, sizeof(SparseGIParameters), cudaMemcpyHostToDevice));
        
        OPTIX_CHECK(
                optixLaunch(
                        sparse_gi->pipeline, 0,
                        sparse_gi->dev_launch_params,
                        sizeof(SparseGIParameters),
                        &sparse_gi->sbt,
                        ev->extent.width, ev->extent.height, 1
                )
        );
        
        // Update previous view and projection matrices
        sparse_gi->launch_params.previous_view = camera.view_matrix(camera_transform);
        sparse_gi->launch_params.previous_projection = camera.perspective_matrix();
}
