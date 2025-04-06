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

#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>

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


	glm::mat4 playerModel;
	glm::quat playerOrientation;

	struct StaticInitData
	{
		std::unordered_map<std::string, uint32> primitiveCounter;
		std::set<std::string> textures;
		uint32 maxPortalNum = 0;
	};
	
	struct VkMemoryBuffer
	{
		VkBuffer vertexBuffer, indexBuffer;
		VmaAllocation vertexBufferMemory, indexBufferMemory;
	};

	struct PushConstants
	{
		glm::mat4 genericMatrix;
	};

	struct SSBOData
	{
		glm::mat4 genericMatrix;

		uint32 textureId = 0;
		uint32 isPortal = 0;
		uint32 reserved2 = 0;
		uint32 reserved3 = 0;
	};

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
		uint32 mipLevels;
	};

	struct RenderTarget
	{
		RenderTarget(VkDevice logicalDevice, VmaAllocator allocator):
			logicalDevice(logicalDevice), allocator(allocator){}
		~RenderTarget();

		void clear(bool bClearImages = false);

		std::vector<Image> images;
		std::vector<Image> depthImages;
		std::vector<VkFramebuffer> framebuffers;

	protected:

		VkDevice logicalDevice;
		VmaAllocator allocator;
	};

	class RenderPass
	{
	public:

		RenderPass(VkDevice logicalDevice, VkFormat colorFormat, VkFormat depthFormat, bool bToPresent);
		virtual ~RenderPass();

		void beginPass(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, const VkExtent2D& size);
		//virtual void render() = 0;
		void endPass(VkCommandBuffer commandBuffer);

		VkRenderPass getRenderPass() const { return renderPass; }

	protected:

		void init(VkFormat colorFormat, VkFormat depthFormat);
		void clear();

		VkRenderPass renderPass;
		VkDevice logicalDevice;
		bool bToPresent;
	};
	
	LRenderer(const std::unique_ptr<LWindow>& window, StaticInitData&& initData);
	~LRenderer();

	const glm::mat4& getProjection() const {return projection;}
	const glm::mat4& getView() const {return view;}

	GLFWwindow* getWindow() { return window; }

	void setProjection(float degrees, float width, float height, float zNear = 0.1f, float zFar = 100.0f);

	void setView(const glm::mat4& view);

	glm::vec3 getCameraUp() const;
	glm::vec3 getCameraFront() const;
	glm::vec3 getCameraPosition() const;

	glm::mat4 computeExitPortalView(
		const glm::vec3& playerPos,
		const glm::vec3& enterPos, const glm::vec3& enterNormal,
		const glm::vec3& exitPos, const glm::vec3& exitNormal
	);

	void setCameraFront(const glm::vec3& cameraFront);
	void setCameraPosition(const glm::vec3& cameraPosition);
	inline void setCameraPositionToPlayer(const glm::vec3& cameraPosition) {
		cameraPositionToPlayer = cameraPosition;
	}

	glm::vec3 extractScale(const glm::mat4& modelMatrix) 
	{
		glm::vec3 scale;

		// The length of each basis vector gives the scale factor
		scale.x = glm::length(glm::vec3(modelMatrix[0])); // X-axis scale
		scale.y = glm::length(glm::vec3(modelMatrix[1])); // Y-axis scale
		scale.z = glm::length(glm::vec3(modelMatrix[2])); // Z-axis scale

		return scale;
	}


	void drawFrame(float delta);
	void exit();

	static LRenderer* get()
	{
		return thisPtr;
	}

protected:

	static LRenderer* thisPtr;

	void init();
	void cleanup();

	void doPortalPass(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, const std::unique_ptr<RenderPass>& portalPass, uint32 portal1Ind, uint32 portal2Ind);
	void doMainPass(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, bool bSwitchRenderPass = true);

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
	VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32 mipLevels);
	VkResult createPortalRenderTarget();
	VkResult createDescriptorSetLayout();
	VkResult createGraphicsPipeline(const GraphicsPipelineParams& params, VkPipeline& graphicsPipelineOut, VkRenderPass renderPass);
	VkShaderModule createShaderModule(const std::vector<uint8_t>& code);
	void createFramebuffers(RenderTarget* renderTarget, const VkExtent2D& size, uint32 framebuffersNum, VkRenderPass renderPass);
	VkResult createImage(const std::string& texturePath, Image& imageOut);
	VkResult createImageInternal(uint32 width, uint32 height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VmaAllocation& imageMemory, uint32 mipLevels);
	VkResult loadTextureImage(const std::string& texturePath);
	void clearUndefinedImage(VkImage imageToClear);
	VkResult createTextureSampler(VkSampler& samplerOut, uint32 mipLevels);
	void createTextureImageView(Image& imageInOut, uint32 mipLevels);
	void initStaticDataTextures();
	void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags aspect, uint32 mipLevels);
	void copyBufferToImage(VkBuffer buffer, VkImage image, uint32 width, uint32 height);
	void generateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32 mipLevels);
	VkResult createCommandPool();
	VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
	VkFormat findDepthFormat();
	VkCommandBuffer beginSingleTimeCommands();
	void endSingleTimeCommands(VkCommandBuffer commandBuffer);

	void updateStorageBuffers(uint32 imageIndex);

	bool isEnoughStaticInstanceSpace(const std::string& typeName)
	{
		uint32 counter = primitiveCounterInitData[typeName];
		return staticInstancedPrimitiveMeshes[typeName].size() < counter;
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

	void recreateSwapChain();

	//void updatePushConstants(const LG::LGraphicsComponent& mesh);
	
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

	bool needPortalRecalculation() const;

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

	uint32 swapChainSize = 0;
	uint32 portalSize = 0;

	std::unique_ptr<RenderTarget> swapChainRt;
	std::vector<std::unique_ptr<RenderTarget>> portalsRt;

	std::unordered_map<uint32, VkSampler> textureSamplers;

	// TODO: need to be cleared
	VkSampler portalSampler;

	VkPipeline graphicsPipelineInstanced;
	VkPipeline graphicsPipelineRegular;
	VkPipeline debugGraphicsPipeline;

	// TODO: need to be cleared
	VkPipeline graphicsPipelineInstancedPortal;
	VkPipeline graphicsPipelineRegularPortal;

	std::unique_ptr<RenderPass> mainPass;
	std::vector<std::unique_ptr<RenderPass>> portalPasses;

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
	uint32 maxPortalNum;

	glm::mat4 projection;
	
    float degrees = 80.0f;
    float zNear = 0.1f;
	float zFar = 1000.0f;

	glm::vec3 cameraPositionToPlayer = glm::vec3(0.0f, 0.0f, 0.0f);
	glm::vec3 cameraPosition = glm::vec3(0.0f, 0.0f, 3.0f);
	glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
	glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
	
	glm::mat4 storedProj;
	glm::mat4 storedView;
	glm::mat4 view;

	// precalculated
	glm::mat4 projView;

	bool bNeedToUpdateProjView = false;

	std::unordered_map<std::string, Image> images;
	
	// TODO: doesn't work properly
	std::vector<std::weak_ptr<LG::LGraphicsComponent>> debugMeshes;
	std::vector<std::weak_ptr<LG::LGraphicsComponent>> primitiveMeshes;
	std::unordered_map<std::string, std::vector<std::weak_ptr<LG::LGraphicsComponent>>> staticInstancedPrimitiveMeshes;
	std::unordered_map<uint32, bool> updatedStorageBuffer;

	std::vector<std::weak_ptr<LG::LPortal>> portals;

	float delta;
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

