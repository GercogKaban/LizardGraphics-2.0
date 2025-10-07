#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint textureId;
layout(location = 3) flat in uint isPortal;
layout(location = 4) flat in vec2 extent;
layout(location = 5) in vec3 position;


layout(binding = 1) uniform sampler2D texSampler[];

layout(location = 0) out vec4 outColor;

void main() 
{
    vec2 newCoords = fragTexCoord;

    if (isPortal > 0)
    {
        bool xAxis = position.x > -0.5f && position.x < -0.48f || position.x < 0.5f && position.x > 0.48f;
        bool zAxis = position.z > -0.5f && position.z < -0.49f || position.z < 0.5f && position.z > 0.49f;
        if (xAxis || zAxis)
        {
            if (isPortal == 1)
            {
                outColor = vec4(0.157f, 0.118f, 0.365f, 1.0f);
            }
            else if (isPortal == 2)
            {
                outColor = vec4(0.867f, 0.341f, 0.11f, 1.0f);
            }
            else 
            {
                outColor = vec4(1.0f, 0.0f, 0.0f, 1.0f);
            }
        }
        else 
        {
            newCoords.x = gl_FragCoord.x / extent.x;
            newCoords.y = gl_FragCoord.y / extent.y;
            outColor = texture(texSampler[nonuniformEXT(textureId)], newCoords);
        }
    } 
    else 
    {
        outColor = texture(texSampler[nonuniformEXT(textureId)], newCoords);
    }
}