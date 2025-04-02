#define VMA_IMPLEMENTATION
#include "LRenderer.h"
#include "pch.h"

#define private public
#define protected public
#include "Primitives.h"
#undef private
#undef protected

#define GLFW_INCLUDE_VULKAN
#include <glfw3.h>

#include <tracy/Tracy.hpp>

#include "LWindow.h"
#include "vulkan/vulkan.h"

//temporary
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include "Util.h"

#include "gen_shaders.cxx"

DEBUG_CODE(
    bool RenderComponentBuilder::bIsConstructing = false;
)

LRenderer* LRenderer::thisPtr = nullptr;
bool LRenderer::bFramebufferResized = false;
std::unordered_map<std::string, int32> RenderComponentBuilder::objectsCounter;
std::unordered_map<std::string, LRenderer::VkMemoryBuffer> RenderComponentBuilder::memoryBuffers;

LRenderer::LRenderer(const std::unique_ptr<LWindow>& window, StaticInitData&& initData)
    :primitiveCounterInitData(initData.primitiveCounter), maxPortalNum(initData.maxPortalNum)
{
    if (thisPtr)
    {
        RAISE_VK_ERROR("LRenderer is a singleton object, can't create more than 1")
    }
    
    thisPtr = this;
    
    this->window = window.get()->getWindow();
    specs = window.get()->getWindowSpecs();

    texturesInitData.reserve(initData.textures.size());

    {
        uint32 textureId = 0;
        for (const auto& texture : initData.textures)
        {
            texturesInitData[texture] = textureId++;
        }
    }

    glfwSetWindowUserPointer(this->window, this);
    glfwSetFramebufferSizeCallback(this->window, framebufferResizeCallback);
	init();
}

LRenderer::~LRenderer()
{
    cleanup();
    thisPtr = nullptr;
}

void LRenderer::init()
{
    HANDLE_VK_ERROR(createInstance())
    HANDLE_VK_ERROR(setupDebugMessenger())
    HANDLE_VK_ERROR(createSurface())
    HANDLE_VK_ERROR(pickPhysicalDevice())
    HANDLE_VK_ERROR(createLogicalDevice())
    HANDLE_VK_ERROR(createAllocator())
    HANDLE_VK_ERROR(createSwapChain())

    initProjection();
    initView();
    
    mainPass = std::make_unique<RenderPass>(logicalDevice, swapChainImageFormat, findDepthFormat(), true);
    HANDLE_VK_ERROR(createDescriptorSetLayout())

    GraphicsPipelineParams mainPipelineParams;
    mainPipelineParams.bInstanced = true;
    mainPipelineParams.polygonMode = VkPolygonMode::VK_POLYGON_MODE_FILL;


    HANDLE_VK_ERROR(createGraphicsPipeline(mainPipelineParams, graphicsPipelineInstanced, mainPass->getRenderPass()))

    mainPipelineParams.bInstanced = false;
    HANDLE_VK_ERROR(createGraphicsPipeline(mainPipelineParams, graphicsPipelineRegular, mainPass->getRenderPass()))

        //DEBUG_CODE(
        //    GraphicsPipelineParams debugPipelineParams;
        //    debugPipelineParams.polygonMode = VkPolygonMode::VK_POLYGON_MODE_LINE;
        //    HANDLE_VK_ERROR(createGraphicsPipeline(debugPipelineParams, debugGraphicsPipeline))
        //)

    HANDLE_VK_ERROR(createCommandPool())

    createFramebuffers(swapChainRt.get(), swapChainExtent, swapChainSize, mainPass->getRenderPass());

    for (uint32 i = 0; i < maxPortalNum; ++i)
    {
        auto portalPass = std::make_unique<RenderPass>(logicalDevice, swapChainImageFormat, findDepthFormat(), false);
        HANDLE_VK_ERROR(createPortalRenderTarget())

        GraphicsPipelineParams mainPipelineParams;
        mainPipelineParams.bInstanced = true;
        mainPipelineParams.polygonMode = VkPolygonMode::VK_POLYGON_MODE_FILL;
        HANDLE_VK_ERROR(createGraphicsPipeline(mainPipelineParams, graphicsPipelineInstancedPortal, portalPass->getRenderPass()))

        mainPipelineParams.bInstanced = false;
        HANDLE_VK_ERROR(createGraphicsPipeline(mainPipelineParams, graphicsPipelineRegularPortal, portalPass->getRenderPass()))
        createFramebuffers(portalsRt[i].get(), swapChainExtent, maxFramesInFlight, portalPass->getRenderPass());

        portalPasses.emplace_back(std::move(portalPass));
    }

    if (maxPortalNum > 0)
    {
        createTextureSampler(portalSampler, 0);
    }

    initStaticDataTextures();
    createInstancesStorageBuffers();

    HANDLE_VK_ERROR(createDescriptorPool())
    HANDLE_VK_ERROR(createDescriptorSets())
    HANDLE_VK_ERROR(createCommandBuffers())
    HANDLE_VK_ERROR(createSyncObjects())
}

