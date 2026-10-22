#include "graphics.hpp"

#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

#include "graphics_internal.hpp"

namespace graphics {

Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, const void* data) {
	auto& context = internal::context;

	const VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};

	// Просим у VMA память, в которую CPU может писать напрямую (HOST_VISIBLE),
	// причём без ручной синхронизации кэшей (HOST_COHERENT), и сразу отображённую (MAPPED).
	const VmaAllocationCreateInfo allocation_info = {
		.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
		         VMA_ALLOCATION_CREATE_MAPPED_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO,
		.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
		                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
	};

	Buffer result;
	VmaAllocationInfo info = {};

	if (vmaCreateBuffer(context.allocator, &buffer_info, &allocation_info,
	                    &result.buffer, &result.allocation, &info) != VK_SUCCESS) {
		std::cerr << "Failed to create buffer\n";
		return {};
	}

	result.mapped = info.pMappedData;

	if (data != nullptr) {
		std::memcpy(result.mapped, data, size);
	}

	return result;
}

void destroyBuffer(Buffer& buffer) {
	vmaDestroyBuffer(internal::context.allocator, buffer.buffer, buffer.allocation);
	buffer = {};
}

VkShaderModule loadShaderModule(const char* path) {
	// ios::ate — сразу встаём в конец файла, чтобы узнать его размер
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file) {
		std::cerr << "Failed to open shader file " << path << '\n';
		return VK_NULL_HANDLE;
	}

	const size_t size = size_t(file.tellg());
	std::vector<uint32_t> code((size + 3) / 4); // SPIR-V состоит из 32-битных слов
	file.seekg(0);
	file.read(reinterpret_cast<char*>(code.data()), std::streamsize(size));

	const VkShaderModuleCreateInfo module_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = size,
		.pCode = code.data(),
	};

	VkShaderModule module = VK_NULL_HANDLE;
	if (vkCreateShaderModule(internal::context.device, &module_info, nullptr, &module) != VK_SUCCESS) {
		std::cerr << "Failed to create shader module from " << path << '\n';
		return VK_NULL_HANDLE;
	}

	return module;
}

} // namespace graphics
