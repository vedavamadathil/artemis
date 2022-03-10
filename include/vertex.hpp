#ifndef VERTEX_H_
#define VERTEX_H_

// Standard headers
#include <vector>

// GLM headers
#include <glm/glm.hpp>

// Engine headers
#include "backend.hpp"

namespace kobra {

// Vertex types
using VertexType = uint32_t;

constexpr VertexType VERTEX_TYPE_POSITION	= 0;
constexpr VertexType VERTEX_TYPE_NORMAL		= 0x1;
constexpr VertexType VERTEX_TYPE_TEXCOORD	= 0x2;
constexpr VertexType VERTEX_TYPE_COLOR		= 0x4;
constexpr VertexType VERTEX_TYPE_TANGENT	= 0x8;

// Vertex information (templated by vertex type)
template <VertexType T = VERTEX_TYPE_POSITION>
struct Vertex {
	// Vertex type
	static constexpr VertexType type = T;

	// Data
	glm::vec3 pos;

	// Vertex constructor
	Vertex() {}

	// Vertex constructor
	Vertex(const glm::vec3 &pos) : pos {pos} {}

	// Vertex binding
	static VertexBinding vertex_binding() {
		return VertexBinding {
			.binding = 0,
			.stride = sizeof(Vertex),
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
		};
	}

	// Get vertex attribute descriptions
	static std::array <VertexAttribute, 1> vertex_attributes() {
		return std::array <VertexAttribute, 1> {
			VertexAttribute {
				.location = 0,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = offsetof(Vertex, pos)
			}
		};
	}
};

// Default vertex type
template <>
struct Vertex <VERTEX_TYPE_POSITION> {
	// Vertex type
	static constexpr VertexType type = VERTEX_TYPE_POSITION;

	// Data
	glm::vec3 pos;

	// Vertex constructor
	Vertex() {}

	// Vertex constructor
	Vertex(const glm::vec3 &pos) : pos {pos} {}

	// Vertex binding
	static VertexBinding vertex_binding() {
		return VertexBinding {
			.binding = 0,
			.stride = sizeof(Vertex),
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
		};
	}

	// Get vertex attribute descriptions
	static std::array <VertexAttribute, 1> vertex_attributes() {
		return std::array <VertexAttribute, 1> {
			VertexAttribute {
				.location = 0,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = offsetof(Vertex, pos)
			}
		};
	}
};

// Vertex with normal
template <>
struct Vertex <VERTEX_TYPE_NORMAL> {
	// Vertex type
	static constexpr VertexType type = VERTEX_TYPE_NORMAL;

	// Data
	glm::vec3 pos;
	glm::vec3 normal;

	// Vertex constructor
	Vertex() {}

	// Vertex constructor
	Vertex(const glm::vec3 &pos, const glm::vec3 &normal) : pos {pos}, normal {normal} {}

	// Vertex binding
	static VertexBinding vertex_binding() {
		return VertexBinding {
			.binding = 0,
			.stride = sizeof(Vertex),
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
		};
	}

	// Get vertex attribute descriptions
	static std::array <VertexAttribute, 2> vertex_attributes() {
		return std::array <VertexAttribute, 2> {
			VertexAttribute {
				.location = 0,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = offsetof(Vertex, pos)
			},
			VertexAttribute {
				.location = 1,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = offsetof(Vertex, normal)
			}
		};
	}
};

// Aliases
template <VertexType T>
using VertexList = std::vector <Vertex <T>>;
using IndexList = std::vector <uint32_t>;

}

#endif