void LRenderer::cleanup()
{
    swapChainRt.reset();

    for (auto& portalRt : portalsRt)
    {
        portalRt.reset();
    }

    for (auto& [_, sampler] : textureSamplers)
    {
        vkDestroySampler(logicalDevice, sampler, nullptr);
    }

    for (auto& [_, image] : images)
    {
        vkDestroyImageView(logicalDevice, image.imageView, nullptr);
        vmaDestroyImage(allocator, image.image, image.allocation);
    }

    vkDestroyDescriptorPool(logicalDevice, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(logicalDevice, descriptorSetLayout, nullptr);

//DEBUG_CODE(
//    vkDestroyPipeline(logicalDevice, debugGraphicsPipeline, nullptr);
//)

    vkDestroyPipeline(logicalDevice, graphicsPipelineInstanced, nullptr);
    vkDestroyPipeline(logicalDevice, graphicsPipelineRegular, nullptr);
    vkDestroyPipelineLayout(logicalDevice, pipelineLayout, nullptr);

    mainPass.reset();

    for (auto& portalPass : portalPasses)
    {
        portalPass.reset();
    }
    
    for (uint32 i = 0; i < maxFramesInFlight; ++i)
    {
        vkDestroySemaphore(logicalDevice, imageAvailableSemaphores[i], nullptr);
        vkDestroySemaphore(logicalDevice, renderFinishedSemaphores[i], nullptr);
        vkDestroyFence(logicalDevice, inFlightFences[i], nullptr);
    }
    
    vkDestroyCommandPool(logicalDevice, commandPool, nullptr);

    for (auto& [buffer, allocation] : primitivesData)
    {
        vmaDestroyBuffer(allocator, buffer, allocation);
    }

    vmaUnmapMemory(allocator, stagingBuffer.memory);
    vmaDestroyBuffer(allocator, stagingBuffer.buffer, stagingBuffer.memory);
    vmaDestroyAllocator(allocator);

    vkDestroyDevice(logicalDevice, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
}

glm::mat4 LRenderer::computeExitPortalView(
    const glm::vec3& playerPos,
    const glm::vec3& enterPos, const glm::vec3& enterNormal,
    const glm::vec3& exitPos, const glm::vec3& exitNormal
) {
    // Compute basis vectors for enter portal
    glm::vec3 enterRight = glm::normalize(glm::cross(glm::vec3(0, 1, 0), enterNormal));
    glm::vec3 enterUp = glm::normalize(glm::cross(enterNormal, enterRight));

    // Compute basis vectors for exit portal
    glm::vec3 exitRight = glm::normalize(glm::cross(glm::vec3(0, 1, 0), exitNormal));
    glm::vec3 exitUp = glm::normalize(glm::cross(exitNormal, exitRight));

    // Create transformation matrices for both portals
    glm::mat4 enterTransform = glm::mat4(1.0f);
    enterTransform[0] = glm::vec4(enterRight, 0.0f);
    enterTransform[1] = glm::vec4(enterUp, 0.0f);
    enterTransform[2] = glm::vec4(enterNormal, 0.0f);
    enterTransform[3] = glm::vec4(enterPos, 1.0f);

    glm::mat4 exitTransform = glm::mat4(1.0f);
    exitTransform[0] = glm::vec4(exitRight, 0.0f);
    exitTransform[1] = glm::vec4(exitUp, 0.0f);
    exitTransform[2] = glm::vec4(exitNormal, 0.0f);
    exitTransform[3] = glm::vec4(exitPos, 1.0f);

    // Transform player position to exit portal space
    glm::mat4 enterToExit = exitTransform * glm::inverse(enterTransform);
    glm::vec4 transformedPos = enterToExit * glm::vec4(playerPos, 1.0f);

    // Compute player's forward direction in portal space
    glm::vec3 forward = glm::normalize(playerPos - enterPos);
    glm::vec4 transformedForward = enterToExit * glm::vec4(forward, 0.0f);

    // Compute the new camera view matrix
    return glm::lookAt(
        glm::vec3(transformedPos),  // New camera position
        glm::vec3(transformedPos) + glm::vec3(transformedForward),  // Look direction
        glm::vec3(exitUp)  // Up vector
    );
}

void LRenderer::doPortalPass(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, 
    const std::unique_ptr<RenderPass>& portalPass, uint32 portal1Ind, uint32 portal2Ind)
{
    std::shared_ptr<LG::LPortal> portalPtr1 = portals[portal1Ind].lock();
    std::shared_ptr<LG::LPortal> portalPtr2 = portals[portal2Ind].lock();

    {
        //glm::vec3 playerUp = getCameraUp(storedView);
        //glm::vec3 playerForward = getCameraForward(storedView);
        glm::vec3 playerPos = getCameraPosition(storedView);

        glm::vec3 upA = getCameraUp(portalPtr1->view);
        glm::vec3 forwardA = getCameraForward(portalPtr1->view);
        glm::vec3 posA = getCameraPosition(portalPtr1->view);

        glm::vec3 upB = getCameraUp(portalPtr2->view);
        glm::vec3 forwardB = getCameraForward(portalPtr2->view);
        glm::vec3 posB = getCameraPosition(portalPtr2->view);

        //glm::vec3 playerToPortal = posA - playerPos;

        //float angleRadians = glm::acos(glm::dot(forwardA, -playerToPortal) / (glm::length(forwardA) * glm::length(playerToPortal)));
        //auto cross = glm::cross(forwardA, -playerToPortal);

        //float sign = cross.y > 0.0f ? 1.0f : -1.0f;
        //sign = std::abs(cross.y) < 0.000001f ? 0.0f : sign;
        
        //if (cross.y < 0.0f)
        //{
        //    float angleDegrees = glm::degrees(angleRadians);
        //    angleRadians = -angleRadians;
        //}
        //else
        //{
        //    float angleDegrees = glm::degrees(angleRadians);
        //    angleRadians = angleRadians;
        //}

        //glm::vec3 rightA = glm::cross(forwardA, upA);
        //glm::vec3 portalRightDir = rightA - posA;
        //glm::vec3 portalToPlayer = playerPos - posA;

        //float angleRadians = glm::acos(glm::dot(portalRightDir, portalToPlayer) / (glm::length(portalRightDir) * glm::length(portalToPlayer)));
        //float angleDegrees = glm::degrees(angleRadians);

        //assert(angleDegrees > 0.0f);
        
        //glm::vec3 cross = glm::cross(portalDir, portalToPlayer);
        //float sign = glm::dot(cross, upA);
        //float angleDegrees = glm::degrees(angleRadians);
        //if (sign > 0.0f)
        //{
        //   angleRadians = -angleRadians;
        //}


 
        // animated rotation
        //const float degreesInSecond = 10.0f;
        //static float rotationDegree = 0.0f;
        //static float rotationSign = -1.0f;

        //if (std::abs(rotationDegree) > 90.0f)
        //{
        //    rotationSign = -rotationSign;
        //} 
        //rotationDegree += rotationSign * degreesInSecond * delta;

        //glm::mat4 rotationMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(rotationDegree), glm::vec3(0, 1, 0));

        //glm::vec3 rotatedForwardB = rotationMatrix * glm::vec4(forwardB, 1.0f);
        //rotatedForwardB.z += 0.5f;

        //glm::mat4 virtualView = glm::lookAt(posB, posB + rotatedForwardB, upB

        
        glm::mat4 m = portalPtr2->getModelMatrix() * glm::inverse(portalPtr1->getModelMatrix()) * playerModel;

        //getCameraPosition();
        //getCameraForward();
        //getCameraUp();

        glm::vec3 newPosition = glm::vec3(m[3]); // Extract translation from matrix
        glm::vec3 newUp = glm::vec3(m[1]); // Up direction
        glm::vec3 newForward = -glm::vec3(m[2]); // Forward direction (negated for LookAt)

        glm::mat4 virtualView = glm::lookAt(
            newPosition,             // Camera position
            newPosition + newForward, // Look-at target
            newUp                    // Up vector
        );

        setView(virtualView);

        
        
        auto scales = extractScale(portalPtr2->getModelMatrix());
        setProjection(degrees, 3, 3, zNear, zFar);

        updateProjView();
        portalPass->beginPass(commandBuffer, framebuffer, swapChainExtent);
        doMainPass(commandBuffer, framebuffer, false);
        portalPass->endPass(commandBuffer);
    }
    //else
    {
        // TODO: should be erased
    }
}

void LRenderer::doMainPass(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, bool bSwitchRenderPass)
{
    // TODO: Ideally this thing should be incapsulated inside RenderPass->render(), but there is some work to do...
    if (bSwitchRenderPass)
    {
        mainPass->beginPass(commandBuffer, framebuffer, swapChainExtent);
    }
    auto drawStaticInstancedMeshes = [this, commandBuffer, bSwitchRenderPass]()
        {
            uint32 instanceArrayNum = 0;
            for (const auto& [typeName, primitives] : staticInstancedPrimitiveMeshes)
            {
                bool bIsPortal = typeName == "LPortal";
                // TODO: actually here we should only ignore current portal
                if (!bSwitchRenderPass && bIsPortal)
                {
                    continue;
                }

                const auto& memoryBuffer = RenderComponentBuilder::getMemoryBuffer(typeName);
                VkBuffer vertexBuffers[] = { memoryBuffer.vertexBuffer };
                VkDeviceSize offsets[] = { 0 };

                auto indicesCount = primitives[0].lock()->getIndexBuffer().size();
                auto instancesCount = primitives.size();

                SSBOData projViewConstants =
                {
                    .genericMatrix = projView,
                    .textureId = 0,
                    .isPortal = bIsPortal
                };

                vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &projViewConstants);

                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[currentFrame * staticInstancedPrimitiveMeshes.size() + instanceArrayNum], 0, nullptr);

                vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
                vkCmdBindIndexBuffer(commandBuffer, memoryBuffer.indexBuffer, 0, VK_INDEX_TYPE_UINT16);
                vkCmdDrawIndexed(commandBuffer, indicesCount, instancesCount, 0, 0, 0);
                ++instanceArrayNum;
            }
        };

    auto drawMeshes = [this, commandBuffer](std::vector<std::weak_ptr<LG::LGraphicsComponent>>& meshes)
        {
            for (auto it = meshes.begin(); it != meshes.end(); ++it)
            {
                if (!it->expired())
                {
                    LG::LGraphicsComponent& mesh = *it->lock();

                    PushConstants projViewConstants =
                    {
                        .genericMatrix = projView * mesh.getModelMatrix(),
                        .view = view
                    };

                    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &projViewConstants);

                    const auto& memoryBuffer = RenderComponentBuilder::getMemoryBuffer(mesh.getTypeName());
                    VkBuffer vertexBuffers[] = { memoryBuffer.vertexBuffer };
                    VkDeviceSize offsets[] = { 0 };

                    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
                    vkCmdBindIndexBuffer(commandBuffer, memoryBuffer.indexBuffer, 0, VK_INDEX_TYPE_UINT16);
                    vkCmdDrawIndexed(commandBuffer, mesh.getIndicesCount(), 1, 0, 0, 0);
                }
                else
                {
                    it = meshes.erase(it);
                }
            }
        };

    {
        ZoneScopedN("Instance pass");
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipelineInstanced);
        drawStaticInstancedMeshes();
    }

    {
        ZoneScopedN("Regular pass");
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipelineRegular);
        drawMeshes(primitiveMeshes);
    }

    //DEBUG_CODE(
    //    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, debugGraphicsPipeline);
    //    drawMeshes(debugMeshes);
    //          )

    if (bSwitchRenderPass)
    {
        mainPass->endPass(commandBuffer);
    }
}

