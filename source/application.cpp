#include "application.hpp"
#include "graphics.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

#include <imgui.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace application {

namespace {

struct Vertex {
	glm::vec3 position;
	glm::vec3 color;
};

// Данные для шейдера, порядок полей как в basic.vert
struct Uniforms {
	glm::mat4 model;
	glm::mat4 view;
	glm::mat4 projection;
	glm::vec4 color;
};

struct Object {
	glm::vec3 position = {0.0f, 0.0f, 0.0f};
	glm::vec3 rotation = {0.0f, 0.0f, 0.0f}; // в градусах
	glm::vec3 scale = {1.0f, 1.0f, 1.0f};
	glm::vec3 color = {1.0f, 1.0f, 1.0f};

	// У каждого объекта свой uniform-буфер и свой набор дескрипторов
	graphics::Buffer uniform_buffer;
	VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
	Uniforms uniforms;
};

// Вершины правильного октаэдра лежат на осях координат
const glm::vec3 positions[6] = {
	{ 1.0f,  0.0f,  0.0f}, // 0: +X
	{-1.0f,  0.0f,  0.0f}, // 1: -X
	{ 0.0f,  1.0f,  0.0f}, // 2: +Y
	{ 0.0f, -1.0f,  0.0f}, // 3: -Y
	{ 0.0f,  0.0f,  1.0f}, // 4: +Z
	{ 0.0f,  0.0f, -1.0f}, // 5: -Z
};

// 8 граней по 3 вершины. Вершины идут против часовой стрелки, если смотреть на грань снаружи
const uint16_t indices[24] = {
	2, 4, 0,  2, 0, 5,  2, 5, 1,  2, 1, 4, // верхняя половина
	3, 0, 4,  3, 5, 0,  3, 1, 5,  3, 4, 1, // нижняя половина
};

const int object_count = 3;

graphics::Buffer vertex_buffer;
graphics::Buffer index_buffer;

VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
VkPipeline pipeline = VK_NULL_HANDLE;

Object objects[object_count];

// Проекция
bool perspective = true;
float fov = 60.0f;
float ortho_size = 4.0f;

// Анимация
bool playing = true;
float speed = 1.0f;
float radius = 1.0f;
float anim_time = 0.0f;

bool createPipeline() {
	auto& context = graphics::internal::context;

	VkShaderModule vertex_shader = graphics::loadShaderModule(SHADER_DIR "basic.vert.spv");
	VkShaderModule fragment_shader = graphics::loadShaderModule(SHADER_DIR "basic.frag.spv");
	if (vertex_shader == VK_NULL_HANDLE || fragment_shader == VK_NULL_HANDLE) {
		return false;
	}

	const VkPipelineShaderStageCreateInfo stages[] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vertex_shader,
			.pName = "main",
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = fragment_shader,
			.pName = "main",
		},
	};

	// Одна вершина = sizeof(Vertex) байт: позиция (location 0) и цвет (location 1)
	const VkVertexInputBindingDescription vertex_binding = {
		.binding = 0,
		.stride = sizeof(Vertex),
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};

	const VkVertexInputAttributeDescription vertex_attributes[] = {
		{
			.location = 0,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(Vertex, position),
		},
		{
			.location = 1,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(Vertex, color),
		},
	};

	const VkPipelineVertexInputStateCreateInfo vertex_input = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &vertex_binding,
		.vertexAttributeDescriptionCount = 2,
		.pVertexAttributeDescriptions = vertex_attributes,
	};

	const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	};

	// Размеры viewport и scissor задаются при рисовании, потому что окно может менять размер
	const VkPipelineViewportStateCreateInfo viewport_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	// Задние грани не рисуем. Передняя грань — вершины на экране идут против часовой стрелки
	const VkPipelineRasterizationStateCreateInfo rasterization = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.0f,
	};

	const VkPipelineMultisampleStateCreateInfo multisample = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};

	// Тест глубины: пиксель рисуется, только если он ближе уже нарисованного
	const VkPipelineDepthStencilStateCreateInfo depth_stencil = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
	};

	const VkPipelineColorBlendAttachmentState blend_attachment = {
		.blendEnable = VK_FALSE,
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};

	const VkPipelineColorBlendStateCreateInfo color_blend = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &blend_attachment,
	};

	const VkDynamicState dynamic_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};

	const VkPipelineDynamicStateCreateInfo dynamic_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = 2,
		.pDynamicStates = dynamic_states,
	};

	const VkGraphicsPipelineCreateInfo pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = 2,
		.pStages = stages,
		.pVertexInputState = &vertex_input,
		.pInputAssemblyState = &input_assembly,
		.pViewportState = &viewport_state,
		.pRasterizationState = &rasterization,
		.pMultisampleState = &multisample,
		.pDepthStencilState = &depth_stencil,
		.pColorBlendState = &color_blend,
		.pDynamicState = &dynamic_state,
		.layout = pipeline_layout,
		.renderPass = context.render_pass,
		.subpass = 0,
	};

	VkResult result = vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1,
	                                            &pipeline_info, nullptr, &pipeline);

	// Шейдерные модули нужны только на время создания конвейера
	vkDestroyShaderModule(context.device, vertex_shader, nullptr);
	vkDestroyShaderModule(context.device, fragment_shader, nullptr);

	return result == VK_SUCCESS;
}

} // namespace

