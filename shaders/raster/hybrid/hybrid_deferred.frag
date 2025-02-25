#version 450

// Shader modules
#include "../material.glsl"
#include "../bindings.h"

// Inputs
layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec2 in_uv;
layout (location = 3) in mat3 in_tbn;
layout (location = 6) flat in int in_id;

// Uniform buffer for material
layout (binding = RASTER_BINDING_UBO) uniform MaterialBlock
{
	Material mat;
};

// Sampler inputs
layout (binding = RASTER_BINDING_ALBEDO_MAP)
uniform sampler2D albedo_map;

layout (binding = RASTER_BINDING_NORMAL_MAP)
uniform sampler2D normal_map;

// Outputs
layout (location = 0) out vec4 g_position;
layout (location = 1) out vec4 g_normal;
layout (location = 2) out vec4 g_albedo;
layout (location = 3) out vec4 g_specular;
layout (location = 4) out vec4 g_extra;
layout (location = 5) out int g_id;

// Color wheel
#define COLOR_WHEEL_SIZE 8

vec3 color_wheel[COLOR_WHEEL_SIZE] = vec3[]
(
	vec3(1.0, 0.0, 0.0),
	vec3(1.0, 1.0, 0.0),
	vec3(0.0, 1.0, 0.0),
	vec3(0.0, 1.0, 1.0),
	vec3(0.0, 0.0, 1.0),
	vec3(1.0, 0.0, 1.0),
	vec3(1.0, 0.0, 0.0),
	vec3(1.0, 1.0, 0.0)
);

void main()
{
	g_position = vec4(in_position, 1.0);

	// Albedo/diffuse
	if (mat.has_albedo > 0.5)
		g_albedo = texture(albedo_map, in_uv);
	else
		g_albedo = vec4(mat.diffuse, 1.0);

	// g_albedo.rgb = color_wheel[in_id % COLOR_WHEEL_SIZE];

	// Normal (TODO: use int instead of float for has_normal)
	if (mat.has_normal > 0.5) {
		g_normal.xyz = 2 * texture(normal_map, in_uv).rgb - 1;
		g_normal.xyz = normalize(in_tbn * g_normal.xyz);
	} else {
		g_normal = vec4(in_normal, 1.0);
	}

	g_specular = vec4(mat.specular, 1.0);
	g_extra = vec4(mat.shininess, mat.roughness, 0.0, 1.0); // TODO:
								// refraction as
								// well...
	
	g_id = in_id + 1;
}