bool LRenderer::checkValidationLayerSupport() const
{
    uint32 layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : validationLayers) 
    {
        bool layerFound = false;

        for (const auto& layerProperties : availableLayers) 
        {
            if (strcmp(layerName, layerProperties.layerName) == 0) 
            {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) 
        {
            return false;
        }
    }

    return true;
}

std::vector<const char*> LRenderer::getRequiredExtensions() const
{
    uint32 glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (enableValidationLayers)
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
}

VkResult LRenderer::setupDebugMessenger()
{
    if (!enableValidationLayers)
    {
        return VK_SUCCESS;
    }

    VkDebugUtilsMessengerCreateInfoEXT createInfo;
    populateDebugMessengerCreateInfo(createInfo);

    createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&createInfo;

    return CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger);
}

VkResult LRenderer::CreateDebugUtilsMessengerEXT(VkInstance instanceIn, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger)
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    return func ? func(instanceIn, pCreateInfo, pAllocator, pDebugMessenger) : VK_ERROR_EXTENSION_NOT_PRESENT;
}

VKAPI_ATTR VkBool32 VKAPI_CALL LRenderer::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
    LLogger::LogString(messageSeverity, true);
    LLogger::LogString(pCallbackData->pMessage, true);
    return VK_FALSE;
}

void LRenderer::DestroyDebugUtilsMessengerEXT(VkInstance instanceIn, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator)
{
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instanceIn, "vkDestroyDebugUtilsMessengerEXT");
    if (func) 
    {
        func(instanceIn, debugMessenger, pAllocator);
    }
}

void LRenderer::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo)
{
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
}

VkResult LRenderer::createInstance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Hello Triangle";
    appInfo.applicationVersion = VK_MAKE_VERSION(majorApiVersion, minorApiVersion, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(majorApiVersion, minorApiVersion, 0);
    appInfo.apiVersion = VK_MAKE_API_VERSION(0, majorApiVersion, minorApiVersion, 0);

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    
    // required extension request
    uint32_t glfwExtensionCount = 0;
    auto extensions = getRequiredExtensions();
    
#ifdef __APPLE__
    createInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    extensions.push_back("VK_KHR_portability_enumeration");
#endif
    createInfo.enabledExtensionCount = extensions.size();
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (enableValidationLayers) 
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();

        populateDebugMessengerCreateInfo(debugCreateInfo);
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
    }
    else 
    {
        createInfo.enabledLayerCount = 0;

        createInfo.pNext = nullptr;
    }

    return vkCreateInstance(&createInfo, nullptr, &instance);
}

VkResult LRenderer::createSurface()
{
   return glfwCreateWindowSurface(instance, window, nullptr, &surface);
}

VkImageView LRenderer::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32 mipLevels)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    HANDLE_VK_ERROR(vkCreateImageView(logicalDevice , &viewInfo, nullptr, &imageView))
    return imageView;
}

