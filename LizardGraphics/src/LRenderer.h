#pragma once

#include "vulkan/vulkan.h"
#include <vector>
#include <optional>
#include <memory>

#include "LWindow.h"
#include "Primitives.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

struct VkMemoryBuffer
{
    VkBuffer buffer;
	VkDeviceMemory memory;
	uint64 memorySize;
};

class LRenderer
{
	friend class ObjectBuilder;
	
public:

	LRenderer(const LWindow& window);
	~LRenderer();

	void updateDelta()
	{
		previousFrameTime = currentFrameTime;
		currentFrameTime = std::chrono::high_resolution_clock::now();
	}
	
	float getDelta() const
	{
		return std::chrono::duration<float>(currentFrameTime - previousFrameTime).count();
	}

	void loop();
	static LRenderer* get()
	{
		return thisPtr;
	}

private:

	static LRenderer* thisPtr;

	void init();
	void cleanup();

	bool checkValidationLayerSupport() const;
	std::vector<const char*> getRequiredExtensions() const;

	VkResult setupDebugMessenger();
	VkResult CreateDebugUtilsMessengerEXT(VkInstance instanceIn, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
		const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger);

	static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
		VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);

	static void framebufferResizeCallback(class GLFWwindow* window, int32 width, int32 height);
	
	void DestroyDebugUtilsMessengerEXT(VkInstance instanceIn, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator);
	void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);

	VkResult createInstance();
	VkResult pickPhysicalDevice();
    VkResult createLogicalDevice();
	VkResult createSurface();
	VkResult createImageViews();
	VkResult createRenderPass();
	VkResult createGraphicsPipeline();
	VkShaderModule createShaderModule(const std::vector<char>& code);
	VkResult rebuildShaders();
	VkResult createFramebuffers();
	VkResult createCommandPool();
	
	template<typename Buffer>
	void createVBO(const Buffer& vertices, VkMemoryBuffer& memoryBuffer)
	{
		using T = typename Buffer::value_type;

		memoryBuffer.memorySize = sizeof(T) * vertices.size();
		
		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = memoryBuffer.memorySize;
		bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		HANDLE_VK_ERROR(vkCreateBuffer(logicalDevice, &bufferInfo, nullptr, &memoryBuffer.buffer))

		VkMemoryRequirements memRequirements;
		vkGetBufferMemoryRequirements(logicalDevice, memoryBuffer.buffer, &memRequirements);

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		HANDLE_VK_ERROR(vkAllocateMemory(logicalDevice, &allocInfo, nullptr, &memoryBuffer.memory))
		HANDLE_VK_ERROR(vkBindBufferMemory(logicalDevice, memoryBuffer.buffer, memoryBuffer.memory, 0))

		void* data;
		HANDLE_VK_ERROR(vkMapMemory(logicalDevice, memoryBuffer.memory, 0, bufferInfo.size, 0, &data))
		memcpy(data, vertices.data(), bufferInfo.size);
		vkUnmapMemory(logicalDevice, memoryBuffer.memory);
	}

	void destroyVBO(VkMemoryBuffer& memoryBuffer)
	{
		vkDestroyBuffer(logicalDevice, memoryBuffer.buffer, nullptr);
		vkFreeMemory(logicalDevice, memoryBuffer.memory, nullptr);
	}
	
	uint32 findMemoryType(uint32 typeFilter, VkMemoryPropertyFlags properties);
	
	VkResult createCommandBuffers();
	VkResult createSyncObjects();
	void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32 imageIndex);

	void cleanupSwapChain();
	void recreateSwapChain();

	void drawFrame();
	
	bool isDeviceSuitable(VkPhysicalDevice device) const;
	bool checkDeviceExtensionSupport(VkPhysicalDevice device) const;

	struct QueueFamilyIndices
	{
		std::optional<uint32> graphicsFamily;
		std::optional<uint32> presentFamily;
		
		bool isValid() const
		{
			return graphicsFamily.has_value() && presentFamily.has_value();
		}
	};

	struct SwapChainSupportDetails
	{
		VkSurfaceCapabilitiesKHR capabilities;
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR> presentModes;
	};
	
	QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) const;

	VkResult createSwapChain();
	SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device) const;
	VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) const;
	VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) const;
	VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;

	void addPrimitve(std::weak_ptr<Primitives::LPrimitiveMesh> ptr);

	// properties

	LWindow::LWindowSpecs specs;
	
	const std::vector<const char*> validationLayers = { "VK_LAYER_KHRONOS_validation" };
    const std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PORTABILITY_SUBSET};

