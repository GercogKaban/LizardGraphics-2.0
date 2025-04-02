#include "Primitives.h"
#include "pch.h"
#include "LRenderer.h"

std::set<std::string> LG::LGraphicsComponent::textures;
uint32 LG::LPortal::portalCounter = 0;

const std::vector<LG::LGraphicsComponent::Vertex> LG::verticesPlane =
{
    {{-0.5f, 0.0f, 0.5f},{1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}} ,
    {{0.5f, 0.0f, 0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
    {{0.5f, 0.0f, -0.5f},  {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
    {{-0.5f, 0.0f, -0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}}
};

const std::vector<uint16> LG::indicesPlane =
{
    0, 1, 2, 2, 3, 0
};

const std::vector<LG::LGraphicsComponent::Vertex> LG::verticesCube =
{
    // 8 shared corner vertices with unique UVs where necessary
    {{-0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}}, // 0
    {{ 0.5f, -0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}}, // 1
    {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}}, // 2
    {{-0.5f,  0.5f,  0.5f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f}}, // 3

    {{ 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 1.0f}, {0.0f, 0.0f}}, // 4
    {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 1.0f}, {1.0f, 0.0f}}, // 5
    {{-0.5f,  0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}}, // 6
    {{ 0.5f,  0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}, {0.0f, 1.0f}}, // 7

    // Extra 6 unique vertices for correct texture mapping
    {{-0.5f,  0.5f, -0.5f}, {0.5f, 0.0f, 1.0f}, {0.0f, 0.0f}}, // 8  (for top)
    {{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 1.0f}, {1.0f, 0.0f}}, // 9  (for top)
    {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 1.0f}, {1.0f, 1.0f}}, // 10 (for top)
    {{ 0.5f,  0.5f, -0.5f}, {0.5f, 1.0f, 0.0f}, {0.0f, 1.0f}}, // 11 (for top)

    {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.5f, 0.0f}, {0.0f, 0.0f}}, // 12 (for bottom)
    {{ 0.5f, -0.5f, -0.5f}, {0.5f, 1.0f, 0.5f}, {0.0f, 0.0f}}, // 13 (for bottom)
};

const std::vector<uint16_t> LG::indicesCube =
{
    // Front face
    0, 1, 2, 2, 3, 0,
    // Back face
    4, 5, 6, 6, 7, 4,
    // Left face
    5, 0, 3, 3, 6, 5,
    // Right face
    1, 4, 7, 7, 2, 1,
    // Top face
    8, 9, 10, 10, 11, 8,
    // Bottom face
    12, 13, 1, 1, 0, 12
};

void LG::LGraphicsComponent::setColorTexture(std::string&& path)
{
    textures.insert(path);
    texturePath = path;
}

LG::LGraphicsComponent::LGraphicsComponent()
{
// DEBUG_CODE(
//     if (!RenderComponentBuilder::isConstructing())
//     {
//         RAISE_VK_ERROR("Use ObjectBuilder::construct to create the objects")
//     }
// )
}

LG::LGraphicsComponent::~LGraphicsComponent()
{
    ::RenderComponentBuilder::destruct(this);
}

void LG::LPortal::setPortalView(const glm::mat4& view)
{
    this->view = view;
    bNeedRecalculation = true;
}
