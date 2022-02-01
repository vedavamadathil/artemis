// Ray structure
struct Ray {
	vec3 origin;
	vec3 direction;
};

// Create a ray from the camera
Ray make_ray(vec2 uv, vec3 camera_position, float scale, float aspect)
{
	float cx = (2.0 * uv.x - 1.0) * scale * aspect;
	float cy = (1.0 - 2.0 * uv.y) * scale;

	vec3 right = vec3(1.0, 0.0, 0.0);
	vec3 up = vec3(0.0, 1.0, 0.0);
	vec3 forward = vec3(0.0, 0.0, 1.0);

	vec3 direction = cx * right + cy * up + forward;

	return Ray(camera_position, normalize(direction));
}
