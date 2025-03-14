#pragma once

#include "vulkan/vulkan.h"
#include <vector>
#include <optional>
#include <memory>
#include <chrono>
#include <filesystem>
#include <unordered_map>

#include "LCore.h"
#include "LWindow.h"
#include "Primitives.h"

class LRenderer
{
	friend class ObjectBuilder;
	
public:
	
	struct VkMemoryBuffer
	{
		VkBuffer vertexBuffer, indexBuffer;
		VkDeviceMemory vertexBufferMemory, indexBufferMemory;
		uint64 memorySize;
	};

	struct PushConstants
	{
		glm::mat4 mvpMatrix;
	} pushConstants;
	
	
	LRenderer(const LWindow& window);
	~LRenderer();

	void executeTickables();

	void updateDelta()
	{
		previousFrameTime = currentFrameTime;
		currentFrameTime = std::chrono::high_resolution_clock::now();
	}
	
	float getDelta() const
	{
		return std::chrono::duration<float>(currentFrameTime - previousFrameTime).count();
	}

	const glm::mat4& getProjection() const {return projection;}
	const glm::mat4& getView() const {return view;}

	void setProjection(float degrees, float zNear = 0.1f, float zFar = 100.0f);
	void setView(const glm::mat4& view);

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
	VkResult createDescriptorSetLayout();
	VkResult createGraphicsPipeline();
	VkShaderModule createShaderModule(const std::vector<uint8_t>& code);
	VkResult createFramebuffers();
	VkResult createCommandPool();
	
	void initProjection();
	void initView();

	void updateProjView();

    enum class BufferType : uint8
    {
	    Vertex,
    	Index
    };
	
	void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
	void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
	void createUniformBuffers();
	
	template<typename Buffer>
	void createObjectBuffer(const Buffer& arrData, VkMemoryBuffer& memoryBuffer, BufferType bufferType)
	{
		using T = typename Buffer::value_type;
		memoryBuffer.memorySize = sizeof(T) * arrData.size();

		VkBuffer stagingBuffer;
		VkDeviceMemory stagingBufferMemory;
		createBuffer(memoryBuffer.memorySize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

		void* tmp;
		vkMapMemory(logicalDevice, stagingBufferMemory, 0, memoryBuffer.memorySize, 0, &tmp);
		memcpy(tmp, arrData.data(), memoryBuffer.memorySize);
		vkUnmapMemory(logicalDevice, stagingBufferMemory);

		if (bufferType == BufferType::Vertex)
		{
			createBuffer(memoryBuffer.memorySize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memoryBuffer.vertexBuffer, memoryBuffer.vertexBufferMemory);
			copyBuffer(stagingBuffer, memoryBuffer.vertexBuffer, memoryBuffer.memorySize);	
		}

		else
		{
			createBuffer(memoryBuffer.memorySize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memoryBuffer.indexBuffer, memoryBuffer.indexBufferMemory);
			copyBuffer(stagingBuffer, memoryBuffer.indexBuffer, memoryBuffer.memorySize);	
		}

		vkDestroyBuffer(logicalDevice, stagingBuffer, nullptr);
		vkFreeMemory(logicalDevice, stagingBufferMemory, nullptr);
	}

	void destroyObjectBuffer(VkMemoryBuffer& memoryBuffer)
	{
		vkDestroyBuffer(logicalDevice, memoryBuffer.indexBuffer, nullptr);
		vkFreeMemory(logicalDevice, memoryBuffer.indexBufferMemory, nullptr);
		
		vkDestroyBuffer(logicalDevice, memoryBuffer.vertexBuffer, nullptr);
		vkFreeMemory(logicalDevice, memoryBuffer.vertexBufferMemory, nullptr);
	}
	
	uint32 findMemoryType(uint32 typeFilter, VkMemoryPropertyFlags properties);

	VkResult createDescriptorPool();
	VkResult createDescriptorSets();
	VkResult createCommandBuffers();
	VkResult createSyncObjects();
	void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32 imageIndex);

	void cleanupSwapChain();
	void recreateSwapChain();

	void drawFrame();

	// TODO: needs to be optimized and also this temp rotation should be disabled
	void updatePushConstants(const Primitives::LPrimitiveMesh& mesh);
	
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

	uint32 getPushConstantSize(VkPhysicalDevice physicalDevice) const;

	void addPrimitve(std::weak_ptr<Primitives::LPrimitiveMesh> ptr);
	void addTickablePrimitive(std::weak_ptr<Primitives::LPrimitiveMesh> ptr);

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
	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorPool descriptorPool;
	std::vector<VkDescriptorSet> descriptorSets;
	
	VkPipelineLayout pipelineLayout;

	VkCommandPool commandPool;
	std::vector<VkCommandBuffer> commandBuffers;

	std::vector<VkBuffer> uniformBuffers;
	std::vector<VkDeviceMemory> uniformBuffersMemory;
	std::vector<void*> uniformBuffersMapped;

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

	glm::mat4 projection;
	
    float degrees = 45.0f;
    float zNear = 0.1f;
	float zFar = 100.0f;
	
	glm::mat4 view;

	// precalculated
	glm::mat4 projView;

	bool bNeedToUpdateProjView = false;
	
	std::vector<std::weak_ptr<Primitives::LPrimitiveMesh>> primitiveMeshes;
	std::vector<std::weak_ptr<Primitives::LPrimitiveMesh>> tickableMeshes;
};

class ObjectBuilder
{
public:

    [[nodiscard]] static const LRenderer::VkMemoryBuffer& getMemoryBuffer(const std::string& primitiveName)
	{
		return memoryBuffers[primitiveName];
	}

	template<typename T>
	[[nodiscard]] static std::shared_ptr<T> construct()
	{
#ifndef NDEBUG
		bIsConstructing = true;
#endif
		
		std::shared_ptr<T> object = std::shared_ptr<T>(new T());
		
		// TODO: it worth to implement UE FName alternative to save some memory
		const_cast<std::string&>(object->typeName) = std::string(typeid(T).name());
		const_cast<uint32&>(object->indicesCount) = object->getIndexBuffer().size();
		if (LRenderer* renderer = LRenderer::get())
		{
			auto resCounter = objectsCounter.emplace(object->typeName, 0);
			auto resBuffer = memoryBuffers.emplace(object->typeName, LRenderer::VkMemoryBuffer());
		
			if (resCounter.first->second++ == 0)
			{
				renderer->createObjectBuffer(object->getVertexBuffer(), resBuffer.first->second, LRenderer::BufferType::Vertex);
				renderer->createObjectBuffer(object->getIndexBuffer(), resBuffer.first->second, LRenderer::BufferType::Index);
			}

			renderer->addPrimitve(object);

			if constexpr (std::is_base_of<LTickable, T>::value)
			{
				renderer->addTickablePrimitive(object);
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
				renderer->destroyObjectBuffer(resBuffer->second);
			}
		}
	}

#ifndef NDEBUG
	static bool isConstructing() {return bIsConstructing;}
#endif
        
protected:
        
	static std::unordered_map<std::string, int32> objectsCounter;
	static std::unordered_map<std::string, LRenderer::VkMemoryBuffer> memoryBuffers;
#ifndef NDEBUG
	static bool bIsConstructing;
#endif
};   

