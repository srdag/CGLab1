#pragma once

#include <vulkan/vulkan_core.h>

#include <vk_mem_alloc.h>

namespace graphics {

// Буфер в памяти, которую видят и CPU, и GPU.
// mapped — адрес, по которому CPU может писать данные в этот буфер.
struct Buffer {
	VkBuffer buffer = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	void* mapped = nullptr;
};

// Создаёт буфер размером size байт. Если data != nullptr, сразу копирует туда данные.
Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, const void* data = nullptr);
void destroyBuffer(Buffer& buffer);

// Читает скомпилированный шейдер (.spv) с диска и создаёт из него VkShaderModule.
VkShaderModule loadShaderModule(const char* path);

} // namespace graphics
