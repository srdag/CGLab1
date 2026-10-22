#version 450

// Входы из вершинного буфера (номера location такие же, как в описании вершины в C++)
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;

// Uniform-буфер объекта: набор дескрипторов 0, привязка 0.
// Порядок полей такой же, как в struct Uniforms в application.cpp
layout(set = 0, binding = 0) uniform Uniforms {
	mat4 model;
	mat4 view;
	mat4 projection;
	vec4 color;
} u;

layout(location = 0) out vec3 v_color;

void main() {
	gl_Position = u.projection * u.view * u.model * vec4(in_position, 1.0);

	// Цвет вершины умножаем на цвет объекта, выбранный в интерфейсе
	v_color = in_color * u.color.rgb;
}
