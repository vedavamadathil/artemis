#ifndef RASTER_H_
#define RASTER_H_

// Engine headers
#include "../buffer_manager.hpp"
#include "../vertex.hpp"

namespace kobra {

namespace raster {

// More aliases
template <VertexType T>
using VertexBuffer = BufferManager <Vertex <T>>;
using IndexBuffer = BufferManager <uint32_t>;

// Rasterization abstraction and primitives
struct RenderPacket {
};

}

}

#endif
