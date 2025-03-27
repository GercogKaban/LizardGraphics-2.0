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

//#define TRACY_ENABLE
#include <tracy/Tracy.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

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

LRenderer::LRenderer(const std::unique_ptr<LWindow>& window, std::unordered_map<std::string, uint32> primitiveCounter)
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

    HANDLE_VK_ERROR(createCommandPool())
    createDepthResources();
    HANDLE_VK_ERROR(createFramebuffers())
    HANDLE_VK_ERROR(createTextureImage())
    createTextureImageView();
    HANDLE_VK_ERROR(createTextureSampler())

    createInstancesStorageBuffers();

    HANDLE_VK_ERROR(createDescriptorPool())
    HANDLE_VK_ERROR(createDescriptorSets())
    HANDLE_VK_ERROR(createCommandBuffers())
    HANDLE_VK_ERROR(createSyncObjects())
}

void LRenderer::cleanup()
{
    cleanupSwapChain();

    vkDestroySampler(logicalDevice, textureSampler, nullptr);
    vkDestroyImageView(logicalDevice, textureImageView, nullptr);
    vkDestroyImage(logicalDevice, textureImage, nullptr);

    for (int32 i = 0; i < swapChainImages.size(); ++i)
    {
        vkDestroyImageView(logicalDevice, depthImagesView[i], nullptr);
        vkDestroyImage(logicalDevice, depthImages[i], nullptr);
        vmaFreeMemory(allocator, depthImagesMemory[i]);
    }

    vmaFreeMemory(allocator, textureImageMemory);

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

VkImageView LRenderer::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    HANDLE_VK_ERROR(vkCreateImageView(logicalDevice , &viewInfo, nullptr, &imageView))
    return imageView;
}

VkResult LRenderer::createImageViews()
{
    swapChainImageViews.resize(swapChainImages.size());

    for (uint32 i = 0; i < swapChainImages.size(); ++i) 
    {
        swapChainImageViews[i] = createImageView(swapChainImages[i], swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    }
    return VK_SUCCESS;
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

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = findDepthFormat();
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
    dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkSubpassDependency depthDependency{};
    depthDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    depthDependency.dstSubpass = 0;
    depthDependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    depthDependency.srcAccessMask = 0;
    depthDependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    depthDependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    depthDependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    std::array<VkSubpassDependency, 2> dependencies = { dependency, depthDependency};
    std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    return vkCreateRenderPass(logicalDevice, &renderPassInfo, nullptr, &renderPass);
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
    samplerLayoutBinding.descriptorCount = 1;
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
    pipelineLayoutInfo.setLayoutCount = 1; // Optional
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

VkResult LRenderer::createFramebuffers()
{
    swapChainFramebuffers.resize(swapChainImageViews.size());
    for (uint32 i = 0; i < swapChainImageViews.size(); ++i)
    {
        std::array<VkImageView, 2> attachments = 
        {
            swapChainImageViews[i],
            depthImagesView[i]
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = swapChainExtent.width;
        framebufferInfo.height = swapChainExtent.height;
        framebufferInfo.layers = 1;

        HANDLE_VK_ERROR(vkCreateFramebuffer(logicalDevice, &framebufferInfo, nullptr, &swapChainFramebuffers[i]))
    }
    return VK_SUCCESS;
}

VkResult LRenderer::createImage(uint32 width, uint32 height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VmaAllocation& imageMemory)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    HANDLE_VK_ERROR(vkCreateImage(logicalDevice, &imageInfo, nullptr, &image))

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(logicalDevice, image, &memRequirements);

    VmaAllocationCreateInfo createInfo{};
    createInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    createInfo.memoryTypeBits = findMemoryType(memRequirements.memoryTypeBits, properties);

    VmaAllocationInfo allocInfo{};
    HANDLE_VK_ERROR(vmaAllocateMemory(allocator, &memRequirements, &createInfo, &imageMemory, &allocInfo))

    return vkBindImageMemory(logicalDevice, image, imageMemory->GetMemory(), 0);
}

VkResult LRenderer::createTextureImage()
{
    stbi_set_flip_vertically_on_load(true);

    int texWidth, texHeight, texChannels;

    if (stbi_uc* pixels = stbi_load("textures/smile.jpg", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha))
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

        HANDLE_VK_ERROR(createImage(texWidth, texHeight, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL, 
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 
            textureImage, textureImageMemory))

        transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        copyBufferToImage(stagingBuffer, textureImage, static_cast<uint32>(texWidth), static_cast<uint32>(texHeight));
        transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

        vmaUnmapMemory(allocator, stagingBufferMemory);
        vmaDestroyBuffer(allocator, stagingBuffer, stagingBufferMemory);

        return VK_SUCCESS;
    }
    else
    {
        RAISE_VK_ERROR("failed to load texture image!");
    }
}

VkResult LRenderer::createTextureSampler()
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
    samplerInfo.maxLod = 0.0f;
    return vkCreateSampler(logicalDevice, &samplerInfo, nullptr, &textureSampler);
}