#if NDEBUG
	const bool enableValidationLayers = false;
#else
	// TODO: for now it always false, because it causes a strange crash in vkCreateGraphicsPipelines 
	const bool enableValidationLayers = false;
#endif
	VkInstance instance;
	
	VkDebugUtilsMessengerEXT debugMessenger;
	
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkDevice logicalDevice;
	
	VkQueue graphicsQueue;
	VkQueue presentQueue;

	VkSurfaceKHR surface;
	VkSwapchainKHR swapChain;
	class GLFWwindow* window;
	VkFormat swapChainImageFormat;
	VkExtent2D swapChainExtent;
	std::vector<VkImage> swapChainImages;
	std::vector<VkImageView> swapChainImageViews;
	std::vector<VkFramebuffer> swapChainFramebuffers;

	VkPipeline graphicsPipeline;
	VkRenderPass renderPass;
	VkPipelineLayout pipelineLayout;

	VkCommandPool commandPool;
	std::vector<VkCommandBuffer> commandBuffers;

	std::vector<VkSemaphore> imageAvailableSemaphores;
	std::vector<VkSemaphore> renderFinishedSemaphores;
	std::vector<VkFence> inFlightFences;

	static bool bFramebufferResized;

	std::filesystem::path shadersPath;

	const int32 maxFramesInFlight = 2;
	uint32 currentFrame = 0;

	std::chrono::high_resolution_clock::time_point currentFrameTime = std::chrono::high_resolution_clock::now();
	std::chrono::high_resolution_clock::time_point previousFrameTime = std::chrono::high_resolution_clock::now();

	float fpsTimer = 0.0f;
	uint32 fps = 0;

	VkBuffer vertexBufferTriangle;
	
	std::vector<std::weak_ptr<Primitives::LPrimitiveMesh>> primitiveMeshes;
};

class ObjectBuilder
{
public:

	static [[nodiscard]] const VkMemoryBuffer& getMemoryBuffer(const std::string& primitiveName)
	{
		return memoryBuffers[primitiveName];
	}

	template<typename T>
	static [[nodiscard]] std::shared_ptr<T> construct()
	{
#ifndef NDEBUG
		bIsConstructing = true;
#endif

		// TODO: need to be added to the std::vector
		std::shared_ptr<T> object = std::shared_ptr<T>(new T());
		// TODO: it worth to implement UE FName alternative to save some memory
		object->typeName = std::string(typeid(T).name());
		object->vertexCount = object->getBuffer().size();
        
		auto resCounter = objectsCounter.emplace(object->typeName, 0);
		auto resBuffer = memoryBuffers.emplace(object->typeName, VkMemoryBuffer());
		
		if (resCounter.first->second++ == 0)
		{
			if (LRenderer* renderer = LRenderer::get())
			{
				renderer->createVBO(object->getBuffer(), resBuffer.first->second);
				renderer->addPrimitve(object);
			}
		}
#ifndef NDEBUG
		bIsConstructing = false;
#endif
        
		return object;
	}

	template<typename T>
	static void destruct(T* object)
	{
		auto resCounter = objectsCounter.find(object->typeName);
		auto resBuffer = memoryBuffers.find(object->typeName);

		if (int32 counter = --resCounter->second; counter == 0)
		{
			if (LRenderer* renderer = LRenderer::get())
			{
				// maybe we want to cache it, but not destroy
				renderer->destroyVBO(resBuffer->second);
			}
		}
	}

#ifndef NDEBUG
	static bool isConstructing() {return bIsConstructing;}
#endif
        
protected:
        
	static std::unordered_map<std::string, int32> objectsCounter;
	static std::unordered_map<std::string, VkMemoryBuffer> memoryBuffers;
#ifndef NDEBUG
	static bool bIsConstructing;
#endif
};   

