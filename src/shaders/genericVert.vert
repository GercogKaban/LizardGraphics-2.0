#version 450

layout(push_constant) uniform UniformBufferObject 
{
    mat4 mvp;
} constants;

layout (set = 0, binding = 0) uniform UBO
{
    mat4 projection;
} ubo[];


layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

void main() 
{
    gl_Position = constants.mvp * vec4(inPosition, 1.0);
    fragColor = inColor;
}