bool initialize() {
	auto& context = graphics::internal::context;

	// Вершины октаэдра. Цвет вершины считаем из её позиции: координаты [-1; 1] -> цвет [0; 1]
	Vertex vertices[6];
	for (int i = 0; i < 6; ++i) {
		vertices[i].position = positions[i];
		vertices[i].color = positions[i] * 0.5f + 0.5f;
	}

	vertex_buffer = graphics::createBuffer(sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertices);
	index_buffer = graphics::createBuffer(sizeof(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices);

	// Описание набора дескрипторов: в привязке 0 — uniform-буфер для вершинного шейдера
	const VkDescriptorSetLayoutBinding binding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
	};

	const VkDescriptorSetLayoutCreateInfo set_layout_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &binding,
	};

	if (vkCreateDescriptorSetLayout(context.device, &set_layout_info, nullptr,
	                                &descriptor_set_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create descriptor set layout\n";
		return false;
	}

	const VkPipelineLayoutCreateInfo layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &descriptor_set_layout,
	};

	if (vkCreatePipelineLayout(context.device, &layout_info, nullptr, &pipeline_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create pipeline layout\n";
		return false;
	}

	if (!createPipeline()) {
		std::cerr << "Failed to create pipeline\n";
		return false;
	}

	// Пул, из которого берутся наборы дескрипторов: по одному на объект
	const VkDescriptorPoolSize pool_size = {
		.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = object_count,
	};

	const VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = object_count,
		.poolSizeCount = 1,
		.pPoolSizes = &pool_size,
	};

	if (vkCreateDescriptorPool(context.device, &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create descriptor pool\n";
		return false;
	}

	for (int i = 0; i < object_count; ++i) {
		Object& obj = objects[i];
		obj.position = glm::vec3(3.0f * float(i - 1), 0.0f, 0.0f); // объекты в ряд: x = -3, 0, 3

		obj.uniform_buffer = graphics::createBuffer(sizeof(Uniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

		const VkDescriptorSetAllocateInfo allocate_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &descriptor_set_layout,
		};

		if (vkAllocateDescriptorSets(context.device, &allocate_info, &obj.descriptor_set) != VK_SUCCESS) {
			std::cerr << "Failed to allocate descriptor set\n";
			return false;
		}

		// Записываем в набор, на какой буфер он указывает
		const VkDescriptorBufferInfo buffer_info = {
			.buffer = obj.uniform_buffer.buffer,
			.offset = 0,
			.range = sizeof(Uniforms),
		};

		const VkWriteDescriptorSet write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = obj.descriptor_set,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &buffer_info,
		};

		vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);
	}

	return true;
}

void shutdown() {
	auto& context = graphics::internal::context;
	vkQueueWaitIdle(context.graphics_queue);

	vkDestroyPipeline(context.device, pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);
	vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr); // наборы удаляются вместе с пулом
	vkDestroyDescriptorSetLayout(context.device, descriptor_set_layout, nullptr);

	for (int i = 0; i < object_count; ++i) {
		graphics::destroyBuffer(objects[i].uniform_buffer);
	}
	graphics::destroyBuffer(index_buffer);
	graphics::destroyBuffer(vertex_buffer);
}

