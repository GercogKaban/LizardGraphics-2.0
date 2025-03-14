#pragma once

#include <vector>
#include <array>

#include "globals.h"
#include "vulkan/vulkan.h"
#include "glm/vec4.hpp"

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
    
    class LPlaneVertexBuffer : public LPrimitiveVertexBuffer
    {
        
    public:
        virtual const std::vector<Vertex>& getVertexBuffer() override
        {
            return verticesPlane;
        }

        virtual const std::vector<uint16>& getIndexBuffer() override
        {
            return indicesPlane;
        }
        
    protected:

        static const std::vector<Vertex> verticesPlane;
        static const std::vector<uint16> indicesPlane;
    };
    
    class LPrimitiveMesh
    {
        LPrimitiveMesh(const LPrimitiveMesh&) = delete;
        LPrimitiveMesh& operator=(const LPrimitiveMesh&) = delete;

    public:
        
        LPrimitiveMesh();
        virtual ~LPrimitiveMesh();

        bool isModified() const {return bModified;}

        const glm::mat4& getModelMatrix() const {return modelMatrix;}
        void setModelMatrix(const glm::mat4& modelMatrix)
        {
            this->modelMatrix = modelMatrix;
            bModified = true;
        }
        
        void translate(const glm::vec3& translation)
        {
            modelMatrix = glm::translate(modelMatrix, translation);
            bModified = true;
        }

        void rotate(float angle, const glm::vec3& axis)
        {
            modelMatrix = glm::rotate(modelMatrix, angle, axis);
            bModified = true;
        }

        void scale(const glm::vec3& scale)
        {
            modelMatrix = glm::scale(modelMatrix, scale);
            bModified = true;
        }

        // some stupid hack, they are const only for user, so their values will be initialized soon
        const std::string typeName = "";
        const uint32 indicesCount = 0;
        
    protected:
        
        glm::mat4 modelMatrix = glm::mat4(1.0f);
        mutable bool bModified = true;
    };
    
    class LPlane : public LPrimitiveMesh, public LPlaneVertexBuffer
    {
    };
}
