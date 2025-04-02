#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint textureId;
layout(location = 3) flat in uint isPortal;
layout(location = 4) flat in mat4 view;

layout(binding = 1) uniform sampler2D texSampler[];

layout(location = 0) out vec4 outColor;

void main() 
{
    vec2 newCoords = fragTexCoord;
    vec4 position = vec4(view[0][3], view[1][3], view[2][3], view[3][3]);

    if (isPortal == 1)
    {
        newCoords.x = 1.0f - newCoords.x;
        newCoords.y = 1.0f - newCoords.y;
    }

    outColor = texture(texSampler[nonuniformEXT(textureId)], newCoords);
}