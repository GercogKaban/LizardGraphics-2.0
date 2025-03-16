#include "pch.h"
#include "Primitives.h"
#include "LRenderer.h"

const std::vector<LG::LPrimitiveVertexBuffer::Vertex> LG::LPlaneVertexBuffer::verticesPlane =
{
    {{-0.5f, -0.5f, 0.0f},{1.0f, 0.0f, 0.0f}},
    {{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    {{0.5f, 0.5f, 0.0f},  {0.0f, 0.0f, 1.0f}},
    {{-0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}}
};

const std::vector<uint16> LG::LPlaneVertexBuffer::indicesPlane =
{
    0, 1, 2, 2, 3, 0
};

// Define the 8 unique vertices of the cube
const std::vector<LG::LPrimitiveVertexBuffer::Vertex> LG::LCubeVertexBuffer::verticesCube =
{
    // Front face
    {{-0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}}, // 0
    {{ 0.5f, -0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}}, // 1
    {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}}, // 2
    {{-0.5f,  0.5f,  0.5f}, {1.0f, 1.0f, 0.0f}}, // 3

    // Back face
    {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 1.0f}}, // 4
    {{ 0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 1.0f}}, // 5
    {{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}}, // 6
    {{-0.5f,  0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}}  // 7
};

// Define indices to form triangles
const std::vector<uint16_t> LG::LCubeVertexBuffer::indicesCube =
{
    // Front face
    0, 1, 2, 2, 3, 0,
    // Right face
    1, 5, 6, 6, 2, 1,
    // Back face
    5, 4, 7, 7, 6, 5,
    // Left face
    4, 0, 3, 3, 7, 4,
    // Top face
    3, 2, 6, 6, 7, 3,
    // Bottom face
    4, 5, 1, 1, 0, 4
};

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