void LRenderer::createTextureImageView()
{
    textureImageView = createImageView(textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);
}

void LRenderer::transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags aspect)
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
    barrier.subresourceRange.levelCount = 1;
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

void LRenderer::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
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

//bool LRenderer::hasStencilComponent(VkFormat format) const
//{
//    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
//}

void LRenderer::createDepthResources()
{
    depthImages.resize(swapChainImages.size());
    depthImagesMemory.resize(swapChainImages.size());
    depthImagesView.resize(swapChainImages.size());

    VkFormat depthFormat = findDepthFormat();
    for (int32 i = 0; i < swapChainImages.size(); ++i)
    {
        createImage(swapChainExtent.width, swapChainExtent.height, depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImages[i], depthImagesMemory[i]);
        transitionImageLayout(depthImages[i], depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
        depthImagesView[i] = createImageView(depthImages[i], depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
    }
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
    uint64 instancedArraysSize = instancedPrimitiveMeshes.size();

    int32 instancedArrayNum = 0;
    for (const auto& [primitiveName, primitives] : instancedPrimitiveMeshes)
    {
        // buffer array
        VkBuffer bufferToCopy = primitivesData[imageIndex * instancedArraysSize + instancedArrayNum].buffer;
        const auto& indices = primitiveDataIndices[primitiveName];

#if _MSC_VER
        std::for_each(std::execution::par_unseq, indices.begin(), indices.end(), [&](uint32 i)
        {
            if (auto objectPtr = primitives[i].lock())
            {
                PushConstants data{ projView * objectPtr->getModelMatrix() };
                memcpy((uint8_t*)stagingBufferPtr + i * sizeof(PushConstants), &data, sizeof(PushConstants));
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

     uint64 instancedArraysSize = primitiveCounter.size();

     primitivesData.resize(instancedArraysSize * maxFramesInFlight);

     for (uint32 i = 0; i < maxFramesInFlight; ++i)
     {
         int32 instancedArrayNum = 0;
         for (const auto& [_, primitivesNum] : primitiveCounter)
         {
             int32 index = i * instancedArraysSize + instancedArrayNum;
             auto bufferSize = sizeof(PushConstants) * primitivesNum;
             createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, primitivesData[index].buffer, primitivesData[index].memory);
             ++instancedArrayNum;
         }
     }

     for (const auto& [primitiveName, primitivesNum] : primitiveCounter)
     {
         std::vector<uint32> indices(primitivesNum);
         std::iota(indices.begin(), indices.end(), 0);
         primitiveDataIndices.emplace(std::make_pair(primitiveName, indices));
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
    return maxPrimitiveCounter * sizeof(PushConstants);
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
    poolSizes[0].descriptorCount = static_cast<uint32>(maxFramesInFlight) * primitiveCounter.size();
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = static_cast<uint32>(maxFramesInFlight) * primitiveCounter.size();

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32>(maxFramesInFlight) * primitiveCounter.size();

    return vkCreateDescriptorPool(logicalDevice, &poolInfo, nullptr, &descriptorPool);
}

VkResult LRenderer::createDescriptorSets()
{
     std::vector<VkDescriptorSetLayout> layouts(static_cast<uint32>(maxFramesInFlight) * primitiveCounter.size(), descriptorSetLayout);
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
             bufferInfo.buffer = primitivesData[i * primitiveCounter.size() + instancedArrayNum].buffer;
             bufferInfo.offset = 0;
             bufferInfo.range = sizeof(PushConstants) * primitivesNum;

             VkDescriptorImageInfo imageInfo{};
             imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
             imageInfo.imageView = textureImageView;
             imageInfo.sampler = textureSampler;

             std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

             descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
             descriptorWrites[0].dstSet = descriptorSets[i * primitiveCounter.size() + instancedArrayNum];
             descriptorWrites[0].dstBinding = 0;
             descriptorWrites[0].dstArrayElement = 0;
             descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
             descriptorWrites[0].descriptorCount = 1;
             descriptorWrites[0].pBufferInfo = &bufferInfo;

             descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
             descriptorWrites[1].dstSet = descriptorSets[i * primitiveCounter.size() + instancedArrayNum];
             descriptorWrites[1].dstBinding = 1;
             descriptorWrites[1].dstArrayElement = 0;
             descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
             descriptorWrites[1].descriptorCount = 1;
             descriptorWrites[1].pImageInfo = &imageInfo;

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

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapChainExtent;

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = { 1.0f, 0 };

    renderPassInfo.clearValueCount = static_cast<uint32>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();
    
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

    updateStorageBuffers(currentFrame);
    
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
    
    {
        ZoneScopedNC("Render call", 0xFFFF0000);
        HANDLE_VK_ERROR(vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]))
    }
    
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
