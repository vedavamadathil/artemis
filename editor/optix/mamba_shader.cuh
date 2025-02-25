#pragma once

// Standard headers
#include <random>

// OptiX headers
#include <optix.h>

// Engine headers
#include "common.cuh"
#include "include/cuda/material.cuh"
#include "include/cuda/random.cuh"
#include "include/vertex.hpp"

// Forward declarations
struct IrradianceProbeTable;

// Sampled lighting information
struct LightInfo {
        float3 position;
        float3 normal;
        float3 emission;
        float area;
        bool sky;
};

// Launch info for the G-buffer raytracer
struct MambaLaunchInfo {
        // Acceleration structure
        OptixTraversableHandle handle;

        // Global parameters
        bool dirty;
        bool reset;
        float time;
        int samples;
        uint counter;

        // Previous camera matrices
        glm::mat4 previous_view;
        glm::mat4 previous_projection;
        float3 previous_origin;

        // Camera parameters
        CameraAxis camera;

        // G-buffer information
        cudaSurfaceObject_t position;
        cudaSurfaceObject_t normal;
        cudaSurfaceObject_t uv;
        cudaSurfaceObject_t index;

        // List of all materials
        kobra::cuda::_material *materials;

        // Lighting information
        struct {
                AreaLight *lights;
                uint count;
                uint triangle_count;
        } area;

        Sky sky;

        // Direct lighting (ReSTIR)
        struct {
                Reservoir <LightInfo> *reservoirs;
                Reservoir <LightInfo> *previous;
                float3 *Le;
        } direct;

        // Indirect lighting caches
        struct {
                uint *block_offsets;
                float3 *Le;
                float3 *wo;

                float *sobel;

		IrradianceProbeTable *probes;

		float *raster;
		float3 *raster_indicator;
		float3 *raster_radiance;
		int32_t *probe_ids;

		int4 *group_probes;
		int32_t *group_probe_sizes;
        } indirect;

	// Information for secondary/ternary rays
	struct {
		cuda::SurfaceHit *hits;
		float3 *wi;
		float3 *Le;

		float3 *Ld_wi;
		float3 *Ld;
		float *Ld_depth;

		cuda::SurfaceHit *caustic_hits;
		float3 *caustic_wi;

		uint2 resolution;
	} secondary;

	// Raytracing results
	float3 *direct_wi;
	float3 *secondary_wi;  // 1/4 resolution
	float3 *ternary_wi;    // 1/16 resolution

	float3 *direct_Le;
	float3 *secondary_Le;  // 1/4 resolution
	float3 *ternary_Le;    // 1/16 resolution

	cuda::SurfaceHit *direct_hit;
	cuda::SurfaceHit *secondary_hit;  // 1/4 resolution
	cuda::SurfaceHit *ternary_hit;    // 1/16 resolution

	uint2 tracing_resolution;

        // Extra options
        struct {
                bool temporal;
                bool spatial;
        } options;

        // IO interface
        OptixIO io;
};
