#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint textureId;
layout(location = 3) flat in uint isPortal;
layout(location = 4) flat in vec2 extent;


layout(binding = 1) uniform sampler2D texSampler[];

layout(location = 0) out vec4 outColor;

void main() 
{
    vec2 newCoords = fragTexCoord;

    if (isPortal == 1)
    {
        newCoords.x = gl_FragCoord.x / extent.x;
        newCoords.y = gl_FragCoord.y / extent.y;
    }

    outColor = texture(texSampler[nonuniformEXT(textureId)], newCoords);
}