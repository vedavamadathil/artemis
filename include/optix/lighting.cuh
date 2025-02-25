#ifndef KOBRA_OPTIX_LIGHTING_H_
#define KOBRA_OPTIX_LIGHTING_H_

// OptiX headers
#include <optix.h>

// Engine headers
#include "../cuda/material.cuh"
#include "../cuda/brdf.cuh"

// TODO: kobra namespace

// Forward declarations
KCUDA_INLINE KCUDA_DEVICE
bool is_occluded(OptixTraversableHandle, float3, float3, float);

namespace kobra {

namespace optix {

// Power heurestic
static const float p = 2.0f;

KCUDA_INLINE KCUDA_HOST_DEVICE
float power(float pdf_f, float pdf_g)
{
	float f = pow(pdf_f, p);
	float g = pow(pdf_g, p);

	return f/(f + g);
}

KCUDA_INLINE KCUDA_HOST_DEVICE
float intersects_triangle
		(float3 v1, float3 v2, float3 v3,
		float3 origin, float3 dir)
{
	float3 e1 = v2 - v1;
	float3 e2 = v3 - v1;
	float3 s1 = cross(dir, e2);
	float divisor = dot(s1, e1);
	if (divisor == 0.0)
		return -1;
	float3 s = origin - v1;
	float inv_divisor = 1.0 / divisor;
	float b1 = dot(s, s1) * inv_divisor;
	if (b1 < 0.0 || b1 > 1.0)
		return -1;
	float3 s2 = cross(s, e1);
	float b2 = dot(dir, s2) * inv_divisor;
	if (b2 < 0.0 || b1 + b2 > 1.0)
		return -1;
	float t = dot(e2, s2) * inv_divisor;
	return t;
}

// Light type
struct QuadLight {
	float3 a;
	float3 ab;
	float3 ac;
	float3 intensity;

	KCUDA_INLINE KCUDA_HOST_DEVICE
	float area() const {
		return length(cross(ab, ac));
	}

	KCUDA_INLINE KCUDA_HOST_DEVICE
	float3 normal() const {
		return normalize(cross(ab, ac));
	}

	KCUDA_INLINE KCUDA_HOST_DEVICE
	float intersects(float3 origin, float3 dir) const {
		float3 v1 = a;
		float3 v2 = a + ab;
		float3 v3 = a + ac;
		float3 v4 = a + ab + ac;

		float t1 = intersects_triangle(v1, v2, v3, origin, dir);
		float t2 = intersects_triangle(v2, v3, v4, origin, dir);

		if (t1 < 0.0 && t2 < 0.0)
			return -1.0;
		if (t1 < 0.0)
			return t2;
		if (t2 < 0.0)
			return t1;

		return (t1 < t2) ? t1 : t2;
	}
};

// Triangular area light
struct TriangleLight {
	float3 a;
	float3 ab;
	float3 ac;
	float3 intensity;

	KCUDA_INLINE KCUDA_HOST_DEVICE
	float area() const {
		return length(cross(ab, ac)) * 0.5;
	}

	KCUDA_INLINE KCUDA_HOST_DEVICE
	float3 normal() const {
		return normalize(cross(ab, ac));
	}

	KCUDA_INLINE KCUDA_HOST_DEVICE
	float intersects(float3 origin, float3 dir) const {
		float3 v1 = a;
		float3 v2 = a + ab;
		float3 v3 = a + ac;

		return intersects_triangle(v1, v2, v3, origin, dir);
	}
};

// Sampling methods
// TODO: move else where
KCUDA_INLINE KCUDA_HOST_DEVICE
float3 sample_area_light(QuadLight light, cuda::Seed seed)
{
	float3 rand = cuda::pcg3f(seed);
	float u = fract(rand.x);
	float v = fract(rand.y);
	return light.a + u * light.ab + v * light.ac;
}

KCUDA_INLINE KCUDA_HOST_DEVICE
float3 sample_area_light(TriangleLight light, cuda::Seed seed)
{
	float3 rand = cuda::pcg3f(seed);
	float u = fract(rand.x);
	float v = fract(rand.y);

	if (u + v > 1.0f) {
		u = 1.0f - u;
		v = 1.0f - v;
	}

	return light.a + u * light.ab + v * light.ac;
}

// Direct lighting for Next Event Estimation
template <class Light>
KCUDA_DEVICE
float3 Ld_light(OptixTraversableHandle handle,
		const Light &light, const cuda::SurfaceHit &sh,
		float &light_pdf, cuda::Seed seed)
{
	// PDF for sampling point on diffuse light
	light_pdf = 1.0f/light.area();

	float3 lpos = sample_area_light(light, seed);

	float3 wi = normalize(lpos - sh.x);
	float R = length(lpos - sh.x);

	bool occluded = is_occluded(handle, sh.x, wi, R);
	if (occluded)
		return make_float3(0.0f);

	// TODO: brdf should evaluate all lobes...
	float3 f = cuda::brdf(sh, wi, eDiffuse);

	float ldot = abs(dot(light.normal(), wi));
	float geometric = ldot * abs(dot(sh.n, wi))/(R * R);

	return f * light.intensity * geometric;
}

}

}

#endif
