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

#define TRACY_ENABLE
#include <tracy/Tracy.hpp>

#include "LWindow.h"
#include "vulkan/vulkan.h"

#include "Util.h"

#include "gen_shaders.cxx"

DEBUG_CODE(
    bool RenderComponentBuilder::bIsConstructing = false;
)

LRenderer* LRenderer::thisPtr = nullptr;
bool LRenderer::bFramebufferResized = false;
std::unordered_map<std::string, int32> RenderComponentBuilder::objectsCounter;
std::unordered_map<std::string, LRenderer::VkMemoryBuffer> RenderComponentBuilder::memoryBuffers;

LRenderer::LRenderer(const std::unique_ptr<LWindow>& window, std::map<std::string, uint32> primitiveCounter)
    :primitiveCounter(primitiveCounter)
{
    if (thisPtr)
    {
        RAISE_VK_ERROR("LRenderer is a singleton object, can't create more than 1")
    }
    
    thisPtr = this;
    
    this->window = window.get()->getWindow();
    specs = window.get()->getWindowSpecs();
    
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
    
    HANDLE_VK_ERROR(createImageViews())
    HANDLE_VK_ERROR(createRenderPass())
    HANDLE_VK_ERROR(createDescriptorSetLayout())

    GraphicsPipelineParams mainPipelineParams;
    mainPipelineParams.bInstanced = true;
    mainPipelineParams.polygonMode = VkPolygonMode::VK_POLYGON_MODE_FILL;
    HANDLE_VK_ERROR(createGraphicsPipeline(mainPipelineParams, graphicsPipelineInstanced))

    mainPipelineParams.bInstanced = false;
    HANDLE_VK_ERROR(createGraphicsPipeline(mainPipelineParams, graphicsPipelineRegular))

//DEBUG_CODE(
//    GraphicsPipelineParams debugPipelineParams;
//    debugPipelineParams.polygonMode = VkPolygonMode::VK_POLYGON_MODE_LINE;
//    HANDLE_VK_ERROR(createGraphicsPipeline(debugPipelineParams, debugGraphicsPipeline))
//)

    HANDLE_VK_ERROR(createFramebuffers())
    HANDLE_VK_ERROR(createCommandPool())

    createInstancesStorageBuffers();

    HANDLE_VK_ERROR(createDescriptorPool())
    HANDLE_VK_ERROR(createDescriptorSets())
    HANDLE_VK_ERROR(createCommandBuffers())
    HANDLE_VK_ERROR(createSyncObjects())
}

