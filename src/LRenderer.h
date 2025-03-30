#pragma once

#include "vulkan/vulkan.h"
#include <vector>
#include <optional>
#include <memory>
#include <chrono>
#include <filesystem>
#include <unordered_map>
#include <map>
#include <string>
#include <set>

#include "LWindow.h"
#include "Primitives.h"

#include <vma/vk_mem_alloc.h>

#ifndef NDEBUG
#define DEBUG_CODE(x) x
#else
#define DEBUG_CODE(x)
#endif
    
class LRenderer
{
	friend class RenderComponentBuilder;
	
public:

	struct StaticInitData
	{
		std::unordered_map<std::string, uint32> primitiveCounter;
		std::set<std::string> textures;
	};
	
	struct VkMemoryBuffer
	{
		VkBuffer vertexBuffer, indexBuffer;
		VmaAllocation vertexBufferMemory, indexBufferMemory;
	};

	struct PushConstants
	{
		glm::mat4 mvpMatrix;

		uint32 textureId = 0;
		uint32 reserved1 = 0;
		uint32 reserved2 = 0;
		uint32 reserved3 = 0;
	} pushConstants;

	struct GraphicsPipelineParams
	{
		VkPolygonMode polygonMode;
		bool bInstanced;
	};

	struct Image
	{
		VkImage image;
		VkImageView imageView;
		VmaAllocation allocation;
	};
	
	LRenderer(const std::unique_ptr<LWindow>& window, StaticInitData&& initData);
	~LRenderer();

	const glm::mat4& getProjection() const {return projection;}
	const glm::mat4& getView() const {return view;}

	GLFWwindow* getWindow() { return window; }

	void setProjection(float degrees, float zNear = 0.1f, float zFar = 100.0f);
	void setView(const glm::mat4& view);

	glm::vec3 getCameraUp() const;
	glm::vec3 getCameraFront() const;
	glm::vec3 getCameraPosition() const;

	void setCameraFront(const glm::vec3& cameraFront);
	void setCameraPosition(const glm::vec3& cameraPosition);

	void drawFrame();
	void exit();

