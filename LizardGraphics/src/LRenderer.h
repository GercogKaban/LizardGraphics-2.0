#pragma once

#include "vulkan/vulkan.h"
#include <vector>
#include <optional>
#include "globals.h"

#ifdef __APPLE__
#define VK_KHR_PORTABILITY_SUBSET "VK_KHR_portability_subset"
#else
#define VK_KHR_PORTABILITY_SUBSET 
#endif

class LRenderer
{
public:

	LRenderer(class GLFWwindow* window);
	~LRenderer();

private:

	void init();
	void cleanup();

	bool checkValidationLayerSupport() const;
	std::vector<const char*> getRequiredExtensions() const;

	VkResult setupDebugMessenger();
	VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
		const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger);

	static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
		VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);

	void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator);
	void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);

	VkResult createInstance();
	VkResult pickPhysicalDevice();
    VkResult createLogicalDevice();
	VkResult createSurface();
	VkResult createImageViews();
	
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

	// properties
	const std::vector<const char*> validationLayers = { "VK_LAYER_KHRONOS_validation" };
    const std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PORTABILITY_SUBSET};

#if NDEBUG
	const bool enableValidationLayers = false;
#else
	const bool enableValidationLayers = true;
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
};

