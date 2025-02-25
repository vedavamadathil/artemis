#ifndef SHADER_PROGRAM_H_
#define SHADER_PROGRAM_H_

// Standard headers
#include <filesystem>
#include <map>
#include <thread>

// Glslang and SPIRV-Tools
#include <glslang/SPIRV/GlslangToSpv.h>
#include <glslang/Public/ResourceLimits.h>

// Engine headers
#include "backend.hpp"
#include "common.hpp"

namespace kobra {

// Custom shader programs
class ShaderProgram {
private:
	bool m_failed = false;
	std::string m_source;
	vk::ShaderStageFlagBits	m_shader_type;
public:
        using Defines = std::map <std::string, std::string>;
        using Includes = std::set <std::string>;

	// Default constructor
	ShaderProgram() = default;

	// Constructor
	ShaderProgram(const std::string &, const vk::ShaderStageFlagBits &);

	// Compile shader
        // TODO: alias for the definitions map
        std::optional <vk::ShaderModule>
        compile(const vk::Device &, const Defines & = {}, const Includes & = {});
	
        std::optional <vk::raii::ShaderModule>
	compile(const vk::raii::Device &, const Defines & = {}, const Includes & = {});
};

}

#endif
