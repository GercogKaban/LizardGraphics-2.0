#version 450

struct SSBOEntry 
{
    mat4 model;
    uint textureId;
    uint isPortal;
    uint reserved2;
    uint reserved3;
};

layout(push_constant) uniform UniformBufferObject 
{
    mat4 projView;
} constants;

layout (binding = 0) buffer SSBO
{
    SSBOEntry entries[];
} ssbo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) flat out uint textureId;
layout(location = 3) flat out uint isPortal;

void main() 
{
    gl_Position = constants.projView * ssbo.entries[gl_InstanceIndex].model * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
    textureId = ssbo.entries[gl_InstanceIndex].textureId;
    isPortal = ssbo.entries[gl_InstanceIndex].isPortal;
}