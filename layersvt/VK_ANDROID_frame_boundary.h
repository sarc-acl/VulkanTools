/* Copyright (c) 2026 The Khronos Group Inc.
 * Copyright (c) 2026 Valve Corporation
 * Copyright (c) 2026 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

// VK_ANDROID_frame_boundary is not present in the official Vulkan registry (vk.xml), so nothing in
// the generator pipeline knows about it and no VkLayerDispatchTable slot exists for it - Android's
// own headers do not declare it either.
#include "vulkan/vulkan.h"

typedef void(VKAPI_PTR *PFN_vkFrameBoundaryANDROID)(VkDevice device, VkSemaphore semaphore, VkImage image);
