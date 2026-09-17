#ifndef TG_VULKAN_MEMORY_PROBE_H
#define TG_VULKAN_MEMORY_PROBE_H

#include "core/typedefs.h"

// Size of the dedicated allocation the launcher makes for its shared image, as
// the Vulkan device whose deviceUUID is p_device_uuid reports it. The launcher
// creates the image in RenderResult.create_external_texture and allocates it
// with VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT, so the size is the image's
// VkMemoryRequirements::size. Returns 0 when it cannot be measured.
uint64_t tg_vulkan_shared_image_allocation_size(int p_width, int p_height, const uint8_t *p_device_uuid);

#endif // TG_VULKAN_MEMORY_PROBE_H
