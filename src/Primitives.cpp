#include "pch.h"
#include "Primitives.h"
#include "LRenderer.h"

const std::vector<Primitives::LPrimitiveVertexBuffer::Vertex> Primitives::LPlaneVertexBuffer::verticesPlane =
{
    {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
    {{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
    {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
    {{-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}}
};

const std::vector<uint16> Primitives::LPlaneVertexBuffer::indicesPlane =
{
    0, 1, 2, 2, 3, 0
};

Primitives::LPrimitiveMesh::LPrimitiveMesh()
{
#ifndef NDEBUG
    if (!ObjectBuilder::isConstructing())
    {
        RAISE_VK_ERROR("Use ObjectBuilder::construct to create the objects")
    }
#endif
}

Primitives::LPrimitiveMesh::~LPrimitiveMesh()
{
    ::ObjectBuilder::destruct(this);
}