VkResult LRenderer::createPortalRenderTarget()
{
    auto portalRt = std::make_unique<RenderTarget>(logicalDevice, allocator);
    portalRt->images.resize(maxFramesInFlight);

    for (uint32 i = 0; i < maxFramesInFlight; ++i)
    {
        HANDLE_VK_ERROR(createImageInternal(swapChainExtent.width, swapChainExtent.height, swapChainImageFormat, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            portalRt->images[i].image, portalRt->images[i].allocation, 1))

        clearUndefinedImage(portalRt->images[i].image);

        portalRt->images[i].imageView = createImageView(portalRt->images[i].image, swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    }

    portalsRt.emplace_back(std::move(portalRt));
    return VK_SUCCESS;
}

VkResult LRenderer::createDescriptorSetLayout()
{
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorCount = texturesInitData.size();
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = { uboLayoutBinding, samplerLayoutBinding };
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    return vkCreateDescriptorSetLayout(logicalDevice, &layoutInfo, nullptr, &descriptorSetLayout);
}

VkResult LRenderer::createGraphicsPipeline(const GraphicsPipelineParams& params, VkPipeline& graphicsPipelineOut, VkRenderPass renderPass)
{
    VkShaderModule vertShaderModule = params.bInstanced? createShaderModule(genericInstancedVert) : createShaderModule(genericVert);
    VkShaderModule fragShaderModule = createShaderModule(genericFrag);
    
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    auto bindingDescription = LG::LGraphicsComponent::getBindingDescription();
    auto attributeDescriptions = LG::LGraphicsComponent::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32>(attributeDescriptions.size());
    
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = params.polygonMode;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f; // Optional
    rasterizer.depthBiasClamp = 0.0f; // Optional
    rasterizer.depthBiasSlopeFactor = 0.0f; // Optional

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 1.0f; // Optional
    multisampling.pSampleMask = nullptr; // Optional
    multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
    multisampling.alphaToOneEnable = VK_FALSE; // Optional

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    std::vector<VkDynamicState> dynamicStates =
    {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();
    

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;

    VkPushConstantRange pushConstantRange = {};
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (!pipelineLayout)
    {
        HANDLE_VK_ERROR(vkCreatePipelineLayout(logicalDevice, &pipelineLayoutInfo, nullptr, &pipelineLayout))
    }

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.minDepthBounds = 0.0f; // Optional
    depthStencil.maxDepthBounds = 1.0f; // Optional
    depthStencil.stencilTestEnable = VK_FALSE;
    depthStencil.front = {}; // Optional
    depthStencil.back = {}; // Optional

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pTessellationState = nullptr;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = nullptr; // Optional
    pipelineInfo.basePipelineIndex = -1;

    HANDLE_VK_ERROR(vkCreateGraphicsPipelines(logicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipelineOut))

    vkDestroyShaderModule(logicalDevice, fragShaderModule, nullptr);
    vkDestroyShaderModule(logicalDevice, vertShaderModule, nullptr);
    
    return VK_SUCCESS;
}

VkShaderModule LRenderer::createShaderModule(const std::vector<uint8_t>& code)
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32*>(code.data());
    
    VkShaderModule shaderModule;
    HANDLE_VK_ERROR(vkCreateShaderModule(logicalDevice, &createInfo, nullptr, &shaderModule))
    return shaderModule;
}

void LRenderer::createFramebuffers(RenderTarget* renderTarget, const VkExtent2D& size, uint32 framebuffersNum, VkRenderPass renderPass)
{ 
    renderTarget->depthImages.resize(framebuffersNum);

    VkFormat depthFormat = findDepthFormat();
    for (int32 i = 0; i < framebuffersNum; ++i)
    {
        createImageInternal(size.width, size.height, depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, renderTarget->depthImages[i].image, renderTarget->depthImages[i].allocation, 1);
        renderTarget->depthImages[i].imageView = createImageView(renderTarget->depthImages[i].image, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
    }

    renderTarget->framebuffers.resize(framebuffersNum);
    for (uint32 i = 0; i < framebuffersNum; ++i)
    {
        std::array<VkImageView, 2> attachments = 
        {
            renderTarget->images[i].imageView,
            renderTarget->depthImages[i].imageView
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = size.width;
        framebufferInfo.height = size.height;
        framebufferInfo.layers = 1;

        HANDLE_VK_ERROR(vkCreateFramebuffer(logicalDevice, &framebufferInfo, nullptr, &renderTarget->framebuffers[i]))
    }
}

VkResult LRenderer::createImage(const std::string& texturePath, Image& imageOut)
{
    stbi_set_flip_vertically_on_load(true);

    int texWidth, texHeight, texChannels;

    if (stbi_uc* pixels = stbi_load(texturePath.data(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha))
    {
        VkDeviceSize imageSize = texWidth * texHeight * 4;

        VkBuffer stagingBuffer;
        VmaAllocation stagingBufferMemory;

        void* data;
        // stage buffer to copy from CPU to GPU
        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO, stagingBuffer, stagingBufferMemory, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

        vmaMapMemory(allocator, stagingBufferMemory, &data);
        memcpy(data, pixels, static_cast<uint64>(imageSize));
        stbi_image_free(pixels);

        imageOut.mipLevels = static_cast<uint32>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

        HANDLE_VK_ERROR(createImageInternal(texWidth, texHeight, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            imageOut.image, imageOut.allocation, imageOut.mipLevels))

        transitionImageLayout(imageOut.image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, imageOut.mipLevels);
        copyBufferToImage(stagingBuffer, imageOut.image, static_cast<uint32>(texWidth), static_cast<uint32>(texHeight));

        // TODO: should be pregenerated
        generateMipmaps(imageOut.image, VK_FORMAT_R8G8B8A8_SRGB, texWidth, texHeight, imageOut.mipLevels);
        //transitionImageLayout(imageOut.image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, imageOut.mipLevels);

        vmaUnmapMemory(allocator, stagingBufferMemory);
        vmaDestroyBuffer(allocator, stagingBuffer, stagingBufferMemory);

        createTextureImageView(imageOut, imageOut.mipLevels);
        return VK_SUCCESS;
    }
    else
    {
        if (texturePath.find("portal") != 0)
        {
            RAISE_VK_ERROR("failed to load texture image!");
        }
    }
}

VkResult LRenderer::createImageInternal(uint32 width, uint32 height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VmaAllocation& imageMemory, uint32 mipLevels)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo createInfo{};
    createInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VmaAllocationInfo allocInfo{};
    return vmaCreateImage(allocator, &imageInfo, &createInfo, &image, &imageMemory, &allocInfo);
}

VkResult LRenderer::loadTextureImage(const std::string& texturePath)
{
    if (images.find(texturePath) == images.end())
    {
        Image imageToCreate{};
        HANDLE_VK_ERROR(createImage(texturePath, imageToCreate))
        images.emplace(texturePath, imageToCreate);

        VkSampler sampler;
        createTextureSampler(sampler, imageToCreate.mipLevels);
        textureSamplers[imageToCreate.mipLevels] = sampler;
    }
}

void LRenderer::clearUndefinedImage(VkImage imageToClear)
{
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = imageToClear;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    // 2. Clear the image with black color
    VkClearColorValue clearColor = {};
    clearColor.float32[0] = 0.0f;
    clearColor.float32[1] = 0.0f;
    clearColor.float32[2] = 0.0f;
    clearColor.float32[3] = 1.0f;

    vkCmdClearColorImage(
        commandBuffer,
        imageToClear,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        &clearColor,
        1,
        &barrier.subresourceRange
    );

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    endSingleTimeCommands(commandBuffer);
}

VkResult LRenderer::createTextureSampler(VkSampler& samplerOut, uint32 mipLevels)
{
    if (textureSamplers.find(mipLevels) == textureSamplers.end())
    {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = getMaxAnisotropy(physicalDevice);
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = mipLevels;
        return vkCreateSampler(logicalDevice, &samplerInfo, nullptr, &samplerOut);
    }
    return VK_SUCCESS;
}

void LRenderer::createTextureImageView(Image& imageInOut, uint32 mipLevels)
{
    imageInOut.imageView = createImageView(imageInOut.image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);
}

void LRenderer::initStaticDataTextures()
{
    for (const auto& [path,_] : texturesInitData)
    {
        // TODO: temporar check
        if (path.find("portal") != 0)
        {
            loadTextureImage(path);
        }
    }
}

void LRenderer::transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags aspect, uint32 mipLevels)
{
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) 
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) 
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) 
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    }
    else 
    {
        RAISE_VK_ERROR("unsupported layout transition!")
    }

    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    endSingleTimeCommands(commandBuffer);
}

