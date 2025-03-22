#version 450

layout (binding = 0) buffer SSBO
{
    mat4 mvp[];
} ssbo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

void main() 
{
    gl_Position = ssbo.mvp[gl_InstanceIndex] * vec4(inPosition, 1.0);
    fragColor = inColor;
}