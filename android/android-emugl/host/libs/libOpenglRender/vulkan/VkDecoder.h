// Copyright (C) 2018 The Android Open Source Project
// Copyright (C) 2018 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Autogenerated module VkDecoder
// (header) generated by android/android-emugl/host/libs/libOpenglRender/vulkan-registry/xml/genvk.py -registry android/android-emugl/host/libs/libOpenglRender/vulkan-registry/xml/vk.xml cereal -o android/android-emugl/host/libs/libOpenglRender/vulkan/cereal
// Please do not modify directly;
// re-run android/scripts/generate-vulkan-sources.sh,
// or directly from Python by defining:
// VULKAN_REGISTRY_XML_DIR : Directory containing genvk.py and vk.xml
// CEREAL_OUTPUT_DIR: Where to put the generated sources.
// python3 $VULKAN_REGISTRY_XML_DIR/genvk.py -registry $VULKAN_REGISTRY_XML_DIR/vk.xml cereal -o $CEREAL_OUTPUT_DIR

#pragma once

#include <vulkan/vulkan.h>


#include <memory>

namespace android {
namespace base {
class Pool;
} // namespace android
} // namespace base







class IOStream;

class VkDecoder {
public:
    VkDecoder();
    ~VkDecoder();
    void setForSnapshotLoad(bool forSnapshotLoad);
    size_t decode(void* buf, size_t bufsize, IOStream* stream);
private:
    class Impl;
    std::unique_ptr<Impl> mImpl;
};
#ifdef VK_VERSION_1_0
#endif
#ifdef VK_VERSION_1_1
#endif
#ifdef VK_KHR_surface
#endif
#ifdef VK_KHR_swapchain
#endif
#ifdef VK_KHR_display
#endif
#ifdef VK_KHR_display_swapchain
#endif
#ifdef VK_KHR_xlib_surface
#endif
#ifdef VK_KHR_xcb_surface
#endif
#ifdef VK_KHR_wayland_surface
#endif
#ifdef VK_KHR_mir_surface
#endif
#ifdef VK_KHR_android_surface
#endif
#ifdef VK_KHR_win32_surface
#endif
#ifdef VK_KHR_sampler_mirror_clamp_to_edge
#endif
#ifdef VK_KHR_multiview
#endif
#ifdef VK_KHR_get_physical_device_properties2
#endif
#ifdef VK_KHR_device_group
#endif
#ifdef VK_KHR_shader_draw_parameters
#endif
#ifdef VK_KHR_maintenance1
#endif
#ifdef VK_KHR_device_group_creation
#endif
#ifdef VK_KHR_external_memory_capabilities
#endif
#ifdef VK_KHR_external_memory
#endif
#ifdef VK_KHR_external_memory_win32
#endif
#ifdef VK_KHR_external_memory_fd
#endif
#ifdef VK_KHR_win32_keyed_mutex
#endif
#ifdef VK_KHR_external_semaphore_capabilities
#endif
#ifdef VK_KHR_external_semaphore
#endif
#ifdef VK_KHR_external_semaphore_win32
#endif
#ifdef VK_KHR_external_semaphore_fd
#endif
#ifdef VK_KHR_push_descriptor
#endif
#ifdef VK_KHR_16bit_storage
#endif
#ifdef VK_KHR_incremental_present
#endif
#ifdef VK_KHR_descriptor_update_template
#endif
#ifdef VK_KHR_create_renderpass2
#endif
#ifdef VK_KHR_shared_presentable_image
#endif
#ifdef VK_KHR_external_fence_capabilities
#endif
#ifdef VK_KHR_external_fence
#endif
#ifdef VK_KHR_external_fence_win32
#endif
#ifdef VK_KHR_external_fence_fd
#endif
#ifdef VK_KHR_maintenance2
#endif
#ifdef VK_KHR_get_surface_capabilities2
#endif
#ifdef VK_KHR_variable_pointers
#endif
#ifdef VK_KHR_get_display_properties2
#endif
#ifdef VK_KHR_dedicated_allocation
#endif
#ifdef VK_KHR_storage_buffer_storage_class
#endif
#ifdef VK_KHR_relaxed_block_layout
#endif
#ifdef VK_KHR_get_memory_requirements2
#endif
#ifdef VK_KHR_image_format_list
#endif
#ifdef VK_KHR_sampler_ycbcr_conversion
#endif
#ifdef VK_KHR_bind_memory2
#endif
#ifdef VK_KHR_maintenance3
#endif
#ifdef VK_KHR_draw_indirect_count
#endif
#ifdef VK_KHR_8bit_storage
#endif
#ifdef VK_ANDROID_native_buffer
#endif
#ifdef VK_EXT_debug_report
#endif
#ifdef VK_NV_glsl_shader
#endif
#ifdef VK_EXT_depth_range_unrestricted
#endif
#ifdef VK_IMG_filter_cubic
#endif
#ifdef VK_AMD_rasterization_order
#endif
#ifdef VK_AMD_shader_trinary_minmax
#endif
#ifdef VK_AMD_shader_explicit_vertex_parameter
#endif
#ifdef VK_EXT_debug_marker
#endif
#ifdef VK_AMD_gcn_shader
#endif
#ifdef VK_NV_dedicated_allocation
#endif
#ifdef VK_AMD_draw_indirect_count
#endif
#ifdef VK_AMD_negative_viewport_height
#endif
#ifdef VK_AMD_gpu_shader_half_float
#endif
#ifdef VK_AMD_shader_ballot
#endif
#ifdef VK_AMD_texture_gather_bias_lod
#endif
#ifdef VK_AMD_shader_info
#endif
#ifdef VK_AMD_shader_image_load_store_lod
#endif
#ifdef VK_IMG_format_pvrtc
#endif
#ifdef VK_NV_external_memory_capabilities
#endif
#ifdef VK_NV_external_memory
#endif
#ifdef VK_NV_external_memory_win32
#endif
#ifdef VK_NV_win32_keyed_mutex
#endif
#ifdef VK_EXT_validation_flags
#endif
#ifdef VK_NN_vi_surface
#endif
#ifdef VK_EXT_shader_subgroup_ballot
#endif
#ifdef VK_EXT_shader_subgroup_vote
#endif
#ifdef VK_EXT_conditional_rendering
#endif
#ifdef VK_NVX_device_generated_commands
#endif
#ifdef VK_NV_clip_space_w_scaling
#endif
#ifdef VK_EXT_direct_mode_display
#endif
#ifdef VK_EXT_acquire_xlib_display
#endif
#ifdef VK_EXT_display_surface_counter
#endif
#ifdef VK_EXT_display_control
#endif
#ifdef VK_GOOGLE_display_timing
#endif
#ifdef VK_NV_sample_mask_override_coverage
#endif
#ifdef VK_NV_geometry_shader_passthrough
#endif
#ifdef VK_NV_viewport_array2
#endif
#ifdef VK_NVX_multiview_per_view_attributes
#endif
#ifdef VK_NV_viewport_swizzle
#endif
#ifdef VK_EXT_discard_rectangles
#endif
#ifdef VK_EXT_conservative_rasterization
#endif
#ifdef VK_EXT_swapchain_colorspace
#endif
#ifdef VK_EXT_hdr_metadata
#endif
#ifdef VK_MVK_ios_surface
#endif
#ifdef VK_MVK_macos_surface
#endif
#ifdef VK_EXT_external_memory_dma_buf
#endif
#ifdef VK_EXT_queue_family_foreign
#endif
#ifdef VK_EXT_debug_utils
#endif
#ifdef VK_ANDROID_external_memory_android_hardware_buffer
#endif
#ifdef VK_EXT_sampler_filter_minmax
#endif
#ifdef VK_AMD_gpu_shader_int16
#endif
#ifdef VK_AMD_mixed_attachment_samples
#endif
#ifdef VK_AMD_shader_fragment_mask
#endif
#ifdef VK_EXT_shader_stencil_export
#endif
#ifdef VK_EXT_sample_locations
#endif
#ifdef VK_EXT_blend_operation_advanced
#endif
#ifdef VK_NV_fragment_coverage_to_color
#endif
#ifdef VK_NV_framebuffer_mixed_samples
#endif
#ifdef VK_NV_fill_rectangle
#endif
#ifdef VK_EXT_post_depth_coverage
#endif
#ifdef VK_EXT_validation_cache
#endif
#ifdef VK_EXT_descriptor_indexing
#endif
#ifdef VK_EXT_shader_viewport_index_layer
#endif
#ifdef VK_EXT_global_priority
#endif
#ifdef VK_EXT_external_memory_host
#endif
#ifdef VK_AMD_buffer_marker
#endif
#ifdef VK_AMD_shader_core_properties
#endif
#ifdef VK_EXT_vertex_attribute_divisor
#endif
#ifdef VK_NV_shader_subgroup_partitioned
#endif
#ifdef VK_NV_device_diagnostic_checkpoints
#endif
#ifdef VK_GOOGLE_address_space
#endif
#ifdef VK_GOOGLE_color_buffer
#endif
#ifdef VK_GOOGLE_sized_descriptor_update_template
#endif
#ifdef VK_GOOGLE_async_command_buffers
#endif
#ifdef VK_GOOGLE_create_resources_with_requirements
#endif
#ifdef VK_GOOGLE_address_space_info
#endif
#ifdef VK_GOOGLE_free_memory_sync
#endif