void LRenderer::copyBufferToImage(VkBuffer buffer, VkImage image, uint32 width, uint32 height)
{
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = {
        width,
        height,
        1
    };

    vkCmdCopyBufferToImage(
        commandBuffer,
        buffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    endSingleTimeCommands(commandBuffer);
}

void LRenderer::generateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32 mipLevels)
{
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(physicalDevice, imageFormat, &formatProperties);

    if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) 
    {
        RAISE_VK_ERROR("texture image format does not support linear blitting!");
    }

    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    int32 mipWidth = texWidth;
    int32 mipHeight = texHeight;

    for (uint32 i = 1; i < mipLevels; i++) 
    {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        VkImageBlit blit{};
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(commandBuffer,
            image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit,
            VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        if (mipWidth > 1)
        {
            mipWidth /= 2;
        }
        if (mipHeight > 1)
        {
            mipHeight /= 2;
        }
    }

    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
        0, nullptr,
        0, nullptr,
        1, &barrier);

    endSingleTimeCommands(commandBuffer);
}

VkResult LRenderer::createCommandPool()
{
    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

    return vkCreateCommandPool(logicalDevice, &poolInfo, nullptr, &commandPool);
}

VkFormat LRenderer::findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features)
{
    for (VkFormat format : candidates) 
    {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);
        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) 
        {
            return format;
        }
        else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) 
        {
            return format;
        }
    }
    RAISE_VK_ERROR("failed to find supported format!")
}

VkFormat LRenderer::findDepthFormat()
{
    return findSupportedFormat(
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

VkCommandBuffer LRenderer::beginSingleTimeCommands()
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(logicalDevice, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

void LRenderer::endSingleTimeCommands(VkCommandBuffer commandBuffer)
{
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(logicalDevice, commandPool, 1, &commandBuffer);
}

void LRenderer::updateStorageBuffers(uint32 imageIndex)
{
    ZoneScoped;
    uint64 instancedArraysSize = staticInstancedPrimitiveMeshes.size();

    int32 instancedArrayNum = 0;
    for (const auto& [primitiveName, primitives] : staticInstancedPrimitiveMeshes)
    {
        // buffer array
        VkBuffer bufferToCopy = primitivesData[imageIndex * instancedArraysSize + instancedArrayNum].buffer;
        const auto& indices = primitiveDataIndices[primitiveName];
        bool bIsPortal = primitiveName == "LPortal";

#if _MSC_VER
        std::for_each(std::execution::par_unseq, indices.begin(), indices.end(), [&](uint32 i)
        {
            if (auto objectPtr = primitives[i].lock())
            {
                // TODO: temp debug
                auto str = objectPtr->getColorTexturePath();
                SSBOData data =
                {
                    .genericMatrix = objectPtr->getModelMatrix(),
                    .textureId = texturesInitData[objectPtr->getColorTexturePath()],
                    .isPortal = bIsPortal    
                };
                memcpy((uint8_t*)stagingBufferPtr + i * sizeof(SSBOData), &data, sizeof(SSBOData));
            }
            else
            {
                // Object is expired. Handle accordingly if needed.
            }
        });
#elif __APPLE__
        // Decide on the number of threads based on the number of available cores
        size_t numThreads = std::thread::hardware_concurrency();
        size_t chunkSize = indices.size() / numThreads;
        
        // Vector to store threads
        std::vector<std::thread> threads;

        // Launch threads
        for (size_t t = 0; t < numThreads; ++t) 
        {
            size_t startIdx = t * chunkSize;
            size_t endIdx = (t == numThreads - 1) ? indices.size() : startIdx + chunkSize;
            
            threads.emplace_back([this, &primitives, &indices, startIdx, endIdx]() 
                {
                    for (size_t i = startIdx; i < endIdx; ++i) {
                        if (auto objectPtr = primitives[i].lock()) 
                        {
                            SSBOData data
                            {
                                .genericMatrix = objectPtr->getModelMatrix(),
                                .textureId = texturesInitData[objectPtr->getColorTexturePath()],
                                .isPortal = bIsPortal
                            };
                            memcpy((uint8_t*)stagingBufferPtr + i * sizeof(SSBOData), &data, sizeof(SSBOData));
                        } 
                        else 
                        {
                            // Object is expired. Handle accordingly if needed.
                        }
                    }
                }
            );
        }

        // Join threads
        for (auto& thread : threads) 
        {
            thread.join();
        }
#endif

        ++instancedArrayNum;

        copyBuffer(stagingBuffer.buffer, bufferToCopy, primitives.size() * sizeof(PushConstants));
    }
}

//void LRenderer::setProjection(float degrees, float zNear, float zFar)
//{
//    glm::mat4 proj = glm::perspective(glm::radians(degrees), swapChainExtent.width / (float) swapChainExtent.height, zNear, zFar);
//    proj[1][1] *= -1;
//    projection = proj;
//    bNeedToUpdateProjView = true;
//}

void LRenderer::setProjection(float degrees, float width, float height, float zNear, float zFar)
{
    glm::mat4 proj = glm::perspective(glm::radians(degrees), width / height, zNear, zFar);
    proj[1][1] *= -1;
    projection = proj;
    bNeedToUpdateProjView = true;
}

void LRenderer::setView(const glm::mat4& viewIn)
{
    view = viewIn;
    bNeedToUpdateProjView = true;
}

glm::vec3 LRenderer::getCameraUp() const
{
    return cameraUp;
}

glm::vec3 LRenderer::getCameraFront() const
{
    return cameraFront;
}

glm::vec3 LRenderer::getCameraPosition() const
{
    return cameraPosition;
}

void LRenderer::setCameraFront(const glm::vec3& cameraFront)
{
    this->cameraFront = cameraFront;
    setView(glm::lookAt(cameraPosition, cameraPosition + cameraFront, cameraUp));
}

void LRenderer::setCameraPosition(const glm::vec3& cameraPosition)
{
    this->cameraPosition = cameraPosition;
    setView(glm::lookAt(cameraPosition, cameraPosition + cameraFront, cameraUp));
}

void LRenderer::initProjection()
{
    setProjection(degrees, swapChainExtent.width, swapChainExtent.height);
}

void LRenderer::initView()
{
    glm::mat4 viewMatrix = glm::lookAt(cameraPosition, cameraPosition + cameraFront, cameraUp);
    setView(viewMatrix);
}

void LRenderer::updateProjView()
{
    projView = projection * view;
    bNeedToUpdateProjView = false;
}

void LRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage properties,
                             VkBuffer& buffer, VmaAllocation& bufferMemory, uint32 vmaFlags)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = properties;
    allocInfo.flags = vmaFlags;

    HANDLE_VK_ERROR(vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer, &bufferMemory, nullptr))
}

void LRenderer::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
{
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
    endSingleTimeCommands(commandBuffer);
}

