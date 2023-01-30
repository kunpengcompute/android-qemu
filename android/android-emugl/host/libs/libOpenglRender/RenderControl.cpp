/*
* Copyright (C) 2011 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include "RenderControl.h"

#include "DispatchTables.h"
#include "FenceSync.h"
#include "FrameBuffer.h"
#include "GLESVersionDetector.h"
#include "RenderContext.h"
#include "RenderThreadInfo.h"
#include "SyncThread.h"
#include "ChecksumCalculatorThreadInfo.h"
#include "OpenGLESDispatch/EGLDispatch.h"
#include "vulkan/VkCommonOperations.h"
#include "vulkan/VkDecoderGlobalState.h"

#include "android/utils/debug.h"
#include "android/base/StringView.h"
#include "android/base/Tracing.h"
#include "emugl/common/feature_control.h"
#include "emugl/common/lazy_instance.h"
#include "emugl/common/sync_device.h"
#include "emugl/common/dma_device.h"
#include "emugl/common/misc.h"
#include "emugl/common/thread.h"
#include "math.h"

#include <atomic>
#include <inttypes.h>
#include <string.h>

using android::base::AutoLock;
using android::base::Lock;
using emugl::emugl_feature_is_enabled;
using emugl::emugl_sync_device_exists;
using emugl::emugl_sync_register_trigger_wait;

#define DEBUG_GRALLOC_SYNC 0
#define DEBUG_EGL_SYNC 0

#define RENDERCONTROL_DPRINT(...) do { \
    if (!VERBOSE_CHECK(gles)) { VERBOSE_ENABLE(gles); } \
    VERBOSE_TID_FUNCTION_DPRINT(gles, __VA_ARGS__); \
} while(0)

#if DEBUG_GRALLOC_SYNC
#define GRSYNC_DPRINT RENDERCONTROL_DPRINT
#else
#define GRSYNC_DPRINT(...)
#endif

#if DEBUG_EGL_SYNC
#define EGLSYNC_DPRINT RENDERCONTROL_DPRINT
#else
#define EGLSYNC_DPRINT(...)
#endif

// GrallocSync is a class that helps to reflect the behavior of
// grallock_lock/gralloc_unlock on the guest.
// If we don't use this, apps that use gralloc buffers (such as webcam)
// will have out of order frames,
// as GL calls from different threads in the guest
// are allowed to arrive at the host in any ordering.
class GrallocSync {
public:
    GrallocSync() {
        // Having in-order webcam frames is nice, but not at the cost
        // of potential deadlocks;
        // we need to be careful of what situations in which
        // we actually lock/unlock the gralloc color buffer.
        //
        // To avoid deadlock:
        // we require rcColorBufferCacheFlush to be called
        // whenever gralloc_lock is called on the guest,
        // and we require rcUpdateWindowColorBuffer to be called
        // whenever gralloc_unlock is called on the guest.
        //
        // Some versions of the system image optimize out
        // the call to rcUpdateWindowColorBuffer in the case of zero
        // width/height, but since we're using that as synchronization,
        // that lack of calling can lead to a deadlock on the host
        // in many situations
        // (switching camera sides, exiting benchmark apps, etc)
        // So, we put GrallocSync under the feature control.
        mEnabled = false;

        // There are two potential tricky situations to handle:
        // a. Multiple users of gralloc buffers that all want to
        // call gralloc_lock. This is obeserved to happen on older APIs
        // (<= 19).
        // b. The pipe doesn't have to preserve ordering of the
        // gralloc_lock and gralloc_unlock commands themselves.
        //
        // To handle a), notice the situation is one of one type of uses
        // needing multiple locks that needs to exclude concurrent use
        // by another type of user. This maps well to a read/write lock,
        // where gralloc_lock and gralloc_unlock users are readers
        // and rcFlushWindowColorBuffer is the writer.
        // From the perspective of the host preparing and posting
        // buffers, these are indeed read/write operations.
        //
        // To handle b), we give up on locking when the state is observed
        // to be bad. lockState tracks how many color buffer locks there are.
        // If lockState < 0, it means we definitely have an unlock before lock
        // sort of situation, and should give up.
        lockState = 0;
    }

    // lockColorBufferPrepare is designed to handle
    // gralloc_lock/unlock requests, and uses the read lock.
    // When rcFlushWindowColorBuffer is called (when frames are posted),
    // we use the write lock (see GrallocSyncPostLock).
    void lockColorBufferPrepare() {
        int newLockState = ++lockState;
        if (mEnabled && newLockState == 1) {
            mGrallocColorBufferLock.lockRead();
        } else if (mEnabled) {
            GRSYNC_DPRINT("warning: recursive/multiple locks from guest!");
        }
    }
    void unlockColorBufferPrepare() {
        int newLockState = --lockState;
        if (mEnabled && newLockState == 0) mGrallocColorBufferLock.unlockRead();
    }
    android::base::ReadWriteLock mGrallocColorBufferLock;
private:
    bool mEnabled;
    std::atomic<int> lockState;
    DISALLOW_COPY_ASSIGN_AND_MOVE(GrallocSync);
};

class GrallocSyncPostLock : public android::base::AutoWriteLock {
public:
    GrallocSyncPostLock(GrallocSync& grallocsync) :
        android::base::AutoWriteLock(grallocsync.mGrallocColorBufferLock) { }
};

static ::emugl::LazyInstance<GrallocSync> sGrallocSync = LAZY_INSTANCE_INIT;

static const GLint rendererVersion = 1;

// GLAsyncSwap version history:
// "ANDROID_EMU_NATIVE_SYNC": original version
// "ANDROIDEMU_native_sync_v2": +cleanup of sync objects
// "ANDROIDEMU_native_sync_v3": EGL_KHR_wait_sync
// "ANDROIDEMU_native_sync_v4": Correct eglGetSyncAttrib via rcIsSyncSignaled
// (We need all the different strings to not be prefixes of any other
// due to how they are checked for in the GL extensions on the guest)
static constexpr android::base::StringView kAsyncSwapStrV2 = "ANDROID_EMU_native_sync_v2";
static constexpr android::base::StringView kAsyncSwapStrV3 = "ANDROID_EMU_native_sync_v3";
static constexpr android::base::StringView kAsyncSwapStrV4 = "ANDROID_EMU_native_sync_v4";

// DMA version history:
// "ANDROID_EMU_dma_v1": add dma device and rcUpdateColorBufferDMA and do
// yv12 conversion on the GPU
// "ANDROID_EMU_dma_v2": adds DMA support glMapBufferRange (and unmap)
static constexpr android::base::StringView kDma1Str = "ANDROID_EMU_dma_v1";
static constexpr android::base::StringView kDma2Str = "ANDROID_EMU_dma_v2";
static constexpr android::base::StringView kDirectMemStr = "ANDROID_EMU_direct_mem";

// GLESDynamicVersion: up to 3.1 so far
static constexpr android::base::StringView kGLESDynamicVersion_2 = "ANDROID_EMU_gles_max_version_2";
static constexpr android::base::StringView kGLESDynamicVersion_3_0 = "ANDROID_EMU_gles_max_version_3_0";
static constexpr android::base::StringView kGLESDynamicVersion_3_1 = "ANDROID_EMU_gles_max_version_3_1";

// HWComposer Host Composition
static constexpr android::base::StringView kHostCompositionV1 = "ANDROID_EMU_host_composition_v1";
static constexpr android::base::StringView kHostCompositionV2 = "ANDROID_EMU_host_composition_v2";

static constexpr android::base::StringView kGLESNoHostError = "ANDROID_EMU_gles_no_host_error";

// Vulkan
static constexpr android::base::StringView kVulkanFeatureStr = "ANDROID_EMU_vulkan";
static constexpr android::base::StringView kDeferredVulkanCommands = "ANDROID_EMU_deferred_vulkan_commands";
static constexpr android::base::StringView kVulkanNullOptionalStrings = "ANDROID_EMU_vulkan_null_optional_strings";
static constexpr android::base::StringView kVulkanCreateResourcesWithRequirements = "ANDROID_EMU_vulkan_create_resources_with_requirements";

// treat YUV420_888 as NV21
static constexpr android::base::StringView kYUV420888toNV21 = "ANDROID_EMU_YUV420_888_to_NV21";

// Cache YUV frame
static constexpr android::base::StringView kYUVCache = "ANDROID_EMU_YUV_Cache";

// GL protocol v2
static constexpr android::base::StringView kAsyncUnmapBuffer = "ANDROID_EMU_async_unmap_buffer";
// Vulkan: Correct marshaling for ignored handles
static constexpr android::base::StringView kVulkanIgnoredHandles = "ANDROID_EMU_vulkan_ignored_handles";

// virtio-gpu-next
static constexpr android::base::StringView kVirtioGpuNext = "ANDROID_EMU_virtio_gpu_next";

// address space subdevices
static constexpr android::base::StringView kHasSharedSlotsHostMemoryAllocator = "ANDROID_EMU_has_shared_slots_host_memory_allocator";

// vulkan free memory sync
static constexpr android::base::StringView kVulkanFreeMemorySync = "ANDROID_EMU_vulkan_free_memory_sync";

// virtio-gpu native sync
static constexpr android::base::StringView kVirtioGpuNativeSync = "ANDROID_EMU_virtio_gpu_native_sync";

#ifdef __cplusplus
extern "C" {
#endif


GLint rcGetRendererVersion()
{

    sGrallocSync.ptr();
    return rendererVersion;
}

EGLint rcGetEGLVersion(EGLint* major, EGLint* minor)
{
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return EGL_FALSE;
    }
    *major = (EGLint)fb->getCaps().eglMajor;
    *minor = (EGLint)fb->getCaps().eglMinor;

    return EGL_TRUE;
}

EGLint rcQueryEGLString(EGLenum name, void* buffer, EGLint bufferSize)
{
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return 0;
    }

    const char *str = s_egl.eglQueryString(fb->getDisplay(), name);
    if (!str) {
        return 0;
    }

    std::string eglStr(str);
    if ((FrameBuffer::getMaxGLESVersion() >= GLES_DISPATCH_MAX_VERSION_3_0) &&
        false &&
        eglStr.find("EGL_KHR_create_context") == std::string::npos) {
        eglStr += "EGL_KHR_create_context ";
    }

    int len = eglStr.size() + 1;
    if (!buffer || len > bufferSize) {
        return -len;
    }

    strcpy((char *)buffer, eglStr.c_str());
    return len;
}

bool shouldEnableAsyncSwap() {
    bool isPhone;
    emugl::getAvdInfo(&isPhone, NULL);
    bool playStoreImage = emugl::emugl_feature_is_enabled(
            android::featurecontrol::PlayStoreImage);

    return emugl_feature_is_enabled(android::featurecontrol::GLAsyncSwap) &&
           emugl_sync_device_exists() && (isPhone || playStoreImage) &&
           sizeof(void*) == 8;
}

bool shouldEnableVirtioGpuNativeSync() {
    return emugl_feature_is_enabled(android::featurecontrol::VirtioGpuNativeSync);
}

bool shouldEnableHostComposition() {
    return emugl_feature_is_enabled(android::featurecontrol::HostComposition);
}

bool shouldEnableVulkan() {
    auto supportInfo =
        goldfish_vk::VkDecoderGlobalState::get()->
            getHostFeatureSupport();
    bool flagEnabled =
        emugl_feature_is_enabled(android::featurecontrol::Vulkan);
    // TODO: Restrict further to devices supporting external memory.
    return supportInfo.supportsVulkan &&
           flagEnabled;
}

bool shouldEnableDeferredVulkanCommands() {
    auto supportInfo =
        goldfish_vk::VkDecoderGlobalState::get()->
            getHostFeatureSupport();
    return supportInfo.supportsVulkan &&
           supportInfo.useDeferredCommands;
}

bool shouldEnableCreateResourcesWithRequirements() {
    auto supportInfo =
        goldfish_vk::VkDecoderGlobalState::get()->
            getHostFeatureSupport();
    return supportInfo.supportsVulkan &&
           supportInfo.useCreateResourcesWithRequirements;
}

android::base::StringView maxVersionToFeatureString(GLESDispatchMaxVersion version) {
    switch (version) {
        case GLES_DISPATCH_MAX_VERSION_2:
            return kGLESDynamicVersion_2;
        case GLES_DISPATCH_MAX_VERSION_3_0:
            return kGLESDynamicVersion_3_0;
        case GLES_DISPATCH_MAX_VERSION_3_1:
            return kGLESDynamicVersion_3_1;
        default:
            return kGLESDynamicVersion_2;
}
}

// OpenGL ES 3.x support involves changing the GL_VERSION string, which is
// assumed to be formatted in the following way:
// "OpenGL ES-CM 1.m <vendor-info>" or
// "OpenGL ES M.m <vendor-info>"
// where M is the major version number and m is minor version number.  If the
// GL_VERSION string doesn't reflect the maximum available version of OpenGL
// ES, many apps will not be able to detect support.  We need to mess with the
// version string in the first place since the underlying backend (whether it
// is Translator, SwiftShader, ANGLE, et al) may not advertise a GL_VERSION
// string reflecting their maximum capabilities.
std::string replaceESVersionString(const std::string& prev,
                                   android::base::StringView newver) {

    // There is no need to fiddle with the string
    // if we are in a ES 1.x context.
    // Such contexts are considered as a special case that must
    // be untouched.
    if (prev.find("ES-CM") != std::string::npos) {
        return prev;
    }

    size_t esStart = prev.find("ES ");
    size_t esEnd = prev.find(" ", esStart + 3);

    if (esStart == std::string::npos ||
        esEnd == std::string::npos) {
        // Account for out-of-spec version strings.
        ERR("%s: Error: invalid OpenGL ES version string %s\n",
            __func__, prev.c_str());
        return prev;
    }

    std::string res = prev.substr(0, esStart + 3);
    res += newver;
    res += prev.substr(esEnd);

    return res;
}

// If the GLES3 feature is disabled, we also want to splice out
// OpenGL extensions that should not appear in a GLES2 system.
void removeExtension(std::string& currExts, const std::string& toRemove) {
    size_t pos = currExts.find(toRemove);

    if (pos != std::string::npos)
        currExts.erase(pos, toRemove.length());
}

EGLint rcGetGLString(EGLenum name, void* buffer, EGLint bufferSize) {
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    RenderThreadInfo *tInfo = RenderThreadInfo::get();

    // whatever we end up returning,
    // it will have a terminating \0,
    // so account for it here.
    std::string glStr;

    if (tInfo && tInfo->currContext.get()) {
        const char *str = nullptr;
        if (tInfo->currContext->clientVersion() > GLESApi_CM) {
            str = (const char *)s_gles2.glGetString(name);
        }
        else {
            ERR("not support gles 1.0");
        }
        if (str) {
            glStr += str;
        }
    }

    // We add the maximum supported GL protocol number into GL_EXTENSIONS

    // filter extensions by name to match guest-side support
    GLESDispatchMaxVersion maxVersion = FrameBuffer::getMaxGLESVersion();
    if (name == GL_EXTENSIONS) {
        glStr = filterExtensionsBasedOnMaxVersion(maxVersion, glStr);
    }

    bool isChecksumEnabled = false;
    bool asyncSwapEnabled = shouldEnableAsyncSwap();
    bool virtioGpuNativeSyncEnabled = shouldEnableVirtioGpuNativeSync();
    bool dma1Enabled = false;
    bool dma2Enabled = false;
    bool directMemEnabled = false;
    bool hostCompositionEnabled = shouldEnableHostComposition();
    bool vulkanEnabled = shouldEnableVulkan();
    bool deferredVulkanCommandsEnabled =
        shouldEnableVulkan() && shouldEnableDeferredVulkanCommands();
    bool vulkanNullOptionalStringsEnabled =
        shouldEnableVulkan() && emugl_feature_is_enabled(android::featurecontrol::VulkanNullOptionalStrings);
    bool vulkanCreateResourceWithRequirementsEnabled =
        shouldEnableVulkan() && shouldEnableCreateResourcesWithRequirements();
    bool YUV420888toNV21Enabled = false;
    bool YUVCacheEnabled = false;
    bool AsyncUnmapBufferEnabled = true;
    bool vulkanIgnoredHandlesEnabled =
        shouldEnableVulkan() && emugl_feature_is_enabled(android::featurecontrol::VulkanIgnoredHandles);
    bool virtioGpuNextEnabled =
        emugl_feature_is_enabled(android::featurecontrol::VirtioGpuNext);
    bool hasSharedSlotsHostMemoryAllocatorEnabled =
        emugl_feature_is_enabled(android::featurecontrol::HasSharedSlotsHostMemoryAllocator);
    bool vulkanFreeMemorySyncEnabled =
        shouldEnableVulkan();

    if (isChecksumEnabled && name == GL_EXTENSIONS) {
        glStr += ChecksumCalculatorThreadInfo::getMaxVersionString();
        glStr += " ";
    }

    if (asyncSwapEnabled && name == GL_EXTENSIONS) {
        glStr += kAsyncSwapStrV2;
        glStr += " "; // for compatibility with older system images
        // Only enable EGL_KHR_wait_sync (and above) for host gpu.
        if (emugl::getRenderer() == SELECTED_RENDERER_HOST) {
            glStr += kAsyncSwapStrV3;
            glStr += " ";
            glStr += kAsyncSwapStrV4;
            glStr += " ";
        }
    }

    if (dma1Enabled && name == GL_EXTENSIONS) {
        glStr += kDma1Str;
        glStr += " ";
    }

    if (dma2Enabled && name == GL_EXTENSIONS) {
        glStr += kDma2Str;
        glStr += " ";
    }

    if (directMemEnabled && name == GL_EXTENSIONS) {
        glStr += kDirectMemStr;
        glStr += " ";
    }

    if (hostCompositionEnabled && name == GL_EXTENSIONS) {
        glStr += kHostCompositionV1;
        glStr += " ";
    }

    if (hostCompositionEnabled && name == GL_EXTENSIONS) {
        glStr += kHostCompositionV2;
        glStr += " ";
    }

    if (vulkanEnabled && name == GL_EXTENSIONS) {
        glStr += kVulkanFeatureStr;
        glStr += " ";
    }

    if (deferredVulkanCommandsEnabled && name == GL_EXTENSIONS) {
        glStr += kDeferredVulkanCommands;
        glStr += " ";
    }

    if (vulkanNullOptionalStringsEnabled && name == GL_EXTENSIONS) {
        glStr += kVulkanNullOptionalStrings;
        glStr += " ";
    }

    if (vulkanCreateResourceWithRequirementsEnabled && name == GL_EXTENSIONS) {
        glStr += kVulkanCreateResourcesWithRequirements;
        glStr += " ";
    }

    if (YUV420888toNV21Enabled && name == GL_EXTENSIONS) {
        glStr += kYUV420888toNV21;
        glStr += " ";
    }

    if (YUVCacheEnabled && name == GL_EXTENSIONS) {
        glStr += kYUVCache;
        glStr += " ";
    }

    if (AsyncUnmapBufferEnabled && name == GL_EXTENSIONS) {
        glStr += kAsyncUnmapBuffer;
        glStr += " ";
    }

    if (vulkanIgnoredHandlesEnabled && name == GL_EXTENSIONS) {
        glStr += kVulkanIgnoredHandles;
        glStr += " ";
    }

    if (virtioGpuNextEnabled && name == GL_EXTENSIONS) {
        glStr += kVirtioGpuNext;
        glStr += " ";
    }

    if (hasSharedSlotsHostMemoryAllocatorEnabled && name == GL_EXTENSIONS) {
        glStr += kHasSharedSlotsHostMemoryAllocator;
        glStr += " ";
    }

    if (vulkanFreeMemorySyncEnabled && name == GL_EXTENSIONS) {
        glStr += kVulkanFreeMemorySync;
        glStr += " ";
    }

    if (virtioGpuNativeSyncEnabled && name == GL_EXTENSIONS) {
        glStr += kVirtioGpuNativeSync;
        glStr += " ";
    }

    if (name == GL_EXTENSIONS) {

        GLESDispatchMaxVersion guestExtVer = GLES_DISPATCH_MAX_VERSION_2;
        if (false) {
            // If the image is in ES 3 mode, add GL_OES_EGL_image_external_essl3 for better Skia support.
            glStr += "GL_OES_EGL_image_external_essl3 ";
            guestExtVer = maxVersion;
        }

        // If we have a GLES3 implementation, add the corresponding
        // GLESv2 extensions as well.
        if (maxVersion > GLES_DISPATCH_MAX_VERSION_2) {
            glStr += "GL_OES_vertex_array_object ";
        }

        // ASTC LDR compressed texture support.
        glStr += "GL_KHR_texture_compression_astc_ldr ";

        if (false) {
            glStr += kGLESNoHostError;
            glStr += " ";
        }
        glStr += " ";
    }


    int nextBufferSize = glStr.size() + 1;

    if (!buffer || nextBufferSize > bufferSize) {
        return -nextBufferSize;
    }

    snprintf((char *)buffer, nextBufferSize, "%s", glStr.c_str());
    return nextBufferSize;
}

EGLint rcGetNumConfigs(uint32_t* p_numAttribs)
{
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    int numConfigs = 0, numAttribs = 0;

    FrameBuffer::getFB()->getConfigs()->getPackInfo(&numConfigs, &numAttribs);
    if (p_numAttribs) {
        *p_numAttribs = static_cast<uint32_t>(numAttribs);
    }
    return numConfigs;
}

EGLint rcGetConfigs(uint32_t bufSize, GLuint* buffer)
{
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    GLuint bufferSize = (GLuint)bufSize;
    return FrameBuffer::getFB()->getConfigs()->packConfigs(bufferSize, buffer);
}

EGLint rcChooseConfig(EGLint *attribs,
                             uint32_t attribs_size,
                             uint32_t *configs,
                             uint32_t configs_size)
{
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return 0;
    }

    if (attribs_size == 0) {
        if (configs && configs_size > 0) {
            // Pick the first config
            *configs = 0;
        }
    }

    return fb->getConfigs()->chooseConfig(
            attribs, (EGLint*)configs, (EGLint)configs_size, true);
}

EGLint rcGetFBParam(EGLint param)
{
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return 0;
    }

    EGLint ret = 0;

    switch(param) {
        case FB_WIDTH:
            ret = fb->getWidth();
            break;
        case FB_HEIGHT:
            ret = fb->getHeight();
            break;
        case FB_XDPI:
            ret = 72; // XXX: should be implemented
            break;
        case FB_YDPI:
            ret = 72; // XXX: should be implemented
            break;
        case FB_FPS:
            ret = 60;
            break;
        case FB_MIN_SWAP_INTERVAL:
            ret = 1; // XXX: should be implemented
            break;
        case FB_MAX_SWAP_INTERVAL:
            ret = 1; // XXX: should be implemented
            break;
        default:
            break;
    }

    return ret;
}

uint32_t rcCreateContext(uint32_t config,
                                uint32_t share, uint32_t glVersion)
{
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return 0;
    }
    uint32_t clientConfigId = fb->getMatchConfigs(config);
    INFO("Create context, config server[%d]-client[%d]", config, clientConfigId);
    HandleType ret = fb->createRenderContext(clientConfigId, share, (GLESApi)glVersion);
    return ret;
}

void rcDestroyContext(uint32_t context)
{
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return;
    }

    fb->DestroyRenderContext(context);
}

uint32_t rcCreateWindowSurface(uint32_t config,
                                      uint32_t width, uint32_t height)
{
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return 0;
    }
    uint32_t clientConfigId = fb->getMatchConfigs(config);
    INFO("Create window, config server[%d]-client[%d]", config, clientConfigId);
    return fb->createWindowSurface(clientConfigId, width, height);
}

void rcDestroyWindowSurface(uint32_t windowSurface)
{
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return;
    }

    fb->DestroyWindowSurface( windowSurface );
}

uint32_t rcCreateColorBuffer(uint32_t width,
                                    uint32_t height, GLenum internalFormat)
{
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return 0;
    }

    return fb->createColorBuffer(width, height, internalFormat,
                                 FRAMEWORK_FORMAT_GL_COMPATIBLE);
}

uint32_t rcCreateColorBufferDMA(uint32_t width,
                                       uint32_t height, GLenum internalFormat,
                                       int frameworkFormat)
{
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return 0;
    }

    return fb->createColorBuffer(width, height, internalFormat,
                                 (FrameworkFormat)frameworkFormat);
}

int rcOpenColorBuffer2(uint32_t colorbuffer)
{
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return -1;
    }
    return fb->openColorBuffer( colorbuffer );
}

void rcOpenColorBuffer(uint32_t colorbuffer)
{
    (void) rcOpenColorBuffer2(colorbuffer);
}

void rcCloseColorBuffer(uint32_t colorbuffer)
{
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return;
    }
    fb->closeColorBuffer( colorbuffer );
}

int rcFlushWindowColorBuffer(uint32_t windowSurface)
{
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();

    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        GRSYNC_DPRINT("unlock gralloc cb lock");
        return -1;
    }

    // Update from Vulkan if necessary
    goldfish_vk::updateColorBufferFromVkImage(
        fb->getWindowSurfaceColorBufferHandle(windowSurface));

    if (!fb->flushWindowSurfaceColorBuffer(windowSurface, nullptr, 0)) {
        GRSYNC_DPRINT("unlock gralloc cb lock }");
        return -1;
    }

    // Update to Vulkan if necessary
    goldfish_vk::updateVkImageFromColorBuffer(
        fb->getWindowSurfaceColorBufferHandle(windowSurface));

    return 0;
}

// Note that even though this calls rcFlushWindowColorBuffer,
// the "Async" part is in the return type, which is void
// versus return type int for rcFlushWindowColorBuffer.
//
// The different return type, even while calling the same
// functions internally, will end up making the encoder
// and decoder use a different protocol. This is because
// the encoder generally obeys the following conventions:
//
// - The encoder will immediately send and wait for a command
//   result if the return type is not void.
// - The encoder will cache the command in a buffer and send
//   at a convenient time if the return type is void.
//
// It can also be expensive performance-wise to trigger
// sending traffic back to the guest. Generally, the more we avoid
// encoding commands that perform two-way traffic, the better.
//
// Hence, |rcFlushWindowColorBufferAsync| will avoid extra traffic;
// with return type void,
// the guest will not wait until this function returns,
// nor will it immediately send the command,
// resulting in more asynchronous behavior.
void rcFlushWindowColorBufferAsync(uint32_t windowSurface, EGLint *rects, EGLint rectsNum)
{
    (void) rects;
    (void) rectsNum;
    rcFlushWindowColorBuffer(windowSurface);
}

void rcSetWindowColorBuffer(uint32_t windowSurface,
                                   uint32_t colorBuffer)
{
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return;
    }
    fb->setWindowSurfaceColorBuffer(windowSurface, colorBuffer);
}

EGLint rcMakeCurrent(uint32_t context,
                            uint32_t drawSurf, uint32_t readSurf)
{
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return EGL_FALSE;
    }

    bool ret = fb->bindContext(context, drawSurf, readSurf);

    return (ret ? EGL_TRUE : EGL_FALSE);
}

void rcFBPost(uint32_t colorBuffer)
{
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return;
    }

    // Update from Vulkan if necessary
    goldfish_vk::updateColorBufferFromVkImage(colorBuffer);

    fb->post(colorBuffer);
}

void rcFBSetSwapInterval(EGLint interval)
{
   // XXX: TBD - should be implemented
}

void rcBindTexture(uint32_t colorBuffer, uint32_t isPostColorbuffer)
{
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return;
    }
    // Update from Vulkan if necessary
    goldfish_vk::updateColorBufferFromVkImage(colorBuffer);

    fb->bindColorBufferToTexture(colorBuffer, isPostColorbuffer);
}

void rcBindRenderbuffer(uint32_t colorBuffer)
{
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return;
    }

    // Update from Vulkan if necessary
    goldfish_vk::updateColorBufferFromVkImage(colorBuffer);

    fb->bindColorBufferToRenderbuffer(colorBuffer);
}

EGLint rcColorBufferCacheFlush(uint32_t colorBuffer,
                                      EGLint postCount, int forRead)
{
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    // gralloc_lock() on the guest calls rcColorBufferCacheFlush
    GRSYNC_DPRINT("waiting for gralloc cb lock");
    sGrallocSync->lockColorBufferPrepare();
    GRSYNC_DPRINT("lock gralloc cb lock {");
    return 0;
}

void rcReadColorBuffer(uint32_t colorBuffer,
                              GLint x, GLint y,
                              GLint width, GLint height,
                              GLenum format, GLenum type, void* pixels)
{
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return;
    }

    // Update from Vulkan if necessary
    goldfish_vk::updateColorBufferFromVkImage(colorBuffer);

    fb->readColorBuffer(colorBuffer, x, y, width, height, format, type, pixels);
}

int rcUpdateColorBuffer(uint32_t colorBuffer,
                               GLint x, GLint y,
                               GLint width, GLint height,
                               GLenum format, GLenum type, void* pixels)
{
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    FrameBuffer *fb = FrameBuffer::getFB();

    if (!fb) {
        GRSYNC_DPRINT("unlock gralloc cb lock");
        sGrallocSync->unlockColorBufferPrepare();
        return -1;
    }

    // Since this is a modify operation, also read the current contents
    // of the VkImage, if any.
    goldfish_vk::updateColorBufferFromVkImage(colorBuffer);

    fb->updateColorBuffer(colorBuffer, x, y, width, height, format, type, pixels);

    GRSYNC_DPRINT("unlock gralloc cb lock");
    sGrallocSync->unlockColorBufferPrepare();

    // Update to Vulkan if necessary
    goldfish_vk::updateVkImageFromColorBuffer(colorBuffer);

    return 0;
}

int rcUpdateColorBufferDMA(uint32_t colorBuffer,
                                  GLint x, GLint y,
                                  GLint width, GLint height,
                                  GLenum format, GLenum type,
                                  void* pixels, uint32_t pixels_size)
{
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    FrameBuffer *fb = FrameBuffer::getFB();

    if (!fb) {
        GRSYNC_DPRINT("unlock gralloc cb lock");
        sGrallocSync->unlockColorBufferPrepare();
        return -1;
    }

    // Since this is a modify operation, also read the current contents
    // of the VkImage, if any.
    goldfish_vk::updateColorBufferFromVkImage(colorBuffer);

    fb->updateColorBuffer(colorBuffer, x, y, width, height,
                          format, type, pixels);

    GRSYNC_DPRINT("unlock gralloc cb lock");
    sGrallocSync->unlockColorBufferPrepare();

    // Update to Vulkan if necessary
    goldfish_vk::updateVkImageFromColorBuffer(colorBuffer);

    return 0;
}

void* rcCreateClientImage(uint32_t context, EGLenum target, GLuint buffer)
{
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return 0;
    }

    return fb->createClientImage(context, target, buffer);
}

int rcDestroyClientImage(void* image)
{
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return 0;
    }

    return fb->destroyClientImage(image);
}

void rcSelectChecksumHelper(uint32_t protocol, uint32_t reserved) {
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    ChecksumCalculatorThreadInfo::setVersion(protocol);
}

// |rcTriggerWait| is called from the goldfish sync
// kernel driver whenever a native fence fd is created.
// We will then need to use the host to find out
// when to signal that native fence fd. We use
// SyncThread for that.
void rcTriggerWait(uint64_t eglsync_ptr,
                          uint64_t thread_ptr,
                          uint64_t timeline) {
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    if (thread_ptr == 1) {
        // Is vulkan sync fd;
        // just signal right away for now
        SyncThread::get()->triggerWait(0, timeline);
    }

    FenceSync* fenceSync = (FenceSync*)(uintptr_t)eglsync_ptr;
    EGLSYNC_DPRINT("eglsync=0x%llx "
                   "fenceSync=%p "
                   "thread_ptr=0x%llx "
                   "timeline=0x%llx",
                   eglsync_ptr, fenceSync, thread_ptr, timeline);
    SyncThread::get()->triggerWait(fenceSync, timeline);
}

// |rcCreateSyncKHR| implements the guest's |eglCreateSyncKHR| by calling the
// host's implementation of |eglCreateSyncKHR|. A SyncThread is also notified
// for purposes of signaling any native fence fd's that get created in the
// guest off the sync object created here.
void rcCreateSyncKHR(EGLenum type,
                            EGLint* attribs,
                            uint32_t num_attribs,
                            int destroy_when_signaled,
                            uint64_t* eglsync_out,
                            uint64_t* syncthread_out) {
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    EGLSYNC_DPRINT("type=0x%x num_attribs=%d",
            type, num_attribs);

    bool hasNativeFence =
        type == EGL_SYNC_NATIVE_FENCE_ANDROID;

    // Usually we expect rcTriggerWait to be registered
    // at the beginning in rcGetRendererVersion, called
    // on init for all contexts.
    // But if we are loading from snapshot, that's not
    // guaranteed, and we need to make sure
    // rcTriggerWait is registered.
    FenceSync* fenceSync = new FenceSync(hasNativeFence,
                                         destroy_when_signaled);

    // This MUST be present, or we get a deadlock effect.
    s_gles2.glFlush();

    if (syncthread_out) *syncthread_out =
        reinterpret_cast<uint64_t>(SyncThread::get());

    if (eglsync_out) {
        uint64_t res = (uint64_t)(uintptr_t)fenceSync;
        *eglsync_out = res;
        EGLSYNC_DPRINT("send out eglsync 0x%llx", res);
    }
}

// |rcClientWaitSyncKHR| implements |eglClientWaitSyncKHR|
// on the guest through using the host's existing
// |eglClientWaitSyncKHR| implementation, which is done
// through the FenceSync object.
EGLint rcClientWaitSyncKHR(uint64_t handle,
                                  EGLint flags,
                                  uint64_t timeout) {
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    RenderThreadInfo *tInfo = RenderThreadInfo::get();
    FrameBuffer *fb = FrameBuffer::getFB();

    EGLSYNC_DPRINT("handle=0x%lx flags=0x%x timeout=%" PRIu64,
                handle, flags, timeout);

    FenceSync* fenceSync = FenceSync::getFromHandle(handle);

    if (!fenceSync) {
        EGLSYNC_DPRINT("fenceSync null, return condition satisfied");
        return EGL_CONDITION_SATISFIED_KHR;
    }

    // Sometimes a gralloc-buffer-only thread is doing stuff with sync.
    // This happens all the time with YouTube videos in the browser.
    // In this case, create a context on the host just for syncing.
    if (!tInfo->currContext) {
        uint32_t gralloc_sync_cxt, gralloc_sync_surf;
        fb->createTrivialContext(0, // There is no context to share.
                                 &gralloc_sync_cxt,
                                 &gralloc_sync_surf);
        fb->bindContext(gralloc_sync_cxt,
                        gralloc_sync_surf,
                        gralloc_sync_surf);
        // This context is then cleaned up when the render thread exits.
    }

    return fenceSync->wait(timeout);
}

void rcWaitSyncKHR(uint64_t handle,
                                  EGLint flags) {
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    RenderThreadInfo *tInfo = RenderThreadInfo::get();
    FrameBuffer *fb = FrameBuffer::getFB();

    EGLSYNC_DPRINT("handle=0x%lx flags=0x%x", handle, flags);

    FenceSync* fenceSync = FenceSync::getFromHandle(handle);

    if (!fenceSync) { return; }

    // Sometimes a gralloc-buffer-only thread is doing stuff with sync.
    // This happens all the time with YouTube videos in the browser.
    // In this case, create a context on the host just for syncing.
    if (!tInfo->currContext) {
        uint32_t gralloc_sync_cxt, gralloc_sync_surf;
        fb->createTrivialContext(0, // There is no context to share.
                                 &gralloc_sync_cxt,
                                 &gralloc_sync_surf);
        fb->bindContext(gralloc_sync_cxt,
                        gralloc_sync_surf,
                        gralloc_sync_surf);
        // This context is then cleaned up when the render thread exits.
    }

    fenceSync->waitAsync();
}

int rcDestroySyncKHR(uint64_t handle) {
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    FenceSync* fenceSync = FenceSync::getFromHandle(handle);
    if (!fenceSync) return 0;
    fenceSync->decRef();
    return 0;
}

void rcSetPuid(uint64_t puid) {
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    RenderThreadInfo *tInfo = RenderThreadInfo::get();
    tInfo->m_puid = puid;
}

int rcCompose(uint32_t bufferSize, void* buffer) {
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return -1;
    }
    return fb->compose(bufferSize, buffer);
}

int rcCreateDisplay(uint32_t* displayId) {
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return -1;
    }

    // Assume this API call always allocates a new displayId
    *displayId = FrameBuffer::s_invalidIdMultiDisplay;
    return fb->createDisplay(displayId);
}

int rcDestroyDisplay(uint32_t displayId) {
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return -1;
    }

    return fb->destroyDisplay(displayId);
}

int rcSetDisplayColorBuffer(uint32_t displayId, uint32_t colorBuffer) {
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return -1;
    }

    return fb->setDisplayColorBuffer(displayId, colorBuffer);
}

int rcGetDisplayColorBuffer(uint32_t displayId, uint32_t* colorBuffer) {
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return -1;
    }

    return fb->getDisplayColorBuffer(displayId, colorBuffer);
}

int rcGetColorBufferDisplay(uint32_t colorBuffer, uint32_t* displayId) {
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return -1;
    }

    return fb->getColorBufferDisplay(colorBuffer, displayId);
}

int rcGetDisplayPose(uint32_t displayId,
                            int32_t* x,
                            int32_t* y,
                            uint32_t* w,
                            uint32_t* h) {
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return -1;
    }

    return fb->getDisplayPose(displayId, x, y, w, h);
}

int rcSetDisplayPose(uint32_t displayId,
                            int32_t x,
                            int32_t y,
                            uint32_t w,
                            uint32_t h) {
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return -1;
    }

    return fb->setDisplayPose(displayId, x, y, w, h);
}

static int rcSetColorBufferVulkanMode(uint32_t colorBuffer, uint32_t mode) {
    if (!goldfish_vk::isColorBufferVulkanCompatible(colorBuffer)) {
        ERR("%s: error: colorBuffer 0x%x is not Vulkan compatible\n",
            __func__, colorBuffer);
        return -1;
    }

#define VULKAN_MODE_VULKAN_ONLY 1

    bool modeIsVulkanOnly = mode == VULKAN_MODE_VULKAN_ONLY;

    if (!goldfish_vk::setupVkColorBuffer(colorBuffer, modeIsVulkanOnly)) {
        ERR("%s: error: failed to create VkImage for colorBuffer 0x%x\n",
            __func__, colorBuffer);
        return -1;
    }

    if (!goldfish_vk::setColorBufferVulkanMode(colorBuffer, mode)) {
        ERR("%s: error: failed to set Vulkan mode for colorBuffer 0x%x\n",
            __func__, colorBuffer);
        return -1;
    }

    return 0;
}

void rcReadColorBufferYUV(uint32_t colorBuffer,
                                GLint x, GLint y,
                                GLint width, GLint height,
                                void* pixels, uint32_t pixels_size)
{
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return;
    }

    fb->readColorBufferYUV(colorBuffer, x, y, width, height, pixels, pixels_size);
}

int rcIsSyncSignaled(uint64_t handle) {
    FenceSync* fenceSync = FenceSync::getFromHandle(handle);
    if (!fenceSync) return 1; // assume destroyed => signaled
    return fenceSync->isSignaled() ? 1 : 0;
}

void rcCreateColorBufferWithHandle(
    uint32_t width, uint32_t height, GLenum internalFormat, uint32_t handle)
{
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    FrameBuffer *fb = FrameBuffer::getFB();

    if (!fb) {
        return;
    }

    fb->createColorBufferWithHandle(
        width, height, internalFormat,
        FRAMEWORK_FORMAT_GL_COMPATIBLE, handle);
}

void rcPushProcessName(const char* procName, uint32_t processNameSize)
{
    if (procName == nullptr || processNameSize <= 0) {
        ERR("proceess name is null, size is %u", processNameSize);
    }
    FrameBuffer::getFB()->SetProcNameOfThread(procName, processNameSize);
}

bool rcUpdateYuv(uint32_t colorBuffer,
                        GLint x, GLint y,
                        GLint width, GLint height,
                        GLenum format, GLenum type, int32_t yuvFormat, void* pixels)
{
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return false;
    }

    return fb->updateYuv(colorBuffer, x, y, width, height, format, type, yuvFormat, pixels);
}

#ifdef __cplusplus
}
#endif
#ifndef VMICLIENT
void initRenderControlContext(renderControl_decoder_context_t *dec)
{
    dec->rcGetRendererVersion = rcGetRendererVersion;
    dec->rcGetEGLVersion = rcGetEGLVersion;
    dec->rcQueryEGLString = rcQueryEGLString;
    dec->rcGetGLString = rcGetGLString;
    dec->rcGetNumConfigs = rcGetNumConfigs;
    dec->rcGetConfigs = rcGetConfigs;
    dec->rcChooseConfig = rcChooseConfig;
    dec->rcGetFBParam = rcGetFBParam;
    dec->rcCreateContext = rcCreateContext;
    dec->rcDestroyContext = rcDestroyContext;
    dec->rcCreateWindowSurface = rcCreateWindowSurface;
    dec->rcDestroyWindowSurface = rcDestroyWindowSurface;
    dec->rcCreateColorBuffer = rcCreateColorBuffer;
    dec->rcOpenColorBuffer = rcOpenColorBuffer;
    dec->rcCloseColorBuffer = rcCloseColorBuffer;
    dec->rcSetWindowColorBuffer = rcSetWindowColorBuffer;
    dec->rcFlushWindowColorBuffer = rcFlushWindowColorBuffer;
    dec->rcMakeCurrent = rcMakeCurrent;
    dec->rcFBPost = rcFBPost;
    dec->rcFBSetSwapInterval = rcFBSetSwapInterval;
    dec->rcBindTexture = rcBindTexture;
    dec->rcBindRenderbuffer = rcBindRenderbuffer;
    dec->rcColorBufferCacheFlush = rcColorBufferCacheFlush;
    dec->rcReadColorBuffer = rcReadColorBuffer;
    dec->rcUpdateColorBuffer = rcUpdateColorBuffer;
    dec->rcOpenColorBuffer2 = rcOpenColorBuffer2;
    dec->rcCreateClientImage = rcCreateClientImage;
    dec->rcDestroyClientImage = rcDestroyClientImage;
    dec->rcSelectChecksumHelper = rcSelectChecksumHelper;
    dec->rcCreateSyncKHR = rcCreateSyncKHR;
    dec->rcClientWaitSyncKHR = rcClientWaitSyncKHR;
    dec->rcFlushWindowColorBufferAsync = rcFlushWindowColorBufferAsync;
    dec->rcDestroySyncKHR = rcDestroySyncKHR;
    dec->rcSetPuid = rcSetPuid;
    dec->rcUpdateColorBufferDMA = rcUpdateColorBufferDMA;
    dec->rcCreateColorBufferDMA = rcCreateColorBufferDMA;
    dec->rcWaitSyncKHR = rcWaitSyncKHR;
    dec->rcCompose = rcCompose;
    dec->rcCreateDisplay = rcCreateDisplay;
    dec->rcDestroyDisplay = rcDestroyDisplay;
    dec->rcSetDisplayColorBuffer = rcSetDisplayColorBuffer;
    dec->rcGetDisplayColorBuffer = rcGetDisplayColorBuffer;
    dec->rcGetColorBufferDisplay = rcGetColorBufferDisplay;
    dec->rcGetDisplayPose = rcGetDisplayPose;
    dec->rcSetDisplayPose = rcSetDisplayPose;
    dec->rcSetColorBufferVulkanMode = rcSetColorBufferVulkanMode;
    dec->rcReadColorBufferYUV = rcReadColorBufferYUV;
    dec->rcIsSyncSignaled = rcIsSyncSignaled;
    dec->rcCreateColorBufferWithHandle = rcCreateColorBufferWithHandle;
}
#endif
