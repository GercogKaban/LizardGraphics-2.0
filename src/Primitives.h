#pragma once

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

    class LGraphicsComponent
    {
        LGraphicsComponent(const LGraphicsComponent&) = delete;
        LGraphicsComponent& operator=(const LGraphicsComponent&) = delete;

    public:

        struct Vertex
        {
            glm::vec3 pos;
            glm::vec3 color;
            glm::vec2 texCoord;
        };

        static VkVertexInputBindingDescription getBindingDescription()
        {
            VkVertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(Vertex);
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            return bindingDescription;
        }

        static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions()
        {
            std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

            attributeDescriptions[0].binding = 0;
            attributeDescriptions[0].location = 0;
            attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[0].offset = offsetof(Vertex, pos);

            attributeDescriptions[1].binding = 0;
            attributeDescriptions[1].location = 1;
            attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[1].offset = offsetof(Vertex, color);

            attributeDescriptions[2].binding = 0;
            attributeDescriptions[2].location = 2;
            attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
            attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

            return attributeDescriptions;
        }

        virtual const std::vector<Vertex>& getVertexBuffer() const
        {
            static std::vector<Vertex> dummy;
            return dummy;
        };

        virtual const std::vector<uint16>& getIndexBuffer() const
        {
            static std::vector<uint16> dummy;
            return dummy;
        };

        const std::string& getTypeName()
        {
            return typeName;
        }
        
        LGraphicsComponent();
        virtual ~LGraphicsComponent();

        std::function<glm::mat4x4 ()> getModelMatrix = [](){ return glm::mat4x4(1); };;
        
        std::string typeName;
        uint32 indicesCount = 0;
    };

    extern const std::vector<LG::LGraphicsComponent::Vertex> verticesCube;
    extern const std::vector<uint16> indicesCube;

    class LCube : public LGraphicsComponent
    {

    public:

        LCube()
        {
            typeName = std::string("LCube");
        }

        virtual const std::vector<Vertex>& getVertexBuffer() const override
        {
            return verticesCube;
        }

        virtual const std::vector<uint16>& getIndexBuffer() const override
        {
            return indicesCube;
        }
    };

    extern const std::vector<LG::LGraphicsComponent::Vertex> verticesPlane;
    extern const std::vector<uint16> indicesPlane;

    class LPlane : public LGraphicsComponent
    {

    public:

        LPlane()
        {
            typeName = std::string("LPlane");
        }

        virtual const std::vector<Vertex>& getVertexBuffer() const override
        {
            return verticesPlane;
        }

        virtual const std::vector<uint16>& getIndexBuffer() const override
        {
            return indicesPlane;
        }
    };

    template<typename Component>
    bool isInstancePrimitive(Component* ptr)
    {
        if (dynamic_cast<LG::LCube*>(ptr))
        {
            return true;
        }
        else if (dynamic_cast<LG::LPlane*>(ptr))
        {
            return true;
        }
        return false;
    }

}