void update(double time) {
	auto& context = graphics::internal::context;

	// dt — сколько секунд прошло с прошлого кадра
	static double last_time = time;
	float dt = float(time - last_time);
	last_time = time;

	if (playing) {
		anim_time += dt * speed;
	}

	// Интерфейс
	ImGui::Begin("Octahedron", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

	ImGui::SeparatorText("Projection");
	if (ImGui::RadioButton("Perspective", perspective)) {
		perspective = true;
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("Orthographic", !perspective)) {
		perspective = false;
	}
	if (perspective) {
		ImGui::SliderFloat("FOV", &fov, 20.0f, 120.0f);
	} else {
		ImGui::SliderFloat("Size", &ortho_size, 1.0f, 10.0f);
	}

	ImGui::SeparatorText("Animation");
	ImGui::Checkbox("Play", &playing);
	ImGui::SliderFloat("Speed", &speed, 0.0f, 5.0f);
	ImGui::SliderFloat("Radius", &radius, 0.0f, 3.0f);

	ImGui::SeparatorText("Objects");
	for (int i = 0; i < object_count; ++i) {
		Object& obj = objects[i];

		// PushID нужен, чтобы у полей разных объектов с одинаковыми названиями были разные ID
		ImGui::PushID(i);
		if (ImGui::CollapsingHeader(("Object " + std::to_string(i + 1)).c_str())) {
			ImGui::DragFloat3("Position", glm::value_ptr(obj.position), 0.01f);
			ImGui::SliderFloat3("Rotation", glm::value_ptr(obj.rotation), -180.0f, 180.0f);
			ImGui::DragFloat3("Scale", glm::value_ptr(obj.scale), 0.01f, 0.1f, 5.0f);
			ImGui::ColorEdit3("Color", glm::value_ptr(obj.color));
		}
		ImGui::PopID();
	}

	ImGui::End();

	// Матрицы View и Projection одинаковые для всех объектов
	float aspect = float(context.swapchain_extent.width) / float(context.swapchain_extent.height);

	glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 3.0f, 7.0f),  // где камера
	                             glm::vec3(0.0f, 0.0f, 0.0f),  // куда смотрит
	                             glm::vec3(0.0f, 1.0f, 0.0f)); // где верх

	glm::mat4 projection;
	if (perspective) {
		projection = glm::perspectiveRH_ZO(glm::radians(fov), aspect, 0.1f, 100.0f);
	} else {
		projection = glm::orthoRH_ZO(-ortho_size * aspect, ortho_size * aspect,
		                             -ortho_size, ortho_size, 0.1f, 100.0f);
	}
	projection[1][1] *= -1.0f; // в Vulkan ось Y направлена вниз, а GLM рассчитан на OpenGL

	// Траектория — «восьмёрка» (фигура Лиссажу) с покачиванием по высоте.
	// Пока объекты летят, они ещё и вращаются вокруг своей оси Y
	glm::vec3 offset = radius * glm::vec3(std::sin(anim_time),
	                                      0.25f * std::sin(3.0f * anim_time),
	                                      0.5f * std::sin(2.0f * anim_time));
	float spin = 45.0f * anim_time; // градусов

	for (int i = 0; i < object_count; ++i) {
		Object& obj = objects[i];

		// Model = T * Rz * Ry * Rx * S: сначала масштаб, потом повороты, потом перенос
		glm::mat4 model(1.0f);
		model = glm::translate(model, obj.position + offset);
		model = glm::rotate(model, glm::radians(obj.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
		model = glm::rotate(model, glm::radians(obj.rotation.y + spin), glm::vec3(0.0f, 1.0f, 0.0f));
		model = glm::rotate(model, glm::radians(obj.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		model = glm::scale(model, obj.scale);

		obj.uniforms.model = model;
		obj.uniforms.view = view;
		obj.uniforms.projection = projection;
		obj.uniforms.color = glm::vec4(obj.color, 1.0f);
	}
}

void render(const graphics::internal::FrameData& fd) {
	auto& context = graphics::internal::context;
	VkCommandBuffer cmd = fd.command_buffer;

	// Копируем матрицы в uniform-буферы здесь, а не в update():
	// к этому моменту prepare() уже дождался, что GPU закончил прошлый кадр
	for (int i = 0; i < object_count; ++i) {
		std::memcpy(objects[i].uniform_buffer.mapped, &objects[i].uniforms, sizeof(Uniforms));
	}

	vkResetCommandBuffer(cmd, 0);

	const VkCommandBufferBeginInfo begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(cmd, &begin_info);

	// Render pass очищает картинку цветом фона, а буфер глубины — единицей (самое далёкое)
	VkClearValue clear_values[2] = {};
	clear_values[0].color = {{0.05f, 0.05f, 0.08f, 1.0f}};
	clear_values[1].depthStencil = {1.0f, 0};

	const VkRenderPassBeginInfo render_pass_info = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = context.render_pass,
		.framebuffer = fd.framebuffer,
		.renderArea = {.extent = context.swapchain_extent},
		.clearValueCount = 2,
		.pClearValues = clear_values,
	};
	vkCmdBeginRenderPass(cmd, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

	const VkViewport viewport = {
		.x = 0.0f,
		.y = 0.0f,
		.width = float(context.swapchain_extent.width),
		.height = float(context.swapchain_extent.height),
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};
	const VkRect2D scissor = {.extent = context.swapchain_extent};

	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer.buffer, &offset);
	vkCmdBindIndexBuffer(cmd, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT16);

	// Геометрия и конвейер общие, у каждого объекта отличается только набор дескрипторов
	for (int i = 0; i < object_count; ++i) {
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
		                        0, 1, &objects[i].descriptor_set, 0, nullptr);
		vkCmdDrawIndexed(cmd, 24, 1, 0, 0, 0); // 24 индекса = 8 треугольников
	}

	vkCmdEndRenderPass(cmd);
	vkEndCommandBuffer(cmd);
}

} // namespace application
