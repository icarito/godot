#include "vulkan_memory_probe.h"

#include "core/error_macros.h"
#include "core/ustring.h"
#include "core/vector.h"

#define VK_NO_PROTOTYPES
#include "vulkan/vulkan_core.h"

#include <string.h>

#ifdef WINDOWS_ENABLED
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

#ifdef WINDOWS_ENABLED
const char *VULKAN_LIBRARY = "vulkan-1.dll";
const char *EXTERNAL_MEMORY_EXTENSION = "VK_KHR_external_memory_win32";
const VkExternalMemoryHandleTypeFlags EXTERNAL_HANDLE_TYPE = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
const char *VULKAN_LIBRARY = "libvulkan.so.1";
const char *EXTERNAL_MEMORY_EXTENSION = "VK_KHR_external_memory_fd";
const VkExternalMemoryHandleTypeFlags EXTERNAL_HANDLE_TYPE = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif

struct VulkanProcs {
	PFN_vkGetInstanceProcAddr get_instance_proc_addr = nullptr;
	PFN_vkDestroyInstance destroy_instance = nullptr;
	PFN_vkEnumeratePhysicalDevices enumerate_physical_devices = nullptr;
	PFN_vkGetPhysicalDeviceProperties2 get_physical_device_properties2 = nullptr;
	PFN_vkEnumerateDeviceExtensionProperties enumerate_device_extension_properties = nullptr;
	PFN_vkCreateDevice create_device = nullptr;
	PFN_vkDestroyDevice destroy_device = nullptr;
	PFN_vkCreateImage create_image = nullptr;
	PFN_vkGetImageMemoryRequirements get_image_memory_requirements = nullptr;
	PFN_vkDestroyImage destroy_image = nullptr;

	bool load(VkInstance p_instance) {
		destroy_instance = (PFN_vkDestroyInstance)get_instance_proc_addr(p_instance, "vkDestroyInstance");
		enumerate_physical_devices = (PFN_vkEnumeratePhysicalDevices)get_instance_proc_addr(p_instance, "vkEnumeratePhysicalDevices");
		get_physical_device_properties2 = (PFN_vkGetPhysicalDeviceProperties2)get_instance_proc_addr(p_instance, "vkGetPhysicalDeviceProperties2");
		enumerate_device_extension_properties = (PFN_vkEnumerateDeviceExtensionProperties)get_instance_proc_addr(p_instance, "vkEnumerateDeviceExtensionProperties");
		create_device = (PFN_vkCreateDevice)get_instance_proc_addr(p_instance, "vkCreateDevice");
		destroy_device = (PFN_vkDestroyDevice)get_instance_proc_addr(p_instance, "vkDestroyDevice");
		create_image = (PFN_vkCreateImage)get_instance_proc_addr(p_instance, "vkCreateImage");
		get_image_memory_requirements = (PFN_vkGetImageMemoryRequirements)get_instance_proc_addr(p_instance, "vkGetImageMemoryRequirements");
		destroy_image = (PFN_vkDestroyImage)get_instance_proc_addr(p_instance, "vkDestroyImage");
		return destroy_instance != nullptr && enumerate_physical_devices != nullptr &&
				get_physical_device_properties2 != nullptr && enumerate_device_extension_properties != nullptr &&
				create_device != nullptr && destroy_device != nullptr && create_image != nullptr &&
				get_image_memory_requirements != nullptr && destroy_image != nullptr;
	}
};

void *open_vulkan_library() {
#ifdef WINDOWS_ENABLED
	return (void *)LoadLibraryA(VULKAN_LIBRARY);
#else
	return dlopen(VULKAN_LIBRARY, RTLD_NOW | RTLD_LOCAL);
#endif
}

void *vulkan_library_symbol(void *p_library, const char *p_name) {
#ifdef WINDOWS_ENABLED
	return (void *)GetProcAddress((HMODULE)p_library, p_name);
#else
	return dlsym(p_library, p_name);
#endif
}

void close_vulkan_library(void *p_library) {
#ifdef WINDOWS_ENABLED
	FreeLibrary((HMODULE)p_library);
#else
	dlclose(p_library);
#endif
}

VkPhysicalDevice find_device_by_uuid(const VulkanProcs &p_vk, VkInstance p_instance, const uint8_t *p_device_uuid) {
	uint32_t count = 0;
	p_vk.enumerate_physical_devices(p_instance, &count, nullptr);
	Vector<VkPhysicalDevice> devices;
	devices.resize(count);
	p_vk.enumerate_physical_devices(p_instance, &count, devices.ptrw());

	for (uint32_t i = 0; i < count; i++) {
		VkPhysicalDeviceIDProperties id_properties = {};
		id_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
		VkPhysicalDeviceProperties2 properties = {};
		properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		properties.pNext = &id_properties;
		p_vk.get_physical_device_properties2(devices[i], &properties);
		if (memcmp(id_properties.deviceUUID, p_device_uuid, VK_UUID_SIZE) == 0) {
			return devices[i];
		}
	}
	return VK_NULL_HANDLE;
}