void LRenderer::createInstancesStorageBuffers()
{
     VkDeviceSize globalBufferSize = findProperStageBufferSize();

     if (globalBufferSize == 0)
     {
         return;
     }

     // stage buffer to copy from CPU to GPU
     createBuffer(globalBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO, stagingBuffer.buffer, stagingBuffer.memory, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
     vmaMapMemory(allocator, stagingBuffer.memory, &stagingBufferPtr);

     uint64 instancedArraysSize = primitiveCounterInitData.size();

     primitivesData.resize(instancedArraysSize * maxFramesInFlight);

     for (uint32 i = 0; i < maxFramesInFlight; ++i)
     {
         int32 instancedArrayNum = 0;
         for (const auto& [_, primitivesNum] : primitiveCounterInitData)
         {
             int32 index = i * instancedArraysSize + instancedArrayNum;
             auto bufferSize = sizeof(PushConstants) * primitivesNum;
             createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, primitivesData[index].buffer, primitivesData[index].memory);
             ++instancedArrayNum;
         }
     }

     for (const auto& [primitiveName, primitivesNum] : primitiveCounterInitData)
     {
         std::vector<uint32> indices(primitivesNum);
         std::iota(indices.begin(), indices.end(), 0);
         primitiveDataIndices.emplace(std::make_pair(primitiveName, indices));
     }
}

uint32 LRenderer::findProperStageBufferSize() const
{
    uint32 maxprimitiveCounterInitData = 0;
    for (const auto& [_, counter] : primitiveCounterInitData)
    {
        uint32 primitivesNum = counter;
        if (primitivesNum > maxprimitiveCounterInitData)
        {
            maxprimitiveCounterInitData = primitivesNum;
        }
    }
    return maxprimitiveCounterInitData * sizeof(PushConstants);
}

void LRenderer::vmaMapWrap(VmaAllocator allocator, VmaAllocation* memory, void*& mappedData)
{
    HANDLE_VK_ERROR(vmaMapMemory(allocator, *memory, &mappedData))
}

void LRenderer::vmaUnmapWrap(VmaAllocator allocator, VmaAllocation* memory)
{
    vmaUnmapMemory(allocator, *memory);
}

void LRenderer::vmaDestroyBufferWrap(VmaAllocator allocator, VkBuffer& buffer, VmaAllocation* memory)
{
    vmaDestroyBuffer(allocator, buffer, *memory);
}

VkResult LRenderer::createCommandBuffers()
{
    commandBuffers.resize(maxFramesInFlight);
    
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32>(commandBuffers.size());

    return vkAllocateCommandBuffers(logicalDevice, &allocInfo, commandBuffers.data());
}

uint32 LRenderer::findMemoryType(uint32 typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32 i = 0; i < memProperties.memoryTypeCount; ++i)
    {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }

    RAISE_VK_ERROR("Failed to find suitable memory type!")
}

VkResult LRenderer::createDescriptorPool()
{
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32>(maxFramesInFlight) * primitiveCounterInitData.size();
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = static_cast<uint32>(maxFramesInFlight) * primitiveCounterInitData.size() * texturesInitData.size();

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32>(maxFramesInFlight) * primitiveCounterInitData.size();

    return vkCreateDescriptorPool(logicalDevice, &poolInfo, nullptr, &descriptorPool);
}

VkResult LRenderer::createDescriptorSets()
{
     std::vector<VkDescriptorSetLayout> layouts(static_cast<uint32>(maxFramesInFlight) * primitiveCounterInitData.size(), descriptorSetLayout);
     VkDescriptorSetAllocateInfo allocInfo{};
     allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
     allocInfo.descriptorPool = descriptorPool;
     allocInfo.descriptorSetCount = static_cast<uint32>(maxFramesInFlight) * primitiveCounterInitData.size();
     allocInfo.pSetLayouts = layouts.data();
    
     descriptorSets.resize(allocInfo.descriptorSetCount);
     HANDLE_VK_ERROR(vkAllocateDescriptorSets(logicalDevice, &allocInfo, descriptorSets.data()))
         
     for (uint32 i = 0; i < maxFramesInFlight; ++i)
     {
         uint32 instancedArrayNum = 0;
         for (auto it = primitiveCounterInitData.begin(); it != primitiveCounterInitData.end(); ++it)
         {
             auto& [_, primitivesNum] = *it;

             VkDescriptorBufferInfo bufferInfo{};
             bufferInfo.buffer = primitivesData[i * primitiveCounterInitData.size() + instancedArrayNum].buffer;
             bufferInfo.offset = 0;
             bufferInfo.range = sizeof(PushConstants) * primitivesNum;

             std::vector<VkDescriptorImageInfo> imageDescriptors;
             imageDescriptors.resize(texturesInitData.size());

             for (auto& [path, image] : images)
             {
                 VkDescriptorImageInfo imageInfo{};
                 imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                 imageInfo.imageView = image.imageView;
                 imageInfo.sampler = textureSamplers[image.mipLevels];

                 uint32 textureIndex = texturesInitData[path];
                 imageDescriptors[textureIndex] = imageInfo;
             }

             for (uint32 j = 0; j < maxPortalNum; ++j)
             {
                 VkDescriptorImageInfo imageInfo{};
                 imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                 imageInfo.imageView = portalsRt[j]->images[i].imageView;
                 imageInfo.sampler = portalSampler;

                 uint32 textureIndex = texturesInitData[std::format("portal{}", j + 1)];
                 imageDescriptors[textureIndex] = imageInfo;
             }

             std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

             descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
             descriptorWrites[0].dstSet = descriptorSets[i * primitiveCounterInitData.size() + instancedArrayNum];
             descriptorWrites[0].dstBinding = 0;
             descriptorWrites[0].dstArrayElement = 0;
             descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
             descriptorWrites[0].descriptorCount = 1;
             descriptorWrites[0].pBufferInfo = &bufferInfo;

             descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
             descriptorWrites[1].dstSet = descriptorSets[i * primitiveCounterInitData.size() + instancedArrayNum];
             descriptorWrites[1].dstBinding = 1;
             descriptorWrites[1].dstArrayElement = 0;
             descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
             descriptorWrites[1].descriptorCount = imageDescriptors.size();
             descriptorWrites[1].pImageInfo = imageDescriptors.data();

             vkUpdateDescriptorSets(logicalDevice, static_cast<uint32>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
             ++instancedArrayNum;
         }
     }

     return VK_SUCCESS;
}

VkResult LRenderer::createSyncObjects()
{
    imageAvailableSemaphores.resize(maxFramesInFlight);
    renderFinishedSemaphores.resize(maxFramesInFlight);
    inFlightFences.resize(maxFramesInFlight);
    
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32 i = 0; i < maxFramesInFlight; ++i)
    {
        HANDLE_VK_ERROR(vkCreateSemaphore(logicalDevice, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]))
        HANDLE_VK_ERROR(vkCreateSemaphore(logicalDevice, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]))
        HANDLE_VK_ERROR(vkCreateFence(logicalDevice, &fenceInfo, nullptr, &inFlightFences[i]))
    }

    return VK_SUCCESS;
}

