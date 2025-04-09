#version 450

layout(push_constant) uniform UniformBufferObject 
{
    mat4 mvp;
    float width;
    float height;
    float reserved1;
    float reserved2;

    //ivec4 textureId_R_R_R;
    //ivec4 R_R_R_R1;
    //ivec4 R_R_R_R2;
    //ivec4 R_R_R_R3;
} constants;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) flat out uint textureId;
layout(location = 3) flat out uint isPortal;
layout(location = 4) flat out vec2 extent;

void main() 
{
    gl_Position = constants.mvp * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
    textureId = 0; //constants.textureId_R_R_R.x;
    isPortal = 0;
    extent = vec2(constants.width, constants.height);
}