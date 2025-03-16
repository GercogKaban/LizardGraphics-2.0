﻿#pragma once

#include <vector>
#include <array>
#include <functional>

#include "globals.h"
#include "vulkan/vulkan.h"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/glm.hpp"

namespace LG
{
    class LDummy
    {

    };

    class LPrimitiveVertexBuffer
    {
    public:
        
        struct Vertex
        {
            glm::vec3 pos;
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
            attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[0].offset = offsetof(Vertex, pos);

            attributeDescriptions[1].binding = 0;
            attributeDescriptions[1].location = 1;
            attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[1].offset = offsetof(Vertex, color);

            return attributeDescriptions;
        }

        virtual const std::vector<Vertex>& getVertexBuffer() 
        { 
            static std::vector<Vertex> dummy;
            return dummy; 
        };
        virtual const std::vector<uint16>& getIndexBuffer() 
        { 
            static std::vector<uint16> dummy;
            return dummy; 
        };
        
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

    class LCubeVertexBuffer : public LPrimitiveVertexBuffer
    {

    public:
        virtual const std::vector<Vertex>& getVertexBuffer() override
        {
            return verticesCube;
        }

        virtual const std::vector<uint16>& getIndexBuffer() override
        {
            return indicesCube;
        }

    protected:

        static const std::vector<Vertex> verticesCube;
        static const std::vector<uint16> indicesCube;
    };
    
    class LGraphicsComponent
    {
        LGraphicsComponent(const LGraphicsComponent&) = delete;
        LGraphicsComponent& operator=(const LGraphicsComponent&) = delete;

    public:
        
        LGraphicsComponent();
        virtual ~LGraphicsComponent();

        //bool isModified() const {return bModified;}

        std::function<glm::mat4x4 ()> getModelMatrix = [](){ return glm::mat4x4(1); };;
        // virtual const glm::mat4x4 getModelMatrix() const = 0;
        //void setModelMatrix(const glm::mat4& modelMatrix)
        //{
        //    this->modelMatrix = modelMatrix;
        //    bModified = true;
        //}
        //
        //void translate(const glm::vec3& translation)
        //{
        //    modelMatrix = glm::translate(modelMatrix, translation);
        //    bModified = true;
        //}

        //void rotate(float angle, const glm::vec3& axis)
        //{
        //    modelMatrix = glm::rotate(modelMatrix, angle, axis);
        //    bModified = true;
        //}

        //void scale(const glm::vec3& scale)
        //{
        //    modelMatrix = glm::scale(modelMatrix, scale);
        //    bModified = true;
        //}

        // some stupid hack, they are const only for user, so their values will be initialized soon
        const std::string typeName = "";
        const uint32 indicesCount = 0;
        
    protected:
        
        //glm::mat4 modelMatrix = glm::mat4(1.0f);
        //mutable bool bModified = true;
    };

    class LGFullGraphicsComponent : virtual public LGraphicsComponent, virtual public LPrimitiveVertexBuffer
    {
    };

    class LPlane : public LPlaneVertexBuffer, public LGFullGraphicsComponent
    {
    };

    class LCube : public LCubeVertexBuffer, public LGFullGraphicsComponent
    {
    };
}