void LRenderer::cleanup()
{
    cleanupSwapChain();

    vkDestroyDescriptorPool(logicalDevice, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(logicalDevice, descriptorSetLayout, nullptr);

//DEBUG_CODE(
//    vkDestroyPipeline(logicalDevice, debugGraphicsPipeline, nullptr);
//)

    vkDestroyPipeline(logicalDevice, graphicsPipelineInstanced, nullptr);
    vkDestroyPipeline(logicalDevice, graphicsPipelineRegular, nullptr);
    vkDestroyPipelineLayout(logicalDevice, pipelineLayout, nullptr);
    vkDestroyRenderPass(logicalDevice, renderPass, nullptr);
    
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
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

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
    createInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    createInfo.pApplicationInfo = &appInfo;
    
    // required extension request
    uint32_t glfwExtensionCount = 0;
    auto extensions = getRequiredExtensions();
    
#ifdef __APPLE__
    std::vector<const char*> instanceExtensions(glfwExtensionCount);
    memcpy(instanceExtensions.data(), glfwExtensions, sizeof(const char*) * glfwExtensionCount);
    instanceExtensions.push_back("VK_KHR_portability_enumeration");
    createInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
    createInfo.ppEnabledExtensionNames = instanceExtensions.data();
#else
    createInfo.enabledExtensionCount = extensions.size();
    createInfo.ppEnabledExtensionNames = extensions.data();
#endif

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

VkResult LRenderer::createImageViews()
{
    swapChainImageViews.resize(swapChainImages.size());
    VkResult res = VK_INCOMPLETE;
    for (uint32 i = 0; i < swapChainImages.size(); i++)
    {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapChainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapChainImageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;
        res = vkCreateImageView(logicalDevice, &createInfo, nullptr, &swapChainImageViews[i]);
    }
    return res;
}

VkResult LRenderer::createRenderPass()
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapChainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.pDependencies = nullptr;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    return vkCreateRenderPass(logicalDevice, &renderPassInfo, nullptr, &renderPass);
}

VkResult LRenderer::createDescriptorSetLayout()
{
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr; // Optional

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;

    return vkCreateDescriptorSetLayout(logicalDevice, &layoutInfo, nullptr, &descriptorSetLayout);
}

VkResult LRenderer::createGraphicsPipeline(const GraphicsPipelineParams& params, VkPipeline& graphicsPipelineOut)
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
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();;

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
    
    VkPushConstantRange pushConstantRange = {};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1; // Optional
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (!pipelineLayout)
    {
        HANDLE_VK_ERROR(vkCreatePipelineLayout(logicalDevice, &pipelineLayoutInfo, nullptr, &pipelineLayout))
    }

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
    pipelineInfo.pDepthStencilState = nullptr; // Optional
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

VkResult LRenderer::createFramebuffers()
{
    swapChainFramebuffers.resize(swapChainImageViews.size());
    for (uint32 i = 0; i < swapChainImageViews.size(); ++i)
    {
        VkImageView attachments[] =
        {
            swapChainImageViews[i]
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = swapChainExtent.width;
        framebufferInfo.height = swapChainExtent.height;
        framebufferInfo.layers = 1;

        HANDLE_VK_ERROR(vkCreateFramebuffer(logicalDevice, &framebufferInfo, nullptr, &swapChainFramebuffers[i]))
    }
    return VK_SUCCESS;
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

void LRenderer::updateUniformBuffers(uint32 imageIndex)
{
    ZoneScoped;
    uint64 instancedArraysSize = instancedPrimitiveMeshes.size();

    int32 instancedArrayNum = 0;
    for (const auto& [_, primitives] : instancedPrimitiveMeshes)
    {
        int32 primitiveNum = 0;

        // buffer array
        VkBuffer bufferToCopy = primitivesData[imageIndex * instancedArraysSize + instancedArrayNum].buffer;

        std::vector<size_t> indices(primitives.size());
        std::iota(indices.begin(), indices.end(), 0);

        #if defined(_MSC_VER)
        std::for_each(std::execution::par, indices.begin(), indices.end(), [&](size_t i) {
        if (auto objectPtr = primitives[i].lock())
        {
            PushConstants data { projView * objectPtr->getModelMatrix() };
            memcpy((uint8_t*)stagingBufferPtr + i * sizeof(PushConstants), &data, sizeof(PushConstants));
        }
        else
        {
                // Object is expired. Handle accordingly if needed.
        }
        });

        #else
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
                            PushConstants data { projView * objectPtr->getModelMatrix() };
                            memcpy((uint8_t*)stagingBufferPtr + i * sizeof(PushConstants), &data, sizeof(PushConstants));
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

void LRenderer::setProjection(float degrees, float zNear, float zFar)
{
    glm::mat4 proj = glm::perspective(glm::radians(degrees), swapChainExtent.width / (float) swapChainExtent.height, zNear, zFar);
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
    setProjection(degrees, zNear, zFar);
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

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0; // Optional
    copyRegion.dstOffset = 0; // Optional
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
    vkEndCommandBuffer(commandBuffer);
    
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(logicalDevice, commandPool, 1, &commandBuffer);
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

     uint64 instancedArraysSize = primitiveCounter.size();

     primitivesData.resize(instancedArraysSize * maxFramesInFlight);

     for (uint32 i = 0; i < maxFramesInFlight; ++i)
     {
         int32 instancedArrayNum = 0;
         for (const auto& [_, primitivesNum] : primitiveCounter)
         {
             auto bufferSize = sizeof(PushConstants) * primitivesNum;
             createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, primitivesData[i * instancedArraysSize + instancedArrayNum].buffer, primitivesData[i].memory);
             ++instancedArrayNum;
         }
     }
}

uint32 LRenderer::findProperStageBufferSize() const
{
    uint32 maxPrimitiveCounter = 0;
    for (const auto& [_, counter] : primitiveCounter)
    {
        uint32 primitivesNum = counter;
        if (primitivesNum > maxPrimitiveCounter)
        {
            maxPrimitiveCounter = primitivesNum;
        }
    }
    return maxPrimitiveCounter * sizeof(PushConstants);;
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
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = static_cast<uint32>(maxFramesInFlight);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = static_cast<uint32_t>(maxFramesInFlight) * primitiveCounter.size();

    return vkCreateDescriptorPool(logicalDevice, &poolInfo, nullptr, &descriptorPool);
}

VkResult LRenderer::createDescriptorSets()
{
     std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, descriptorSetLayout);
     VkDescriptorSetAllocateInfo allocInfo{};
     allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
     allocInfo.descriptorPool = descriptorPool;
     allocInfo.descriptorSetCount = static_cast<uint32>(maxFramesInFlight) * primitiveCounter.size();
     allocInfo.pSetLayouts = layouts.data();
    
     descriptorSets.resize(allocInfo.descriptorSetCount);
     HANDLE_VK_ERROR(vkAllocateDescriptorSets(logicalDevice, &allocInfo, descriptorSets.data()))
         
     for (uint32 i = 0; i < maxFramesInFlight; ++i)
     {
         uint32 instancedArrayNum = 0;
         for (auto it = primitiveCounter.begin(); it != primitiveCounter.end(); ++it)
         {
             auto& [_, primitivesNum] = *it;

             VkDescriptorBufferInfo bufferInfo{};
             bufferInfo.buffer = primitivesData[i].buffer;
             bufferInfo.offset = 0;
             bufferInfo.range = sizeof(PushConstants) * primitivesNum;

             VkWriteDescriptorSet descriptorWrite{};
             descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
             descriptorWrite.dstSet = descriptorSets[i * primitiveCounter.size() + instancedArrayNum];
             descriptorWrite.dstBinding = 0;
             descriptorWrite.dstArrayElement = 0;
             descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
             descriptorWrite.descriptorCount = 1;
             descriptorWrite.pBufferInfo = &bufferInfo;

             vkUpdateDescriptorSets(logicalDevice, 1, &descriptorWrite, 0, nullptr);
         }
         ++instancedArrayNum;
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

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapChainExtent;
    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;
    
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

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
    
    auto drawInstancedMeshes = [this, commandBuffer]()
        {
            uint32 instanceArrayNum = 0;
            for (const auto& [typeName, primitives] : instancedPrimitiveMeshes)
            {
                const auto& memoryBuffer = RenderComponentBuilder::getMemoryBuffer(typeName);
                VkBuffer vertexBuffers[] = { memoryBuffer.vertexBuffer };
                VkDeviceSize offsets[] = { 0 };

                auto indicesCount = primitives[0].lock()->getIndexBuffer().size();
                auto instancesCount = primitives.size();

                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[currentFrame * instancedPrimitiveMeshes.size() + instanceArrayNum], 0, nullptr);

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

                    updatePushConstants(mesh);
                    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pushConstants);

                    const auto& memoryBuffer = RenderComponentBuilder::getMemoryBuffer(mesh.getTypeName());
                    VkBuffer vertexBuffers[] = { memoryBuffer.vertexBuffer };
                    VkDeviceSize offsets[] = { 0 };

                    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
                    vkCmdBindIndexBuffer(commandBuffer, memoryBuffer.indexBuffer, 0, VK_INDEX_TYPE_UINT16);
                    vkCmdDrawIndexed(commandBuffer, mesh.indicesCount, 1, 0, 0, 0);
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
        drawInstancedMeshes();
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
  
    vkCmdEndRenderPass(commandBuffer);
    HANDLE_VK_ERROR(vkEndCommandBuffer(commandBuffer))
}

void LRenderer::cleanupSwapChain()
{
    for (uint32 i = 0; i < swapChainFramebuffers.size(); ++i)
    {
        vkDestroyFramebuffer(logicalDevice, swapChainFramebuffers[i], nullptr);
    }

    for (uint32 i = 0; i < swapChainImageViews.size(); ++i)
    {
        vkDestroyImageView(logicalDevice, swapChainImageViews[i], nullptr);
    }

    vkDestroySwapchainKHR(logicalDevice, swapChain, nullptr);
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

    cleanupSwapChain();
    
    createSwapChain();
    createImageViews();
    createFramebuffers();

    initProjection();
}

void LRenderer::drawFrame()
{
    vkWaitForFences(logicalDevice, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
    uint32 imageIndex;

    VkResult result = vkAcquireNextImageKHR(logicalDevice, swapChain, UINT64_MAX, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        recreateSwapChain();
        return;
    }
    else if (result != VK_SUCCESS)
    {
        RAISE_VK_ERROR(result)
    }

    updateUniformBuffers(currentFrame);
    
    vkResetFences(logicalDevice, 1, &inFlightFences[currentFrame]);
    
    vkResetCommandBuffer(commandBuffers[currentFrame], 0);
    recordCommandBuffer(commandBuffers[currentFrame], imageIndex);
    
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

    vkQueuePresentKHR(presentQueue, &presentInfo);

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

void LRenderer::updatePushConstants(const LG::LGraphicsComponent& mesh)
{
    glm::mat4 model = mesh.getModelMatrix();
    pushConstants.mvpMatrix = projView * model;
}

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
    
    return extensionsSupported && swapChainAdequate && indices.isValid();
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

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;

    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount)
    {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }
    
    VkSwapchainCreateInfoKHR createInfo{};
    
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
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

    VkResult res = vkCreateSwapchainKHR(logicalDevice, &createInfo, nullptr, &swapChain);
    if (res == VK_SUCCESS)
    {
        vkGetSwapchainImagesKHR(logicalDevice, swapChain, &imageCount, nullptr);
        swapChainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(logicalDevice, swapChain, &imageCount, swapChainImages.data());
    }
    return res;
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

void LRenderer::addPrimitive(std::weak_ptr<LG::LGraphicsComponent> ptr)
{
    if (auto sharedPtr = ptr.lock())
    {
        const auto& typeName = sharedPtr->getTypeName();
        if (isEnoughInstanceSpace(typeName) && LG::isInstancePrimitive(sharedPtr.get()))
        {
            auto& instancesArray = instancedPrimitiveMeshes[typeName];
            instancesArray.emplace_back(ptr);
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

void LRenderer::framebufferResizeCallback(GLFWwindow* window, int32 width, int32 height)
{
    LRenderer::bFramebufferResized = true;
}
