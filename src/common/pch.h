#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define GLFW_EXPOSE_NATIVE_WIN32
#include <windows.h>
#include <mmsystem.h>
#endif

#include <iostream>
#include <memory>
#include <optional>
#include <chrono>
#include <thread>
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <set>
#include <array>
#include <functional>
#include <deque>
#include <queue>
#include <cmath>
#include <numeric>
#include <random>
#include <cstdint>#include <mutex>
#include <utility>
#include <unordered_set>#include <unordered_map>
#include <atomic>
#include <algorithm>
#include <variant>

#include "Vulkan/vulkan.h"
#include "../renderer/backend/VulkanExtensionFunctions.h"
#include "glfw/glfw3.h"
#include "fmt/core.h"
#include <vma/vk_mem_alloc.h>
#include <vulkan/vk_enum_string_helper.h>
#include <stb_image/stb_image.h>
#include "enkiTS/TaskScheduler.h"
#include <meshoptimizer.h>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>
#endif

#define FASTGLTF_ENABLE_GLMC
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/util.hpp>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include "common/glm_common.hpp"
