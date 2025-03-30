#version 450

layout(push_constant) uniform UniformBufferObject 
{
    mat4 mvp;
    uint textureId;
    uint reserved1;
    uint reserved2;
    uint reserved3;
} constants;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) flat out uint textureId;

void main() 
{
    gl_Position = constants.mvp * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
    textureId = 0;
}