void LRenderer::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32 imageIndex)
{
    ZoneScoped;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0; // Optional
    beginInfo.pInheritanceInfo = nullptr; // Optional

    HANDLE_VK_ERROR(vkBeginCommandBuffer(commandBuffer, &beginInfo))

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapChainExtent.width);
    viewport.height = static_cast<float>(swapChainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = swapChainExtent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    if (!updatedStorageBuffer[currentFrame])
    {
        updateStorageBuffers(currentFrame);
        updatedStorageBuffer[currentFrame] = true;
    }

    //glm::mat4 savedView = getView();

    // TODO: Ideally these pass calls should be incapsulated inside RenderPass->render(), but there is some work to do...
    {
        ZoneScopedN("Portal passes");

        // TODO: temp check
        //static uint32 shit = 0;
        //if (shit < portalsRt.size() * maxFramesInFlight/*needPortalRecalculation()*/)
        {
            storedView = view;
            storedProj = projection;
            for (uint32 i = 0; i < portalsRt.size(); ++i)
            {
                // TODO: I'm not sure if we can use currentFrame here
                doPortalPass(commandBuffer, portalsRt[0]->framebuffers[currentFrame], portalPasses[0], 0, 1);
                doPortalPass(commandBuffer, portalsRt[1]->framebuffers[currentFrame], portalPasses[1], 1, 0);
                //++shit;
            }
        }
    }

    {
        ZoneScopedN("Main pass");
        view = storedView;
        projection = storedProj;
        updateProjView();
        doMainPass(commandBuffer, swapChainRt->framebuffers[imageIndex]);
    }

    HANDLE_VK_ERROR(vkEndCommandBuffer(commandBuffer))
}

void LRenderer::recreateSwapChain()
{
    int32 width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    
    while (width == 0 || height == 0)
    {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }
    
    vkDeviceWaitIdle(logicalDevice);

    swapChainRt->clear();
    vkDestroySwapchainKHR(logicalDevice, swapChain, nullptr);
    createSwapChain();
    createFramebuffers(swapChainRt.get(), swapChainExtent, swapChainSize, mainPass->getRenderPass());

    for (uint32 i = 0; i < portalsRt.size(); ++i)
    {
        auto& portalRt = portalsRt[i];

        portalRt->clear();
        createFramebuffers(portalRt.get(), swapChainExtent, portalSize, portalPasses[i]->getRenderPass());
    }

    initProjection();
}

void LRenderer::drawFrame(float delta)
{
    this->delta = delta;

    uint32 imageIndex;
    {
        ZoneScopedNC("Render call", 0xFFFF0000);
        vkWaitForFences(logicalDevice, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
    }

    VkResult result;
    {
        ZoneScopedN("Acquire next image");
        result = vkAcquireNextImageKHR(logicalDevice, swapChain, UINT64_MAX, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);
    }

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        recreateSwapChain();
        return;
    }
    else if (result != VK_SUCCESS)
    {
        RAISE_VK_ERROR(result)
    }
    
    {
        ZoneScopedN("Rerecording command buffer");
        vkResetFences(logicalDevice, 1, &inFlightFences[currentFrame]);
        vkResetCommandBuffer(commandBuffers[currentFrame], 0);
        recordCommandBuffer(commandBuffers[currentFrame], imageIndex);
    }
    
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[currentFrame];
    
    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;
    
    HANDLE_VK_ERROR(vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]))
    
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    
    VkSwapchainKHR swapChains[] = {swapChain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;
    presentInfo.pResults = nullptr; // Optional

    {
        ZoneScopedNC("Present KHR", 0xFFFF0000);
        vkQueuePresentKHR(presentQueue, &presentInfo);
    }

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || bFramebufferResized)
    {
        bFramebufferResized = false;
        recreateSwapChain();
    }
    else if (result != VK_SUCCESS)
    {
        RAISE_VK_ERROR(result)
    }

    currentFrame = (currentFrame + 1) % maxFramesInFlight;
}

void LRenderer::exit()
{
    vkDeviceWaitIdle(logicalDevice);
}

//void LRenderer::updatePushConstants(const LG::LGraphicsComponent& mesh)
//{
//    glm::mat4 model = mesh.getModelMatrix();
//    pushConstants.genericMatrix = projView * model;
//}

VkResult LRenderer::pickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0)
    {
        return VK_ERROR_DEVICE_LOST;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    for (const auto& device : devices)
    {
        if (isDeviceSuitable(device))
        {
            physicalDevice = device;
            break;
        }
    }

    if (physicalDevice == VK_NULL_HANDLE)
    {
        LLogger::LogString("Failed to find a suitable GPU!", true);
    }
    
    return VK_SUCCESS;
}

VkResult LRenderer::createLogicalDevice()
{
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32> uniqueQueueFamilies = {indices.graphicsFamily.value(), indices.presentFamily.value()};
    
    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies)
    {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    if (enableValidationLayers)
    {
        createInfo.enabledLayerCount = static_cast<uint32>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
    }
    else
    {
        createInfo.enabledLayerCount = 0;
    }

    HANDLE_VK_ERROR(vkCreateDevice(physicalDevice, &createInfo, nullptr, &logicalDevice))
    
    vkGetDeviceQueue(logicalDevice, indices.graphicsFamily.value(), 0, &graphicsQueue);
    vkGetDeviceQueue(logicalDevice, indices.presentFamily.value(), 0, &presentQueue);
    
    return VK_SUCCESS;
}

VkResult LRenderer::createAllocator()
{
    VmaVulkanFunctions vulkanFunctions = {};
    vulkanFunctions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocatorCreateInfo = {};
    allocatorCreateInfo.flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    allocatorCreateInfo.vulkanApiVersion = VK_MAKE_API_VERSION(0, majorApiVersion, minorApiVersion, 0);
    allocatorCreateInfo.physicalDevice = physicalDevice;
    allocatorCreateInfo.device = logicalDevice;
    allocatorCreateInfo.instance = instance;
    allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;

    return vmaCreateAllocator(&allocatorCreateInfo, &allocator);
}

