#pragma once

#include <vector>
#include <array>

#include "globals.h"
#include "vulkan/vulkan.h"
#include "glm/glm.hpp"

namespace Primitives
{
    class LPrimitiveVertexBuffer
    {
    public:
        
        struct Vertex
        {
            glm::vec2 pos;
            glm::vec3 color;
        };
        
        static VkVertexInputBindingDescription getBindingDescription()
        {
            VkVertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(Vertex);
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
			
            return bindingDescription;
        }
		
        static std::array<VkVertexInputAttributeDescription, 2> getAttributeDescriptions()
        {
            std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};
			
            attributeDescriptions[0].binding = 0;
            attributeDescriptions[0].location = 0;
            attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
            attributeDescriptions[0].offset = offsetof(Vertex, pos);

            attributeDescriptions[1].binding = 0;
            attributeDescriptions[1].location = 1;
            attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[1].offset = offsetof(Vertex, color);

            return attributeDescriptions;
        }

        virtual const std::vector<Vertex>& getVertexBuffer() = 0;
        virtual const std::vector<uint16>& getIndexBuffer() = 0;
        
        virtual ~LPrimitiveVertexBuffer() = default;
    };
    
    class LRectangleVertexBuffer : public LPrimitiveVertexBuffer
    {
        
    public:
        virtual const std::vector<Vertex>& getVertexBuffer() override
        {
            return verticesRectangle;
        }

        virtual const std::vector<uint16>& getIndexBuffer() override
        {
            return indicesRectangle;
        }
        
    protected:

        static const std::vector<Vertex> verticesRectangle;
        static const std::vector<uint16> indicesRectangle;
    };

    class LPrimitiveMesh
    {
        LPrimitiveMesh(const LPrimitiveMesh&) = delete;
        LPrimitiveMesh& operator=(const LPrimitiveMesh&) = delete;

    public:
        
        LPrimitiveMesh();
        virtual ~LPrimitiveMesh();

        std::string typeName;
        uint32 indicesCount;
        
    protected:
        
        glm::mat4 modelMatrix = glm::mat4(1.0f);
    };
    
    class LRectangle : public LPrimitiveMesh, public LRectangleVertexBuffer
    {
    };
}