	static LRenderer* get()
	{
		return thisPtr;
	}

protected:

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
	VkResult createAllocator();
	VkResult createSurface();
	VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
	VkResult createImageViews();
	VkResult createRenderPass();
	VkResult createDescriptorSetLayout();
	VkResult createGraphicsPipeline(const GraphicsPipelineParams& params, VkPipeline& graphicsPipelineOut);
	VkShaderModule createShaderModule(const std::vector<uint8_t>& code);
	VkResult createFramebuffers();
	VkResult createImage(const std::string& texturePath, Image& imageOut);
	VkResult createImageInternal(uint32 width, uint32 height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VmaAllocation& imageMemory);
	VkResult loadTextureImage(const std::string& texturePath);
	VkResult createTextureSampler();
	void createTextureImageView(Image& imageInOut);
	void initStaticDataTextures();
	void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags aspect);
	void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
	VkResult createCommandPool();
	VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
	VkFormat findDepthFormat();
	void createDepthResources();
	VkCommandBuffer beginSingleTimeCommands();
	void endSingleTimeCommands(VkCommandBuffer commandBuffer);

	void updateStorageBuffers(uint32 imageIndex);

	bool isEnoughInstanceSpace(const std::string& typeName)
	{
		uint32 counter = primitiveCounterInitData[typeName];
		return instancedPrimitiveMeshes[typeName].size() < counter;
	}
	
	void initProjection();
	void initView();

	void updateProjView();

    enum class BufferType : uint8
    {
	    Vertex,
    	Index
    };
	
	void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage properties, VkBuffer& buffer, VmaAllocation& bufferMemory, uint32 vmaFlags = 0);
	void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
	void createInstancesStorageBuffers();
	uint32 findProperStageBufferSize() const;

	void vmaMapWrap(VmaAllocator allocator, VmaAllocation* memory, void*& mappedData);
	void vmaUnmapWrap(VmaAllocator allocator, VmaAllocation* memory);
	void vmaDestroyBufferWrap(VmaAllocator allocator, VkBuffer& buffer, VmaAllocation* memory);
	
	template<typename Buffer>
	void createObjectBuffer(const Buffer& arrData, VkMemoryBuffer& memoryBuffer, BufferType bufferType)
	{
		using T = typename Buffer::value_type;
		
		auto memorySize = sizeof(T) * arrData.size();

		VkBuffer stagingBuffer;
		VmaAllocation stagingBufferMemory;
		createBuffer(memorySize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO, stagingBuffer, stagingBufferMemory, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

		void* mappedData = nullptr;
		vmaMapWrap(allocator, &stagingBufferMemory, mappedData);
		memcpy(mappedData, arrData.data(), memorySize);
		vmaUnmapWrap(allocator, &stagingBufferMemory);

		if (bufferType == BufferType::Vertex)
		{
			createBuffer(memorySize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, memoryBuffer.vertexBuffer, memoryBuffer.vertexBufferMemory);
			copyBuffer(stagingBuffer, memoryBuffer.vertexBuffer, memorySize);	
		}

		else
		{
			createBuffer(memorySize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, memoryBuffer.indexBuffer, memoryBuffer.indexBufferMemory);
			copyBuffer(stagingBuffer, memoryBuffer.indexBuffer, memorySize);	
		}

		vmaDestroyBufferWrap(allocator, stagingBuffer, &stagingBufferMemory);
	}

	void destroyObjectBuffer(VkMemoryBuffer& memoryBuffer)
	{
		vmaDestroyBufferWrap(allocator, memoryBuffer.vertexBuffer, &memoryBuffer.vertexBufferMemory);
		vmaDestroyBufferWrap(allocator, memoryBuffer.indexBuffer, &memoryBuffer.indexBufferMemory);
	}
	
	uint32 findMemoryType(uint32 typeFilter, VkMemoryPropertyFlags properties);

	VkResult createDescriptorPool();
	VkResult createDescriptorSets();
	VkResult createCommandBuffers();
	VkResult createSyncObjects();
	void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32 imageIndex);

	void cleanupSwapChain();
	void recreateSwapChain();

	void updatePushConstants(const LG::LGraphicsComponent& mesh);
	
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
	uint32 getMaxAnisotropy(VkPhysicalDevice physicalDeviceIn) const;

	void addPrimitive(std::weak_ptr<LG::LGraphicsComponent> ptr);
	DEBUG_CODE(void addDebugPrimitive(std::weak_ptr<LG::LGraphicsComponent> ptr);)

	// properties

	LWindow::LWindowSpecs specs;
	
	const std::vector<const char*> validationLayers = { "VK_LAYER_KHRONOS_validation" };
    const std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PORTABILITY_SUBSET};

#if NDEBUG
	const bool enableValidationLayers = false;
#elif __APPLE__
	const bool enableValidationLayers = false;
#else 
	const bool enableValidationLayers = true;
#endif
	VkInstance instance;

	int32 majorApiVersion = 1;
	int32 minorApiVersion = 2;
	
	VkDebugUtilsMessengerEXT debugMessenger;
	
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkDevice logicalDevice;

	VmaAllocator allocator;
	
	VkQueue graphicsQueue;
	VkQueue presentQueue;

	VkSurfaceKHR surface;
	VkSwapchainKHR swapChain;
	GLFWwindow* window;
	VkFormat swapChainImageFormat;
	VkExtent2D swapChainExtent;
	std::vector<VkImage> swapChainImages;
	std::vector<VkImageView> swapChainImageViews;
	std::vector<VkFramebuffer> swapChainFramebuffers;

	VkSampler textureSampler;

	// TODO: should be packed to the Image I guess
	std::vector<VkImage> depthImages;
	std::vector<VmaAllocation> depthImagesMemory;
	std::vector<VkImageView> depthImagesView;

	VkPipeline graphicsPipelineInstanced;
	VkPipeline graphicsPipelineRegular;
	VkPipeline debugGraphicsPipeline;

	VkRenderPass renderPass;
	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorPool descriptorPool;
	std::vector<VkDescriptorSet> descriptorSets;
	
	VkPipelineLayout pipelineLayout = nullptr;

	VkCommandPool commandPool;
	std::vector<VkCommandBuffer> commandBuffers;

	std::vector<VkSemaphore> imageAvailableSemaphores;
	std::vector<VkSemaphore> renderFinishedSemaphores;
	std::vector<VkFence> inFlightFences;

	static bool bFramebufferResized;

	std::filesystem::path shadersPath;

	static const int32 maxFramesInFlight = 2;
	uint32 currentFrame = 0;

	struct ObjectDataBuffer
	{
		VkBuffer buffer;
		VmaAllocation memory;
	};

	// used for multithread write
	std::unordered_map<std::string, std::vector<uint32>> primitiveDataIndices;
	
	// TODO: should be destroyed
	std::vector<ObjectDataBuffer> primitivesData;

	// TODO: should be destroyed
	ObjectDataBuffer stagingBuffer;
	void* stagingBufferPtr;

	std::unordered_map<std::string, uint32> primitiveCounterInitData;
	std::unordered_map<std::string, uint32> texturesInitData;

	glm::mat4 projection;
	
    float degrees = 80.0f;
    float zNear = 0.1f;
	float zFar = 1000.0f;

	glm::vec3 cameraPosition = glm::vec3(0.0f, 0.0f, 3.0f);
	glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
	glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
	
	glm::mat4 view;

	// precalculated
	glm::mat4 projView;

	bool bNeedToUpdateProjView = false;

	std::unordered_map<std::string, Image> images;
	
	// TODO: doesn't work properly
	std::vector<std::weak_ptr<LG::LGraphicsComponent>> debugMeshes;
	std::vector<std::weak_ptr<LG::LGraphicsComponent>> primitiveMeshes;
	std::unordered_map<std::string, std::vector<std::weak_ptr<LG::LGraphicsComponent>>> instancedPrimitiveMeshes;
};