bool device_has_extension(const VulkanProcs &p_vk, VkPhysicalDevice p_device, const char *p_extension) {
	uint32_t count = 0;
	p_vk.enumerate_device_extension_properties(p_device, nullptr, &count, nullptr);
	Vector<VkExtensionProperties> extensions;
	extensions.resize(count);
	p_vk.enumerate_device_extension_properties(p_device, nullptr, &count, extensions.ptrw());
	for (uint32_t i = 0; i < count; i++) {
		if (strcmp(extensions[i].extensionName, p_extension) == 0) {
			return true;
		}
	}
	return false;
}

uint64_t measure_image(const VulkanProcs &p_vk, VkPhysicalDevice p_physical_device, int p_width, int p_height) {
	const float priority = 1.0f;
	VkDeviceQueueCreateInfo queue_info = {};
	queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue_info.queueFamilyIndex = 0;
	queue_info.queueCount = 1;
	queue_info.pQueuePriorities = &priority;

	const bool has_extension = device_has_extension(p_vk, p_physical_device, EXTERNAL_MEMORY_EXTENSION);
	VkDeviceCreateInfo device_info = {};
	device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	device_info.queueCreateInfoCount = 1;
	device_info.pQueueCreateInfos = &queue_info;
	device_info.enabledExtensionCount = has_extension ? 1 : 0;
	device_info.ppEnabledExtensionNames = &EXTERNAL_MEMORY_EXTENSION;

	VkDevice device = VK_NULL_HANDLE;
	ERR_FAIL_COND_V_MSG(p_vk.create_device(p_physical_device, &device_info, nullptr, &device) != VK_SUCCESS, 0,
			"Vulkan probe: vkCreateDevice failed");

	// Mirrors the launcher's external_texture_create for RenderResult's format.
	VkExternalMemoryImageCreateInfo external_info = {};
	external_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
	external_info.handleTypes = EXTERNAL_HANDLE_TYPE;

	VkImageCreateInfo image_info = {};
	image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_info.pNext = &external_info;
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
	image_info.extent = { (uint32_t)p_width, (uint32_t)p_height, 1 };
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	uint64_t size = 0;
	VkImage image = VK_NULL_HANDLE;
	if (p_vk.create_image(device, &image_info, nullptr, &image) == VK_SUCCESS) {
		VkMemoryRequirements requirements = {};
		p_vk.get_image_memory_requirements(device, image, &requirements);
		size = requirements.size;
		p_vk.destroy_image(device, image, nullptr);
	} else {
		ERR_PRINT("Vulkan probe: vkCreateImage failed");
	}

	p_vk.destroy_device(device, nullptr);
	return size;
}

} // namespace

uint64_t tg_vulkan_shared_image_allocation_size(int p_width, int p_height, const uint8_t *p_device_uuid) {
	void *library = open_vulkan_library();
	ERR_FAIL_COND_V_MSG(library == nullptr, 0, String("Vulkan probe: cannot load ") + VULKAN_LIBRARY);

	VulkanProcs vk;
	vk.get_instance_proc_addr = (PFN_vkGetInstanceProcAddr)vulkan_library_symbol(library, "vkGetInstanceProcAddr");
	const PFN_vkCreateInstance create_instance = vk.get_instance_proc_addr != nullptr
			? (PFN_vkCreateInstance)vk.get_instance_proc_addr(VK_NULL_HANDLE, "vkCreateInstance")
			: nullptr;
	if (create_instance == nullptr) {
		close_vulkan_library(library);
		ERR_FAIL_V_MSG(0, "Vulkan probe: the loader exports no vkCreateInstance");
	}

	VkApplicationInfo app_info = {};
	app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app_info.apiVersion = VK_API_VERSION_1_1;
	VkInstanceCreateInfo instance_info = {};
	instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance_info.pApplicationInfo = &app_info;

	VkInstance instance = VK_NULL_HANDLE;
	if (create_instance(&instance_info, nullptr, &instance) != VK_SUCCESS) {
		close_vulkan_library(library);
		ERR_FAIL_V_MSG(0, "Vulkan probe: vkCreateInstance failed");
	}

	uint64_t size = 0;
	if (!vk.load(instance)) {
		ERR_PRINT("Vulkan probe: Vulkan 1.1 entry points are missing");
	} else {
		const VkPhysicalDevice physical_device = find_device_by_uuid(vk, instance, p_device_uuid);
		if (physical_device != VK_NULL_HANDLE) {
			size = measure_image(vk, physical_device, p_width, p_height);
		} else {
			ERR_PRINT("Vulkan probe: no Vulkan device matches the GL context's GPU; the launcher and this renderer must share one");
		}
	}
	if (vk.destroy_instance != nullptr) {
		vk.destroy_instance(instance, nullptr);
	}

	close_vulkan_library(library);
	return size;
}