bool LRenderer::isDeviceSuitable(VkPhysicalDevice device) const
{
    QueueFamilyIndices indices = findQueueFamilies(device);
    bool extensionsSupported = checkDeviceExtensionSupport(device);
    
    bool swapChainAdequate = false;
    if (extensionsSupported)
    {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    VkPhysicalDeviceFeatures supportedFeatures;
    vkGetPhysicalDeviceFeatures(device, &supportedFeatures);
    
    return extensionsSupported && swapChainAdequate && indices.isValid() && supportedFeatures.samplerAnisotropy;
}

bool LRenderer::checkDeviceExtensionSupport(VkPhysicalDevice device) const
{
    uint32 extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

    for (const auto& extension : availableExtensions)
    {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

LRenderer::QueueFamilyIndices LRenderer::findQueueFamilies(VkPhysicalDevice device) const
{
    QueueFamilyIndices indices;

    uint32 queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());
    
     
    for (uint32 i = 0; i < queueFamilies.size(); ++i)
    {
        const auto& queueFamily = queueFamilies[i];
        
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

        if (presentSupport)
        {
            indices.presentFamily = i;
        }
        
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            indices.graphicsFamily = i;
        }

        if (indices.isValid())
        {
            break;
        }
    }
 
    return indices;
}

VkResult LRenderer::createSwapChain()
{
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    
    swapChainExtent = chooseSwapExtent(swapChainSupport.capabilities);
    swapChainImageFormat = surfaceFormat.format;

    swapChainSize = swapChainSupport.capabilities.minImageCount + 1;

    if (swapChainSupport.capabilities.maxImageCount > 0 && swapChainSize > swapChainSupport.capabilities.maxImageCount)
    {
        swapChainSize = swapChainSupport.capabilities.maxImageCount;
    }
    
    VkSwapchainCreateInfoKHR createInfo{};
    
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = swapChainSize;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = swapChainExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};
    
    if (indices.graphicsFamily != indices.presentFamily)
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }
    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (!swapChainRt)
    {
        swapChainRt = std::make_unique<RenderTarget>(logicalDevice, allocator);
    }

    if (vkCreateSwapchainKHR(logicalDevice, &createInfo, nullptr, &swapChain) == VK_SUCCESS)
    {
        vkGetSwapchainImagesKHR(logicalDevice, swapChain, &swapChainSize, nullptr);

        std::vector<VkImage> images;
        images.resize(swapChainSize);
        vkGetSwapchainImagesKHR(logicalDevice, swapChain, &swapChainSize, images.data());

        swapChainRt->images.resize(swapChainSize);
        for (uint32 i = 0; i < swapChainSize; ++i)
        {
            swapChainRt->images[i].image = images[i];
        }
    }
    else
    {
        return VK_ERROR_SURFACE_LOST_KHR;
    }

    for (uint32 i = 0; i < swapChainRt->images.size(); ++i)
    {
        swapChainRt->images[i].imageView = createImageView(swapChainRt->images[i].image, swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    }

    return VK_SUCCESS;
}

LRenderer::SwapChainSupportDetails LRenderer::querySwapChainSupport(VkPhysicalDevice device) const
{
    SwapChainSupportDetails details;
	
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32 formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

    if (formatCount != 0)
    {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32 presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

    if (presentModeCount != 0)
    {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }
	
    return details;
}

VkSurfaceFormatKHR LRenderer::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) const
{
    for (const auto& availableFormat : availableFormats)
    {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return availableFormat;
        }
    }
    return availableFormats[0];
}

VkPresentModeKHR LRenderer::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) const
{
    if (!specs.bVsync)
    {
        return VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
    
    for (const auto& availablePresentMode : availablePresentModes)
    {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            return availablePresentMode;
        }
    }
    
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D LRenderer::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32>::max())
    {
        return capabilities.currentExtent;
    }
    
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    VkExtent2D actualExtent =
    {
        static_cast<uint32>(width),
        static_cast<uint32>(height)
    };

    actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    return actualExtent;
}

uint32 LRenderer::getPushConstantSize(VkPhysicalDevice physicalDeviceIn) const
{
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(physicalDeviceIn, &deviceProperties);
    return deviceProperties.limits.maxPushConstantsSize;
}

uint32 LRenderer::getMaxAnisotropy(VkPhysicalDevice physicalDeviceIn) const
{
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(physicalDeviceIn, &deviceProperties);
    return deviceProperties.limits.maxSamplerAnisotropy;
}

void LRenderer::addPrimitive(std::weak_ptr<LG::LGraphicsComponent> ptr)
{
    if (auto sharedPtr = ptr.lock())
    {
        const auto& typeName = sharedPtr->getTypeName();

        if (LG::isPortal(sharedPtr.get()))
        {
            portals.emplace_back(std::reinterpret_pointer_cast<LG::LPortal>(sharedPtr));
            auto& staticInstancesArray = staticInstancedPrimitiveMeshes[typeName];
            staticInstancesArray.emplace_back(ptr);
        }
        else if (isEnoughStaticInstanceSpace(typeName) && LG::isInstancePrimitive(sharedPtr.get()))
        {
            auto& staticInstancesArray = staticInstancedPrimitiveMeshes[typeName];
            staticInstancesArray.emplace_back(ptr);
        }
        else
        {
            primitiveMeshes.push_back(ptr);
        }
    }
}

DEBUG_CODE(void LRenderer::addDebugPrimitive(std::weak_ptr<LG::LGraphicsComponent> ptr)
{
    debugMeshes.push_back(ptr);
})

bool LRenderer::needPortalRecalculation() const
{
    if (maxPortalNum == 0)
    {
        return false;
    }
    for (uint32 i = 0; i < portals.size(); ++i)
    {
        if (std::shared_ptr<LG::LPortal> portalPtr = portals[i].lock())
        {
            if (portalPtr->needsRecalculation())
            {
                return true;
            }
        }
        else
        {
            // TODO: should be erased
        }
    }
    return false;
}

void LRenderer::framebufferResizeCallback(GLFWwindow* window, int32 width, int32 height)
{
    LRenderer::bFramebufferResized = true;
}

LRenderer::RenderTarget::~RenderTarget()
{
    clear(true);
}

void LRenderer::RenderTarget::clear(bool bClearImages)
{
    for (uint32 i = 0; i < framebuffers.size(); ++i)
    {
        vkDestroyFramebuffer(logicalDevice, framebuffers[i], nullptr);
    }

    for (int32 i = 0; i < images.size(); ++i)
    {
        vkDestroyImageView(logicalDevice, images[i].imageView, nullptr);
        vkDestroyImageView(logicalDevice, depthImages[i].imageView, nullptr);
        vkDestroyImage(logicalDevice, depthImages[i].image, nullptr);
        vmaFreeMemory(allocator, depthImages[i].allocation);

        if (bClearImages)
        {
            vkDestroyImage(logicalDevice, images[i].image, nullptr);
            if (images[i].allocation)
            {
                vmaFreeMemory(allocator, images[i].allocation);
            }
        }
    }

    images.clear();
    depthImages.clear();
    framebuffers.clear();
}

LRenderer::RenderPass::RenderPass(VkDevice logicalDevice, VkFormat colorFormat, VkFormat depthFormat, bool bToPresent)
    :logicalDevice(logicalDevice), bToPresent(bToPresent)
{
    init(colorFormat, depthFormat);
}

LRenderer::RenderPass::~RenderPass()
{
    clear();
}

void LRenderer::RenderPass::init(VkFormat colorFormat, VkFormat depthFormat)
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = colorFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkSubpassDependency depthDependency{};
    depthDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    depthDependency.dstSubpass = 0;
    depthDependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    depthDependency.srcAccessMask = 0;
    depthDependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    depthDependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkSubpassDependency, 2> dependencies = { dependency, depthDependency };
    std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    HANDLE_VK_ERROR(vkCreateRenderPass(logicalDevice, &renderPassInfo, nullptr, &renderPass))
}

void LRenderer::RenderPass::beginPass(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, const VkExtent2D& size)
{
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = size;

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = { {0.0f, 0.0f, 0.0f, 1.0f} };
    clearValues[1].depthStencil = { 1.0f, 0 };

    renderPassInfo.clearValueCount = static_cast<uint32>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void LRenderer::RenderPass::endPass(VkCommandBuffer commandBuffer)
{
    vkCmdEndRenderPass(commandBuffer);
}

void LRenderer::RenderPass::clear()
{
    vkDestroyRenderPass(logicalDevice, renderPass, nullptr);
}