class RenderComponentBuilder
{
protected:

	friend class LG::LGraphicsComponent;
	friend class LRenderer;
	friend class LEngine;
	friend class ObjectBuilder;

	// template<typename T>
	// [[nodiscard]] static std::shared_ptr<T> construct()
	// {
	// 	auto object = constructImpl<T>();
	// 	LRenderer::get()->addPrimitve(object);
	// 	return object;

	// }

//	DEBUG_CODE(
//	template<typename T>
//	[[nodiscard]] static std::shared_ptr<T> constructDebug()
//	{
//		std::shared_ptr<T> object = std::shared_ptr<T>(new T());
//
//		adjustImpl<T>(object);
//		LRenderer::get()->addDebugPrimitive(object);
//		return object;
//	}
//)

	static void adjustImpl(const std::weak_ptr<LG::LGraphicsComponent>& graphicsComponent)
	{
		DEBUG_CODE(
			bIsConstructing = true;
			)

		auto object = graphicsComponent.lock();

		// TODO: it worth to implement UE FName alternative to save some memory
		if (LRenderer* renderer = LRenderer::get())
		{
			auto resCounter = objectsCounter.emplace(object->getTypeName(), 0);
			auto resBuffer = memoryBuffers.emplace(object->getTypeName(), LRenderer::VkMemoryBuffer());

			if (resCounter.first->second++ == 0)
			{
				renderer->createObjectBuffer(object->getVertexBuffer(), resBuffer.first->second, LRenderer::BufferType::Vertex);
				renderer->createObjectBuffer(object->getIndexBuffer(), resBuffer.first->second, LRenderer::BufferType::Index);
			}
			renderer->addPrimitive(object);
		}
		DEBUG_CODE(
			bIsConstructing = false;
		)
	}

	template<typename T>
	static void destruct(T* object)
	{
		auto resCounter = objectsCounter.find(object->getTypeName());
		auto resBuffer = memoryBuffers.find(object->getTypeName());

		if (int32 counter = --resCounter->second; counter == 0)
		{
			if (LRenderer* renderer = LRenderer::get())
			{
				// maybe we want to cache it, but not destroy
				renderer->destroyObjectBuffer(resBuffer->second);
			}
		}
	}

	[[nodiscard]] static const LRenderer::VkMemoryBuffer& getMemoryBuffer(const std::string& primitiveName)
	{
		return memoryBuffers[primitiveName];
	}

	DEBUG_CODE(
		static bool isConstructing() { return bIsConstructing; }
	)

protected:

	static std::unordered_map<std::string, int32> objectsCounter;
	static std::unordered_map<std::string, LRenderer::VkMemoryBuffer> memoryBuffers;

	DEBUG_CODE(
		static bool bIsConstructing;
	)
};

