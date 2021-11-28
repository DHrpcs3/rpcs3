#pragma once

#include "Utilities/geometry.h"
#include "util/types.hpp"
#include "util/atomic.hpp"
#include <vector>
#include <vulkan/vulkan_core.h>

class GSFrameBase
{
public:
	GSFrameBase() = default;
	GSFrameBase(const GSFrameBase&) = delete;
	virtual ~GSFrameBase() = default;

	virtual std::vector<const char *> required_instance_extensions() const = 0;
	virtual VkSurfaceKHR create_surface(VkInstance instance) = 0;
	virtual sizeu client_size() const = 0;
	virtual double client_device_pixel_ratio() const = 0;
};
