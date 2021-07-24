/*
* Copyright (C) 2011-2015 The Android Open Source Project
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

#include "FrameBuffer.h"

#include "DispatchTables.h"
#include "GLESVersionDetector.h"
#include "NativeSubWindow.h"
#include "RenderControl.h"
#include "RenderThreadInfo.h"
#include "YUVConverter.h"
#include "OpenGLESDispatch/EGLDispatch.h"
#include "android/base/LayoutResolver.h"
#include "android/base/CpuUsage.h"
#include "android/base/containers/Lookup.h"
#include "android/base/files/StreamSerializing.h"
#include "android/base/memory/LazyInstance.h"
#include "android/base/memory/MemoryTracker.h"
#include "android/base/memory/ScopedPtr.h"
#include "android/base/system/System.h"

#include "emugl/common/crash_reporter.h"
#include "emugl/common/feature_control.h"
#include "emugl/common/logging.h"
#include "emugl/common/misc.h"
#include "emugl/common/vm_operations.h"

#include <stdio.h>
#include <string.h>

using android::base::AutoLock;
using android::base::LazyInstance;
using android::base::Stream;
using android::base::System;
using android::base::WorkerProcessingResult;

namespace {

// Helper class to call the bind_locked() / unbind_locked() properly.
typedef ColorBuffer::RecursiveScopedHelperContext ScopedBind;

// Implementation of a ColorBuffer::Helper instance that redirects calls
// to a FrameBuffer instance.
class ColorBufferHelper : public ColorBuffer::Helper {
public:
    ColorBufferHelper(FrameBuffer* fb) : mFb(fb) {}

    virtual bool setupContext() {
        mIsBound = mFb->bind_locked();
        return mIsBound;
    }

    virtual void teardownContext() {
        mFb->unbind_locked();
        mIsBound = false;
    }

    virtual TextureDraw* getTextureDraw() const {
        return mFb->getTextureDraw();
    }

    virtual bool isBound() const { return mIsBound; }

	virtual std::shared_ptr<YuvDraw<GLESv2Dispatch *>> getYuvDraw() const {
        return mFb->getYuvDraw();
    }
private:
    FrameBuffer* mFb;
    bool mIsBound = false;
};

}  // namespace

FrameBuffer* FrameBuffer::s_theFrameBuffer = NULL;
HandleType FrameBuffer::s_nextHandle = 0;
float FrameBuffer::m_zRot = 0;
static const GLint gles2ContextAttribsESOrGLCompat[] =
   { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };

static const GLint gles2ContextAttribsCoreGL[] =
   { EGL_CONTEXT_CLIENT_VERSION, 2,
     EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
     EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
     EGL_NONE };

static const GLint gles3ContextAttribsESOrGLCompat[] =
   { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };

static const GLint gles3ContextAttribsCoreGL[] =
   { EGL_CONTEXT_CLIENT_VERSION, 3,
     EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
     EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
     EGL_NONE };

const GLint* getGlesMaxContextAttribs() {
        return gles3ContextAttribsESOrGLCompat;
    }

bool g_isBasePhone = false;
static char* getGLES2ExtensionString(EGLDisplay p_dpy) {
    EGLConfig config;
    EGLSurface surface;

    static const GLint configAttribs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                          EGL_RENDERABLE_TYPE,
                                          EGL_OPENGL_ES2_BIT, EGL_NONE};

    int n;
    if (!s_egl.eglChooseConfig(p_dpy, configAttribs, &config, 1, &n) ||
        n == 0) {
        GL_LOG("Could not find GLES 2.x config!");
        ERR("%s: Could not find GLES 2.x config!\n", __FUNCTION__);
        return NULL;
    }

    static const EGLint pbufAttribs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};

    surface = s_egl.eglCreatePbufferSurface(p_dpy, config, pbufAttribs);
    if (surface == EGL_NO_SURFACE) {
        GL_LOG("Could not create GLES 2.x Pbuffer!");
        ERR("%s: Could not create GLES 2.x Pbuffer!\n", __FUNCTION__);
        return NULL;
    }

    EGLContext ctx = s_egl.eglCreateContext(p_dpy, config, EGL_NO_CONTEXT,
                                            getGlesMaxContextAttribs());
    if (ctx == EGL_NO_CONTEXT) {
        GL_LOG("Could not create GLES 2.x Context!");
        ERR("%s: Could not create GLES 2.x Context!\n", __FUNCTION__);
        s_egl.eglDestroySurface(p_dpy, surface);
        return NULL;
    }

    if (!s_egl.eglMakeCurrent(p_dpy, surface, surface, ctx)) {
        GL_LOG("Could not make GLES 2.x context current!");
        ERR("%s: Could not make GLES 2.x context current!\n", __FUNCTION__);
        s_egl.eglDestroySurface(p_dpy, surface);
        s_egl.eglDestroyContext(p_dpy, ctx);
        return NULL;
    }

    // the string pointer may become invalid when the context is destroyed
    const char* s = (const char*)s_gles2.glGetString(GL_EXTENSIONS);
    char* extString = strdup(s ? s : "");
	const char* render = (const char *)s_gles2.glGetString(GL_RENDERER);
    g_isBasePhone = std::string(render).find("AMD Radeon (TM) Pro WX 5100 Graphics") != std::string::npos;
    INFO("base phone:%d", g_isBasePhone);

    // It is rare but some drivers actually fail this...
    if (!s_egl.eglMakeCurrent(p_dpy, NULL, NULL, NULL)) {
        GL_LOG("Could not unbind context. Please try updating graphics card driver!");
        ERR("%s: Could not unbind context. Please try updating graphics card "
            "driver!\n",
            __FUNCTION__);
        free(extString);
        extString = NULL;
    }
    s_egl.eglDestroyContext(p_dpy, ctx);
    s_egl.eglDestroySurface(p_dpy, surface);

    return extString;
}

// A condition variable needed to wait for framebuffer initialization.
namespace {
struct InitializedGlobals {
    android::base::Lock lock;
    android::base::ConditionVariable condVar;
};
}  // namespace

// |sInitialized| caches the initialized framebuffer state - this way
// happy path doesn't need to lock the mutex.
static std::atomic<bool> sInitialized{false};
static LazyInstance<InitializedGlobals> sGlobals = {};

void FrameBuffer::waitUntilInitialized() {
    if (sInitialized.load(std::memory_order_relaxed)) {
        return;
    }

#if SNAPSHOT_PROFILE > 1
    const auto startTime = System::get()->getHighResTimeUs();
#endif
    {
        AutoLock l(sGlobals->lock);
        sGlobals->condVar.wait(
                &l, [] { return sInitialized.load(std::memory_order_acquire); });
    }
#if SNAPSHOT_PROFILE > 1
    printf("Waited for FrameBuffer initialization for %.03f ms\n",
           (System::get()->getHighResTimeUs() - startTime) / 1000.0);
#endif
}

void FrameBuffer::finalize() {
    AutoLock lock(sGlobals->lock);
    sInitialized.store(true, std::memory_order_relaxed);
    sGlobals->condVar.broadcastAndUnlock(&lock);

    if (m_shuttingDown) {
        // The only visible thing in the framebuffer is subwindow. Everything else
        // will get cleaned when the process exits.
        if (m_useSubWindow) {
            m_postWorker.reset();
            removeSubWindow_locked();
        }
        return;
    }

    sweepColorBuffersLocked();

    m_colorbuffers.clear();
    m_colorBufferDelayedCloseList.clear();
    if (m_useSubWindow) {
        removeSubWindow_locked();
    }
    m_windows.clear();
    m_contexts.clear();
    m_yuvDraw = nullptr;
    if (m_eglDisplay != EGL_NO_DISPLAY) {
        s_egl.eglMakeCurrent(m_eglDisplay, NULL, NULL, NULL);
        if (m_eglContext != EGL_NO_CONTEXT) {
            s_egl.eglDestroyContext(m_eglDisplay, m_eglContext);
            m_eglContext = EGL_NO_CONTEXT;
        }
        if (m_pbufContext != EGL_NO_CONTEXT) {
            s_egl.eglDestroyContext(m_eglDisplay, m_pbufContext);
            m_pbufContext = EGL_NO_CONTEXT;
        }
        if (m_pbufSurface != EGL_NO_SURFACE) {
            s_egl.eglDestroySurface(m_eglDisplay, m_pbufSurface);
            m_pbufSurface = EGL_NO_SURFACE;
        }
        if (m_eglSurface != EGL_NO_SURFACE) {
            s_egl.eglDestroySurface(m_eglDisplay, m_eglSurface);
            m_eglSurface = EGL_NO_SURFACE;
        }
        s_egl.eglTerminate(m_eglDisplay);
        m_eglDisplay = EGL_NO_DISPLAY;
    }

    m_readbackThread.enqueue({ReadbackCmd::Exit});
}

extern GLESv2Dispatch s_gles2;
extern "C" bool InitLibrary();
bool InitLibrary()
{
    if (!init_egl_dispatch()) {
        ERR("Failed to init_egl_dispatch");
        return false;
    }
    INFO("Init egl dispatch");
    if (!gles2_dispatch_init(&s_gles2)) {
        ERR("Failed to gles2_dispatch_init");
        return false;
    }
    INFO("Init gles2 dispatch");
    return true;
}
bool FrameBuffer::initialize(int width, int height, unsigned int guest_width, unsigned int guest_height, bool useSubWindow,
        bool egl2egl) {
    GL_LOG("FrameBuffer::initialize");
    if (s_theFrameBuffer != NULL) {
        delete s_theFrameBuffer;
        s_theFrameBuffer = nullptr;
        GL_LOG("FrameBuffer Reinitialize");
    }

    //
    // allocate space for the FrameBuffer object
    //
    std::unique_ptr<FrameBuffer> fb(
            new FrameBuffer(width, height, guest_width, guest_height, useSubWindow));
    if (!fb) {
        GL_LOG("Failed to create fb");
        ERR("Failed to create fb\n");
        return false;
    }

    if (s_egl.eglUseOsEglApi)
        s_egl.eglUseOsEglApi(egl2egl);
    //
    // Initialize backend EGL display
    //
    fb->m_eglDisplay = s_egl.eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (fb->m_eglDisplay == EGL_NO_DISPLAY) {
        GL_LOG("Failed to Initialize backend EGL display");
        ERR("Failed to Initialize backend EGL display\n");
        return false;
    }

    GL_LOG("call eglInitialize");
    if (!s_egl.eglInitialize(fb->m_eglDisplay, &fb->m_caps.eglMajor,
                             &fb->m_caps.eglMinor)) {
        GL_LOG("Failed to eglInitialize");
        ERR("Failed to eglInitialize\n");
        return false;
    }

    DBG("egl: %d %d\n", fb->m_caps.eglMajor, fb->m_caps.eglMinor);
    GL_LOG("egl: %d %d", fb->m_caps.eglMajor, fb->m_caps.eglMinor);
    s_egl.eglBindAPI(EGL_OPENGL_ES_API);

    GLESDispatchMaxVersion dispatchMaxVersion =
            calcMaxVersionFromDispatch(fb->m_eglDisplay);

    FrameBuffer::setMaxGLESVersion(dispatchMaxVersion);
    if (s_egl.eglSetMaxGLESVersion) {
        // eglSetMaxGLESVersion must be called before any context binding
        // because it changes how we initialize the dispatcher table.
        s_egl.eglSetMaxGLESVersion(dispatchMaxVersion);
    }

    int glesMaj, glesMin;
    emugl::getGlesVersion(&glesMaj, &glesMin);

    DBG("gles version: %d %d\n", glesMaj, glesMin);
    GL_LOG("gles version: %d %d\n", glesMaj, glesMin);

    fb->m_asyncReadbackSupported = glesMaj > 2;
    if (fb->m_asyncReadbackSupported) {
        DBG("Async readback supported\n");
        GL_LOG("Async readback supported");
    } else {
        DBG("Async readback not supported\n");
        GL_LOG("Async readback not supported");
    }

    fb->m_fastBlitSupported = false;

    //
    // if GLES2 plugin has loaded - try to make GLES2 context and
    // get GLES2 extension string
    //
    android::base::ScopedCPtr<char> gles2Extensions(
            getGLES2ExtensionString(fb->m_eglDisplay));
    if (!gles2Extensions) {
        // Could not create GLES2 context - drop GL2 capability
        GL_LOG("Failed to obtain GLES 2.x extensions string!");
        ERR("Failed to obtain GLES 2.x extensions string!\n");
        return false;
    }

    //
    // Create EGL context for framebuffer post rendering.
    //
    GLint surfaceType = (useSubWindow ? EGL_WINDOW_BIT : 0) | EGL_PBUFFER_BIT;

    // On Linux, we need RGB888 exactly, or eglMakeCurrent will fail,
    // as glXMakeContextCurrent needs to match the format of the
    // native pixmap.
    EGLint wantedRedSize = 8;
    EGLint wantedGreenSize = 8;
    EGLint wantedBlueSize = 8;

    const GLint configAttribs[] = {
            EGL_RED_SIZE,       wantedRedSize, EGL_GREEN_SIZE,
            wantedGreenSize,    EGL_BLUE_SIZE, wantedBlueSize,
            EGL_SURFACE_TYPE,   surfaceType,   EGL_RENDERABLE_TYPE,
            EGL_OPENGL_ES2_BIT, EGL_NONE};

    EGLint total_num_configs = 0;
    s_egl.eglGetConfigs(fb->m_eglDisplay, NULL, 0, &total_num_configs);

    std::vector<EGLConfig> all_configs(total_num_configs);
    EGLint total_egl_compatible_configs = 0;
    s_egl.eglChooseConfig(fb->m_eglDisplay, configAttribs, &all_configs[0],
                          total_num_configs, &total_egl_compatible_configs);

    EGLint exact_match_index = -1;
    for (EGLint i = 0; i < total_egl_compatible_configs; i++) {
        EGLint r, g, b;
        EGLConfig c = all_configs[i];
        s_egl.eglGetConfigAttrib(fb->m_eglDisplay, c, EGL_RED_SIZE, &r);
        s_egl.eglGetConfigAttrib(fb->m_eglDisplay, c, EGL_GREEN_SIZE, &g);
        s_egl.eglGetConfigAttrib(fb->m_eglDisplay, c, EGL_BLUE_SIZE, &b);

        if (r == wantedRedSize && g == wantedGreenSize && b == wantedBlueSize) {
            exact_match_index = i;
            break;
        }
    }

    if (exact_match_index < 0) {
        GL_LOG("Failed on eglChooseConfig");
        ERR("Failed on eglChooseConfig\n");
        return false;
    }

    fb->m_eglConfig = all_configs[exact_match_index];

    GL_LOG("attempting to create egl context");
    fb->m_eglContext = s_egl.eglCreateContext(fb->m_eglDisplay, fb->m_eglConfig,
                                              EGL_NO_CONTEXT, getGlesMaxContextAttribs());
    if (fb->m_eglContext == EGL_NO_CONTEXT) {
        GL_LOG("Failed to create context 0x%x", s_egl.eglGetError());
        ERR("Failed to create context 0x%x\n", s_egl.eglGetError());
        return false;
    }

    GL_LOG("attempting to create egl pbuffer context");
    //
    // Create another context which shares with the eglContext to be used
    // when we bind the pbuffer. That prevent switching drawable binding
    // back and forth on framebuffer context.
    // The main purpose of it is to solve a "blanking" behaviour we see on
    // on Mac platform when switching binded drawable for a context however
    // it is more efficient on other platforms as well.
    //
    fb->m_pbufContext =
            s_egl.eglCreateContext(fb->m_eglDisplay, fb->m_eglConfig,
                                   fb->m_eglContext, getGlesMaxContextAttribs());
    if (fb->m_pbufContext == EGL_NO_CONTEXT) {
        GL_LOG("Failed to create Pbuffer Context 0x%x", s_egl.eglGetError());
        ERR("Failed to create Pbuffer Context 0x%x\n", s_egl.eglGetError());
        return false;
    }

    GL_LOG("context creation successful");
    //
    // create a 1x1 pbuffer surface which will be used for binding
    // the FB context.
    // The FB output will go to a subwindow, if one exist.
    //
    static const EGLint pbufAttribs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};

    fb->m_pbufSurface = s_egl.eglCreatePbufferSurface(
            fb->m_eglDisplay, fb->m_eglConfig, pbufAttribs);
    if (fb->m_pbufSurface == EGL_NO_SURFACE) {
        GL_LOG("Failed to create pbuf surface for FB 0x%x", s_egl.eglGetError());
        ERR("Failed to create pbuf surface for FB 0x%x\n", s_egl.eglGetError());
        return false;
    }

    GL_LOG("attempting to make context current");
    // Make the context current
    ScopedBind bind(fb->m_colorBufferHelper);
    if (!bind.isOk()) {
        GL_LOG("Failed to make current");
        ERR("Failed to make current\n");
        return false;
    }
    GL_LOG("context-current successful");

    //
    // Initilize framebuffer capabilities
    //
    const bool has_gl_oes_image =
            emugl::hasExtension(gles2Extensions.get(), "GL_OES_EGL_image");
    gles2Extensions.reset();

    fb->m_caps.has_eglimage_texture_2d = false;
    fb->m_caps.has_eglimage_renderbuffer = false;
    if (has_gl_oes_image) {
        const char* const eglExtensions =
                s_egl.eglQueryString(fb->m_eglDisplay, EGL_EXTENSIONS);
        if (eglExtensions != nullptr) {
            fb->m_caps.has_eglimage_texture_2d =
                    emugl::hasExtension(eglExtensions, "EGL_KHR_gl_texture_2D_image");
            fb->m_caps.has_eglimage_renderbuffer =
                    emugl::hasExtension(eglExtensions, "EGL_KHR_gl_renderbuffer_image");
        }
    }

    //
    // Fail initialization if not all of the following extensions
    // exist:
    //     EGL_KHR_gl_texture_2d_image
    //     GL_OES_EGL_IMAGE (by both GLES implementations [1 and 2])
    //
    if (!fb->m_caps.has_eglimage_texture_2d) {
        GL_LOG("Failed: Missing egl_image related extension(s)");
        ERR("Failed: Missing egl_image related extension(s)\n");
        return false;
    }

    GL_LOG("host system has enough extensions");
    //
    // Initialize set of configs
    //
    fb->m_configs = new FbConfigList(fb->m_eglDisplay, g_isBasePhone);
    if (fb->m_configs->empty()) {
        GL_LOG("Failed: Initialize set of configs");
        ERR("Failed: Initialize set of configs\n");
        return false;
    }
	if (!fb->m_configs->MatchAllConfig()) {
		ERR("Failed to match all server config");
        return false;
    }

    //
    // Check that we have config for each GLES and GLES2
    //
    size_t nConfigs = fb->m_configs->size();
    int nGLConfigs = 0;
    int nGL2Configs = 0;
    for (size_t i = 0; i < nConfigs; ++i) {
        GLint rtype = fb->m_configs->get(i)->getRenderableType();
        if (0 != (rtype & EGL_OPENGL_ES_BIT)) {
            nGLConfigs++;
        }
        if (0 != (rtype & EGL_OPENGL_ES2_BIT)) {
            nGL2Configs++;
        }
    }

    //
    // Don't fail initialization if no GLES configs exist
    //

    //
    // If no configs at all, exit
    //
    if (nGLConfigs + nGL2Configs == 0) {
        GL_LOG("Failed: No GLES 2.x configs found!");
        ERR("Failed: No GLES 2.x configs found!\n");
        return false;
    }

    GL_LOG("There are sufficient EGLconfigs available");

    //
    // Cache the GL strings so we don't have to think about threading or
    // current-context when asked for them.
    //
    fb->m_glVendor = std::string((const char*)s_gles2.glGetString(GL_VENDOR));
    fb->m_glRenderer = std::string((const char*)s_gles2.glGetString(GL_RENDERER));
    fb->m_glVersion = std::string((const char*)s_gles2.glGetString(GL_VERSION));

    DBG("GL Vendor %s\n", fb->m_glVendor.c_str());
    DBG("GL Renderer %s\n", fb->m_glRenderer.c_str());
    DBG("GL Extensions %s\n", fb->m_glVersion.c_str());
    GL_LOG("GL Vendor %s", fb->m_glVendor.c_str());
    GL_LOG("GL Renderer %s", fb->m_glRenderer.c_str());
    GL_LOG("GL Extensions %s", fb->m_glVersion.c_str());

    fb->m_textureDraw = new TextureDraw();
    if (!fb->m_textureDraw) {
        GL_LOG("Failed: creation of TextureDraw instance");
        ERR("Failed: creation of TextureDraw instance\n");
        return false;
    }
	fb->m_yuvDraw = std::make_shared<YuvDraw<GLESv2Dispatch *>>();
    if (fb->m_yuvDraw == nullptr) {
		ERR("Failed: creation of YuvDraw instance");
		return false;
    }

	if (!fb->m_yuvDraw->Init(&s_gles2)) {
        ERR("Failed: init of YuvDraw instance\n");
        return false;
    }
    //
    // Keep the singleton framebuffer pointer
    //
    s_theFrameBuffer = fb.release();
    {
        AutoLock lock(sGlobals->lock);
        sInitialized.store(true, std::memory_order_release);
        sGlobals->condVar.broadcastAndUnlock(&lock);
    }

    GL_LOG("basic EGL initialization successful");

    // Nothing else to do - we're ready to rock!
    return true;
}

bool FrameBuffer::importMemoryToColorBuffer(
#ifdef _WIN32
        void* handle,
#else
        int handle,
#endif
        uint64_t size,
        bool dedicated,
        bool linearTiling,
        bool vulkanOnly,
        uint32_t colorBufferHandle) {
    AutoLock mutex(m_lock);

    ColorBufferMap::iterator c(m_colorbuffers.find(colorBufferHandle));
    if (c == m_colorbuffers.end()) {
        // bad colorbuffer handle
        ERR("FB: importMemoryToColorBuffer cb handle %#x not found\n", colorBufferHandle);
        return false;
    }

    return (*c).second.cb->importMemory(handle, size, dedicated, linearTiling, vulkanOnly);
}

void FrameBuffer::setColorBufferInUse(
    uint32_t colorBufferHandle,
    bool inUse) {

    AutoLock mutex(m_lock);

    ColorBufferMap::iterator c(m_colorbuffers.find(colorBufferHandle));
    if (c == m_colorbuffers.end()) {
        // bad colorbuffer handle
        ERR("FB: setColorBufferInUse cb handle %#x not found\n", colorBufferHandle);
        return;
    }

    (*c).second.cb->setInUse(inUse);
}

void FrameBuffer::disableFastBlit() {
    m_fastBlitSupported = false;
}

void FrameBuffer::fillGLESUsages(android_studio::EmulatorGLESUsages* usages) {
    if (s_egl.eglFillUsages) {
        s_egl.eglFillUsages(usages);
    }
}

static GLESDispatchMaxVersion sMaxGLESVersion = GLES_DISPATCH_MAX_VERSION_2;

// static
void FrameBuffer::setMaxGLESVersion(GLESDispatchMaxVersion version) {
    sMaxGLESVersion = version;
}

GLESDispatchMaxVersion FrameBuffer::getMaxGLESVersion() {
    return sMaxGLESVersion;
}

FrameBuffer::FrameBuffer(int p_width, int p_height, unsigned int guestWidth, unsigned int guestHeight, bool useSubWindow)
    : m_framebufferWidth(p_width),
      m_framebufferHeight(p_height),
      m_windowWidth(p_width),
      m_windowHeight(p_height),
	  m_guestWidth(guestWidth),
      m_guestHeight(guestHeight),
      m_useSubWindow(useSubWindow),
      m_fpsStats(getenv("SHOW_FPS_STATS") != nullptr),
      m_perfStats(false),
      m_colorBufferHelper(new ColorBufferHelper(this)),
      m_refCountPipeEnabled(false),
      m_noDelayCloseColorBufferEnabled(false),
      m_readbackThread([this](FrameBuffer::Readback&& readback) {
          return sendReadbackWorkerCmd(readback);
      }),
      m_postThread([this](FrameBuffer::Post&& post) {
          return postWorkerFunc(post);
      }) {
     uint32_t displayId = 0;
     if (createDisplay(&displayId) < 0) {
         fprintf(stderr, "Failed to create default display\n");
     }

     setDisplayPose(displayId, 0, 0, getWidth(), getHeight(), 0);
}

FrameBuffer::~FrameBuffer() {
    finalize();

    if (m_postThread.isStarted()) {
        m_postThread.enqueue({ PostCmd::Exit, });
    }

    delete m_textureDraw;
    delete m_configs;
    delete m_colorBufferHelper;

    if (s_theFrameBuffer) {
        s_theFrameBuffer = nullptr;
    }
    sInitialized.store(false, std::memory_order_relaxed);

    m_readbackThread.join();
    m_postThread.join();

    m_postWorker.reset();
    m_readbackWorker.reset();
}

WorkerProcessingResult
FrameBuffer::sendReadbackWorkerCmd(const Readback& readback) {
    ensureReadbackWorker();
    switch (readback.cmd) {
    case ReadbackCmd::Init:
        m_readbackWorker->initGL();
        return WorkerProcessingResult::Continue;
    case ReadbackCmd::GetPixels:
        m_readbackWorker->getPixels(readback.displayId, readback.pixelsOut, readback.bytes);
        return WorkerProcessingResult::Continue;
    case ReadbackCmd::AddRecordDisplay:
        m_readbackWorker->setRecordDisplay(readback.displayId, readback.width, readback.height, true);
        return WorkerProcessingResult::Continue;
    case ReadbackCmd::DelRecordDisplay:
        m_readbackWorker->setRecordDisplay(readback.displayId, 0, 0, false);
        return WorkerProcessingResult::Continue;
    case ReadbackCmd::Exit:
        return WorkerProcessingResult::Stop;
    }
    return WorkerProcessingResult::Stop;
}

WorkerProcessingResult
FrameBuffer::postWorkerFunc(const Post& post) {
    switch (post.cmd) {
        case PostCmd::Post:
            m_postWorker->post(post.cb);
            break;
        case PostCmd::Viewport:
            m_postWorker->viewport(post.viewport.width,
                                   post.viewport.height);
            break;
        case PostCmd::Compose:
            if (post.d->version <= 1) {
                m_postWorker->compose(post.d);
            } else {
                m_postWorker->compose((ComposeDevice_v2*)post.d);
            }
            break;
        case PostCmd::Clear:
            m_postWorker->clear();
            break;
        case PostCmd::Screenshot:
            m_postWorker->screenshot(
                post.screenshot.cb,
                post.screenshot.screenwidth,
                post.screenshot.screenheight,
                post.screenshot.format,
                post.screenshot.type,
                post.screenshot.rotation,
                post.screenshot.pixels);
            break;
        case PostCmd::Exit:
            return WorkerProcessingResult::Stop;
        case PostCmd::ResetSubWindow:
            m_postWorker->resetSubWindow();
            break;
        default:
            break;
    }
    return WorkerProcessingResult::Continue;
}

void FrameBuffer::sendPostWorkerCmd(FrameBuffer::Post post) {
    if (!m_postThread.isStarted()) {
        m_postWorker.reset(new PostWorker([this]() {
            if (m_subWin) {
                return bindSubwin_locked();
            } else {
                return bindFakeWindow_locked();
            }
        }));
        m_postThread.start();
    }

    m_postThread.enqueue(Post(post));
    m_postThread.waitQueuedItems();
}

void FrameBuffer::setPostCallback(
        emugl::Renderer::OnPostCallback onPost,
        void* onPostContext,
        uint32_t displayId,
        bool useBgraReadback) {
    AutoLock lock(m_lock);
    if (onPost) {
        uint32_t w, h;
        if (!false) {
            ERR("display %d not exist, cancelling OnPost callback", displayId);
            return;
        }
        if (m_onPost.find(displayId) != m_onPost.end()) {
            ERR("display %d already configured for recording", displayId);
            return;
        }
        m_onPost[displayId].cb = onPost;
        m_onPost[displayId].context = onPostContext;
        m_onPost[displayId].displayId = displayId;
        m_onPost[displayId].width = w;
        m_onPost[displayId].height = h;
        m_onPost[displayId].img = new unsigned char[4 * w * h];
        m_onPost[displayId].readBgra = useBgraReadback;
        if (!m_readbackThread.isStarted()) {
            m_readbackThread.start();
            m_readbackThread.enqueue({ ReadbackCmd::Init });
        }
        m_readbackThread.enqueue({ ReadbackCmd::AddRecordDisplay, displayId, 0, nullptr, 0, w, h });
        m_readbackThread.waitQueuedItems();
    } else {
        m_readbackThread.enqueue({ ReadbackCmd::DelRecordDisplay, displayId });
        m_readbackThread.waitQueuedItems();
        m_onPost.erase(displayId);
    }
}

static void subWindowRepaint(void* param) {
    GL_LOG("call repost from subWindowRepaint callback");
    auto fb = static_cast<FrameBuffer*>(param);
    fb->repost();
}

bool FrameBuffer::setupSubWindow(EGLNativeWindowType p_window,
                                 int x,
                                 int y,
                                 int width,
                                 int height,
                                 float zRot) {
    if (!m_useSubWindow) {
        ERR("%s: Cannot create native sub-window in this configuration\n",
            __FUNCTION__);
        return false;
    }


    AutoLock mutex(m_lock);

    m_nativeWindow = p_window;

    // create EGLSurface from the generated subwindow
    m_eglSurface = s_egl.eglCreateWindowSurface(m_eglDisplay,
                                                m_eglConfig,
                                                p_window,
                                                nullptr);

    // if (m_eglSurface != EGL_NO_SURFACE) {
    bool success = false;
    if (bindSubwin_locked()) {
        s_gles2.glViewport(0, 0, this->m_guestWidth, this->m_guestHeight);
        m_subWin = p_window;    
        if (m_lastPostedColorBuffer == 0) {
            s_gles2.glClearColor(1, 1, 1, 0.5);
            s_gles2.glClear(GL_COLOR_BUFFER_BIT |
                            GL_DEPTH_BUFFER_BIT |
                            GL_STENCIL_BUFFER_BIT);
           
            s_egl.eglSwapBuffers(m_eglDisplay, m_eglSurface);
        }

        unbind_locked();

        success = true;
    }
    if (m_lastPostedColorBuffer) {
        post(m_lastPostedColorBuffer, 0);
    }
    return success;
}

bool FrameBuffer::removeSubWindow() {
    if (!m_useSubWindow) {
        ERR("%s: Cannot remove native sub-window in this configuration\n",
            __FUNCTION__);
        return false;
    }
    AutoLock lock(sGlobals->lock);
    sInitialized.store(false, std::memory_order_relaxed);
    sGlobals->condVar.broadcastAndUnlock(&lock);

    AutoLock mutex(m_lock);
    return removeSubWindow_locked();
}

bool FrameBuffer::removeSubWindow_locked() {
    if (!m_useSubWindow) {
        ERR("%s: Cannot remove native sub-window in this configuration\n",
            __FUNCTION__);
        return false;
    }
    bool removed = false;
    if (m_subWin) {
        s_egl.eglMakeCurrent(m_eglDisplay, NULL, NULL, NULL);
        s_egl.eglDestroySurface(m_eglDisplay, m_eglSurface);

        m_eglSurface = EGL_NO_SURFACE;
        m_subWin = (EGLNativeWindowType)0;
        Post postCmd;
        postCmd.cmd = PostCmd::ResetSubWindow;
        sendPostWorkerCmd(postCmd);
        removed = true;
    }
    return removed;
}

HandleType FrameBuffer::genHandle_locked() {
    HandleType id;
    do {
        id = ++s_nextHandle;
    } while (id == 0 || m_contexts.find(id) != m_contexts.end() ||
             m_windows.find(id) != m_windows.end() ||
             m_colorbuffers.find(id) != m_colorbuffers.end());

    return id;
}

HandleType FrameBuffer::createColorBuffer(int p_width,
                                          int p_height,
                                          GLenum p_internalFormat,
                                          FrameworkFormat p_frameworkFormat) {

    AutoLock mutex(m_lock);
    return createColorBufferLocked(p_width, p_height, p_internalFormat,
                                   p_frameworkFormat);
}

void FrameBuffer::createColorBufferWithHandle(
     int p_width,
     int p_height,
     GLenum p_internalFormat,
     FrameworkFormat p_frameworkFormat,
     HandleType handle) {

    AutoLock mutex(m_lock);

    // Check for handle collision
    if (m_colorbuffers.count(handle) != 0) {
        emugl::emugl_crash_reporter(
            "FATAL: color buffer with handle %u already exists",
            handle);
    }

    createColorBufferWithHandleLocked(
        p_width, p_height, p_internalFormat, p_frameworkFormat,
        handle);
}

HandleType FrameBuffer::createColorBufferLocked(int p_width,
                                                int p_height,
                                                GLenum p_internalFormat,
                                                FrameworkFormat p_frameworkFormat) {
    sweepColorBuffersLocked();

    return createColorBufferWithHandleLocked(
        p_width, p_height, p_internalFormat, p_frameworkFormat,
        genHandle_locked());
}

HandleType FrameBuffer::createColorBufferWithHandleLocked(
    int p_width,
    int p_height,
    GLenum p_internalFormat,
    FrameworkFormat p_frameworkFormat,
    HandleType handle) {

    sweepColorBuffersLocked();

    ColorBufferPtr cb(ColorBuffer::create(getDisplay(), p_width, p_height,
                                          p_internalFormat, p_frameworkFormat,
                                          handle, m_colorBufferHelper,
                                          m_fastBlitSupported));
    if (cb.get() != NULL) {
        assert(m_colorbuffers.count(handle) == 0);
        // When guest feature flag RefCountPipe is on, no reference counting is
        // needed. We only memoize the mapping from handle to ColorBuffer.
        // Explicitly set refcount to 1 to avoid the colorbuffer being added to
        // m_colorBufferDelayedCloseList in FrameBuffer::onLoad().
        if (m_refCountPipeEnabled) {
            m_colorbuffers[handle] = { std::move(cb), 1, false, 0 };
        } else {
            // Android master default api level is 1000
            int apiLevel = 1000;
            emugl::getAvdInfo(nullptr, &apiLevel);
            // pre-O and post-O use different color buffer memory management
            // logic
            if (apiLevel > 0 && apiLevel < 26) {
                m_colorbuffers[handle] = {std::move(cb), 1, false, 0};
            } else {
                m_colorbuffers[handle] = {std::move(cb), 1, false, 0};
            }
                RenderThreadInfo* tInfo = RenderThreadInfo::get();
                uint64_t puid = tInfo->m_puid;
                if (puid) {
                    m_procOwnedColorBuffers[puid][handle]++;
            }
        }
    } else {
        handle = 0;
        DBG("Create color buffer failed.\n");
    }
    return handle;
}

HandleType FrameBuffer::createRenderContext(int p_config,
                                            HandleType p_share,
                                            GLESApi version) {
    AutoLock mutex(m_lock);
    emugl::ReadWriteMutex::AutoWriteLock contextLock(m_contextStructureLock);
    HandleType ret = 0;

    const FbConfig* config = getConfigs()->get(p_config);
    if (!config) {
        return ret;
    }

    RenderContextPtr share;
    if (p_share != 0) {
        RenderContextMap::iterator s(m_contexts.find(p_share));
        if (s == m_contexts.end()) {
            return ret;
        }
        share = (*s).second;
    }
    EGLContext sharedContext =
            share.get() ? share->getEGLContext() : EGL_NO_CONTEXT;

    ret = genHandle_locked();
    RenderContextPtr rctx(RenderContext::create(
            m_eglDisplay, config->getEglConfig(), sharedContext, ret, version));
    if (rctx.get() != NULL) {
        m_contexts[ret] = rctx;
        RenderThreadInfo* tinfo = RenderThreadInfo::get();
        uint64_t puid = tinfo->m_puid;
        // The new emulator manages render contexts per guest process.
        // Fall back to per-thread management if the system image does not
        // support it.
        if (puid) {
            m_procOwnedRenderContext[puid].insert(ret);
        } else { // legacy path to manage context lifetime by threads
            tinfo->m_contextSet.insert(ret);
        }
    } else {
        ret = 0;
    }

    return ret;
}

HandleType FrameBuffer::createWindowSurface(int p_config,
                                            int p_width,
                                            int p_height) {
    AutoLock mutex(m_lock);

    HandleType ret = 0;

    const FbConfig* config = getConfigs()->get(p_config);
    if (!config) {
        return ret;
    }

    ret = genHandle_locked();
    WindowSurfacePtr win(WindowSurface::create(
            getDisplay(), config->getEglConfig(), p_width, p_height, ret));
    if (win.get() != NULL) {
        m_windows[ret] = { win, 0 };
        RenderThreadInfo* tInfo = RenderThreadInfo::get();
        uint64_t puid = tInfo->m_puid;
        if (puid) {
            m_procOwnedWindowSurfaces[puid].insert(ret);
        } else { // legacy path to manage window surface lifetime by threads
            tInfo->m_windowSet.insert(ret);
        }
    }

    return ret;
}

void FrameBuffer::drainRenderContext() {
    if (m_shuttingDown) {
        return;
    }

    RenderThreadInfo* const tinfo = RenderThreadInfo::get();
    if (tinfo->m_contextSet.empty()) {
        return;
    }

    AutoLock mutex(m_lock);
    emugl::ReadWriteMutex::AutoWriteLock contextLock(m_contextStructureLock);
    for (const HandleType contextHandle : tinfo->m_contextSet) {
        m_contexts.erase(contextHandle);
    }
    tinfo->m_contextSet.clear();
}

void FrameBuffer::drainWindowSurface() {
    if (m_shuttingDown) {
        return;
    }
    RenderThreadInfo* const tinfo = RenderThreadInfo::get();
    if (tinfo->m_windowSet.empty()) {
        return;
    }

    std::vector<HandleType> colorBuffersToCleanup;

    AutoLock mutex(m_lock);
    ScopedBind bind(m_colorBufferHelper);
    for (const HandleType winHandle : tinfo->m_windowSet) {
        const auto winIt = m_windows.find(winHandle);
        if (winIt != m_windows.end()) {
            if (const HandleType oldColorBufferHandle = winIt->second.second) {
                if (m_refCountPipeEnabled) {
                    if (decColorBufferRefCountLocked(oldColorBufferHandle)) {
                        colorBuffersToCleanup.push_back(oldColorBufferHandle);
                    }
                } else {
                    if (closeColorBufferLocked(oldColorBufferHandle)) {
                        colorBuffersToCleanup.push_back(oldColorBufferHandle);
                    }
                }
                m_windows.erase(winIt);
            }
        }
    }
    tinfo->m_windowSet.clear();

    m_lock.unlock();
}

void FrameBuffer::DestroyRenderContext(HandleType p_context) {
    AutoLock mutex(m_lock);
    sweepColorBuffersLocked();

    emugl::ReadWriteMutex::AutoWriteLock contextLock(m_contextStructureLock);
    m_contexts.erase(p_context);
    RenderThreadInfo* tinfo = RenderThreadInfo::get();
    uint64_t puid = tinfo->m_puid;
    // The new emulator manages render contexts per guest process.
    // Fall back to per-thread management if the system image does not
    // support it.
    if (puid) {
        auto ite = m_procOwnedRenderContext.find(puid);
        if (ite != m_procOwnedRenderContext.end()) {
            ite->second.erase(p_context);
        }
    } else {
        tinfo->m_contextSet.erase(p_context);
    }
}

void FrameBuffer::DestroyWindowSurface(HandleType p_surface) {
    if (m_shuttingDown) {
        return;
    }
    AutoLock mutex(m_lock);
    auto colorBuffersToCleanup = DestroyWindowSurfaceLocked(p_surface);

    mutex.unlock();
}

std::vector<HandleType> FrameBuffer::DestroyWindowSurfaceLocked(HandleType p_surface) {
    std::vector<HandleType> colorBuffersToCleanUp;
    const auto w = m_windows.find(p_surface);
    if (w != m_windows.end()) {
        ScopedBind bind(m_colorBufferHelper);
        if (m_refCountPipeEnabled) {
            if (decColorBufferRefCountLocked(w->second.second)) {
                colorBuffersToCleanUp.push_back(w->second.second);
            }
        } else {
            if (closeColorBufferLocked(w->second.second)) {
                colorBuffersToCleanUp.push_back(w->second.second);
            }
        }
        m_windows.erase(w);
        RenderThreadInfo* tinfo = RenderThreadInfo::get();
        uint64_t puid = tinfo->m_puid;
        if (puid) {
            auto ite = m_procOwnedWindowSurfaces.find(puid);
            if (ite != m_procOwnedWindowSurfaces.end()) {
                ite->second.erase(p_surface);
            }
        } else {
            tinfo->m_windowSet.erase(p_surface);
        }
    }
    return colorBuffersToCleanUp;
}

int FrameBuffer::openColorBuffer(HandleType p_colorbuffer) {
    // When guest feature flag RefCountPipe is on, no reference counting is
    // needed.
    if (m_refCountPipeEnabled)
        return 0;

    RenderThreadInfo* tInfo = RenderThreadInfo::get();

    AutoLock mutex(m_lock);

    ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
    if (c == m_colorbuffers.end()) {
        // bad colorbuffer handle
        ERR("FB: openColorBuffer cb handle %#x not found\n", p_colorbuffer);
        return -1;
    }

    c->second.refcount++;
    markOpened(&c->second);

    uint64_t puid = tInfo->m_puid;
    if (puid) {
        m_procOwnedColorBuffers[puid][p_colorbuffer]++;
    }
    return 0;
}

void FrameBuffer::closeColorBuffer(HandleType p_colorbuffer) {
    // When guest feature flag RefCountPipe is on, no reference counting is
    // needed.
    if (m_refCountPipeEnabled)
        return;

    RenderThreadInfo* tInfo = RenderThreadInfo::get();

    std::vector<HandleType> toCleanup;

    AutoLock mutex(m_lock);
    uint64_t puid = tInfo->m_puid;
    if (false) {
        auto ite = m_procOwnedColorBuffers.find(puid);
        if (ite != m_procOwnedColorBuffers.end()) {
            const auto& cb = ite->second.find(p_colorbuffer);
            if (cb != ite->second.end()) {
                ite->second.erase(cb);
                if (closeColorBufferLocked(p_colorbuffer)) {
                    toCleanup.push_back(p_colorbuffer);
                }
            }
        }
    } else {
        if (closeColorBufferLocked(p_colorbuffer)) {
            toCleanup.push_back(p_colorbuffer);
        }
    }

    mutex.unlock();
}

bool FrameBuffer::closeColorBufferLocked(HandleType p_colorbuffer,
                                         bool forced) {
    // When guest feature flag RefCountPipe is on, no reference counting is
    // needed.
    if (m_refCountPipeEnabled)
        return false;

    if (m_noDelayCloseColorBufferEnabled)
        forced = true;

    ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
    if (c == m_colorbuffers.end()) {
        // This is harmless: it is normal for guest system to issue
        // closeColorBuffer command when the color buffer is already
        // garbage collected on the host. (we dont have a mechanism
        // to give guest a notice yet)
        return false;
    }
    RenderThreadInfo* tInfo = RenderThreadInfo::get();
    uint64_t puid = tInfo->m_puid;
    if (puid) {
        auto iter = m_procOwnedColorBuffers[puid].find(p_colorbuffer);
        if (iter != m_procOwnedColorBuffers[puid].end() && iter->second > 0) {
            --iter->second;
        }
    }
    bool deleted = false;
    // The guest can and will gralloc_alloc/gralloc_free and then
    // gralloc_register a buffer, due to API level (O+) or
    // timing issues.
    // So, we don't actually close the color buffer when refcount
    // reached zero, unless it has been opened at least once already.
    // Instead, put it on a 'delayed close' list to return to it later.
    if (--c->second.refcount == 0) {
        if (forced) {
            eraseDelayedCloseColorBufferLocked(c->first, c->second.closedTs);
            m_colorbuffers.erase(c);
            deleted = true;
        } else {
            c->second.closedTs = System::get()->getUnixTime();
            m_colorBufferDelayedCloseList.push_back(
                    {c->second.closedTs, p_colorbuffer});
        }
    }

    performDelayedColorBufferCloseLocked(false);

    return deleted;
}

void FrameBuffer::performDelayedColorBufferCloseLocked(bool forced) {
    // Let's wait just long enough to make sure it's not because of instant
    // timestamp change (end of previous second -> beginning of a next one),
    // but not for long - this is a workaround for race conditions, and they
    // are quick.
    static constexpr int kColorBufferClosingDelaySec = 1;

    const auto now = System::get()->getUnixTime();
    auto it = m_colorBufferDelayedCloseList.begin();
    while (it != m_colorBufferDelayedCloseList.end() &&
           (forced ||
           it->ts + kColorBufferClosingDelaySec <= now)) {
        if (it->cbHandle != 0) {
            const auto& cb = m_colorbuffers.find(it->cbHandle);
            if (cb != m_colorbuffers.end()) {
                m_colorbuffers.erase(cb);
            }
        }
        ++it;
    }
    m_colorBufferDelayedCloseList.erase(
                m_colorBufferDelayedCloseList.begin(), it);
}

void FrameBuffer::eraseDelayedCloseColorBufferLocked(
        HandleType cb, android::base::System::Duration ts)
{
    // Find the first delayed buffer with a timestamp <= |ts|
    auto it = std::lower_bound(
                  m_colorBufferDelayedCloseList.begin(),
                  m_colorBufferDelayedCloseList.end(), ts,
                  [](const ColorBufferCloseInfo& ci, System::Duration ts) {
        return ci.ts < ts;
    });
    while (it != m_colorBufferDelayedCloseList.end() &&
           it->ts == ts) {
        // if this is the one we need - clear it out.
        if (it->cbHandle == cb) {
            it->cbHandle = 0;
            break;
        }
        ++it;
    }
}

void FrameBuffer::cleanupProcGLObjects(uint64_t puid) {
    AutoLock mutex(m_lock);
    auto colorBuffersToCleanup = cleanupProcGLObjects_locked(puid);

    // Run other cleanup callbacks
    // Avoid deadlock by first storing a separate list of callbacks
    std::vector<std::function<void()>> callbacks;

    {
        auto procIte = m_procOwnedCleanupCallbacks.find(puid);
        if (procIte != m_procOwnedCleanupCallbacks.end()) {
            for (auto it : procIte->second) {
                callbacks.push_back(it.second);
            }
            m_procOwnedCleanupCallbacks.erase(procIte);
        }
    }

    mutex.unlock();

    for (auto cb : callbacks) {
        cb();
    }
}

std::vector<HandleType> FrameBuffer::cleanupProcGLObjects_locked(uint64_t puid, bool forced) {
    std::vector<HandleType> colorBuffersToCleanup;
    {
        ScopedBind bind(m_colorBufferHelper);
        // Clean up window surfaces
        {
            auto procIte = m_procOwnedWindowSurfaces.find(puid);
            if (procIte != m_procOwnedWindowSurfaces.end()) {
                for (auto whndl : procIte->second) {
                    auto w = m_windows.find(whndl);
                    if (m_refCountPipeEnabled) {
                        if (decColorBufferRefCountLocked(w->second.second)) {
                            colorBuffersToCleanup.push_back(w->second.second);
                        }
                    } else {
                        if (closeColorBufferLocked(w->second.second, forced)) {
                            colorBuffersToCleanup.push_back(w->second.second);
                        }
                    }
                    m_windows.erase(w);
                }
                m_procOwnedWindowSurfaces.erase(procIte);
            }
        }
        // Clean up color buffers.
        // A color buffer needs to be closed as many times as it is opened by
        // the guest process, to give the correct reference count.
        // (Note that a color buffer can be shared across guest processes.)
        {
            auto procIte = m_procOwnedColorBuffers.find(puid);
            if (procIte != m_procOwnedColorBuffers.end()) {
                for (auto cb : procIte->second) {
                    int count = cb.second;
                    bool isFirst = true;
                    while(count > 0) {
                        if (isFirst) {
                            INFO("begin clearup colorbuffer:%#x, ref:%d", cb.first, count);
                            isFirst = false;
                        }
                        if (closeColorBufferLocked(cb.first, forced)) {
                            colorBuffersToCleanup.push_back(cb.first);
                        }
                        --count;
                    }
                }
                m_procOwnedColorBuffers.erase(procIte);
            }
        }

        // Clean up EGLImage handles
        {
            auto procIte = m_procOwnedEGLImages.find(puid);
            if (procIte != m_procOwnedEGLImages.end()) {
                if (!procIte->second.empty()) {
                    for (auto eglImg : procIte->second) {
                        s_egl.eglDestroyImageKHR(
                                m_eglDisplay,
                                reinterpret_cast<EGLImageKHR>((HandleType)eglImg));
                    }
                }
                m_procOwnedEGLImages.erase(procIte);
            }
        }
    }
    // Unbind before cleaning up contexts
    // Cleanup render contexts
    {
        auto procIte = m_procOwnedRenderContext.find(puid);
        if (procIte != m_procOwnedRenderContext.end()) {
            for (auto ctx : procIte->second) {
                m_contexts.erase(ctx);
            }
            m_procOwnedRenderContext.erase(procIte);
        }
    }

    return colorBuffersToCleanup;
}

void FrameBuffer::markOpened(ColorBufferRef* cbRef) {
    cbRef->opened = true;
    eraseDelayedCloseColorBufferLocked(cbRef->cb->getHndl(), cbRef->closedTs);
    cbRef->closedTs = 0;
}

bool FrameBuffer::flushWindowSurfaceColorBuffer(HandleType p_surface, EGLint *rects, EGLint rectsNum) {
    AutoLock mutex(m_lock);

    WindowSurfaceMap::iterator w(m_windows.find(p_surface));
    if (w == m_windows.end()) {
        ERR("FB::flushWindowSurfaceColorBuffer: window handle %#x not found\n",
            p_surface);
        // bad surface handle
        return false;
    }

    WindowSurface* surface = (*w).second.first.get();
    surface->flushColorBuffer(rects, rectsNum);

    return true;
}

HandleType FrameBuffer::getWindowSurfaceColorBufferHandle(HandleType p_surface) {
    AutoLock mutex(m_lock);

    auto it = m_windowSurfaceToColorBuffer.find(p_surface);

    if (it == m_windowSurfaceToColorBuffer.end()) return 0;

    return it->second;
}

bool FrameBuffer::setWindowSurfaceColorBuffer(HandleType p_surface,
                                              HandleType p_colorbuffer) {
    AutoLock mutex(m_lock);

    WindowSurfaceMap::iterator w(m_windows.find(p_surface));
    if (w == m_windows.end()) {
        // bad surface handle
        ERR("%s: bad window surface handle %#x\n", __FUNCTION__, p_surface);
        return false;
    }

    ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
    if (c == m_colorbuffers.end()) {
        DBG("%s: bad color buffer handle %#x\n", __FUNCTION__, p_colorbuffer);
        // bad colorbuffer handle
        return false;
    }

    (*w).second.first->setColorBuffer((*c).second.cb);
    markOpened(&c->second);
    if (w->second.second) {
        if (m_refCountPipeEnabled)
            decColorBufferRefCountLocked(w->second.second);
        else
            closeColorBufferLocked(w->second.second);
    }
    RenderThreadInfo* tInfo = RenderThreadInfo::get();
    uint64_t puid = tInfo->m_puid;
    if (puid) {
        m_procOwnedColorBuffers[puid][p_colorbuffer]++;
    }
    c->second.refcount++;
    (*w).second.second = p_colorbuffer;

    m_windowSurfaceToColorBuffer[p_surface] = p_colorbuffer;

    return true;
}

void FrameBuffer::readColorBuffer(HandleType p_colorbuffer,
                                  int x,
                                  int y,
                                  int width,
                                  int height,
                                  GLenum format,
                                  GLenum type,
                                  void* pixels) {
    AutoLock mutex(m_lock);

    ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
    if (c == m_colorbuffers.end()) {
        // bad colorbuffer handle
        return;
    }

    (*c).second.cb->readPixels(x, y, width, height, format, type, pixels);
}

void FrameBuffer::readColorBufferYUV(HandleType p_colorbuffer,
                                     int x,
                                     int y,
                                     int width,
                                     int height,
                                     void* pixels,
                                     uint32_t pixels_size) {
    AutoLock mutex(m_lock);

    ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
    if (c == m_colorbuffers.end()) {
        // bad colorbuffer handle
        return;
    }

    (*c).second.cb->readPixelsYUVCached(x, y, width, height, pixels, pixels_size);
}

void FrameBuffer::createYUVTextures(uint32_t type,
                                    uint32_t count,
                                    int width,
                                    int height,
                                    uint32_t* output) {
    constexpr bool kIsInterleaved = true;
    constexpr bool kIsNotInterleaved = false;
    AutoLock mutex(m_lock);
    ScopedBind bind(m_colorBufferHelper);
    for (uint32_t i = 0; i < count; ++i) {
        if (type == FRAMEWORK_FORMAT_NV12) {
            YUVConverter::createYUVGLTex(GL_TEXTURE0, width, height,
                                         &output[2 * i], kIsNotInterleaved);
            YUVConverter::createYUVGLTex(GL_TEXTURE1, width / 2, height / 2,
                                         &output[2 * i + 1], kIsInterleaved);
        } else if (type == FRAMEWORK_FORMAT_YUV_420_888) {
            YUVConverter::createYUVGLTex(GL_TEXTURE0, width, height,
                                         &output[3 * i], kIsNotInterleaved);
            YUVConverter::createYUVGLTex(GL_TEXTURE1, width / 2, height / 2,
                                         &output[3 * i + 1], kIsNotInterleaved);
            YUVConverter::createYUVGLTex(GL_TEXTURE2, width / 2, height / 2,
                                         &output[3 * i + 2], kIsNotInterleaved);
        }
    }
}

void FrameBuffer::destroyYUVTextures(uint32_t type,
                                     uint32_t count,
                                     uint32_t* textures) {
    AutoLock mutex(m_lock);
    ScopedBind bind(m_colorBufferHelper);
    if (type == FRAMEWORK_FORMAT_NV12) {
        s_gles2.glDeleteTextures(2 * count, textures);
    } else if (type == FRAMEWORK_FORMAT_YUV_420_888) {
        s_gles2.glDeleteTextures(3 * count, textures);
    }
}

extern "C" {
typedef void (*yuv_updater_t)(void* privData,
                              uint32_t type,
                              uint32_t* textures);
}

void FrameBuffer::updateYUVTextures(uint32_t type,
                                    uint32_t* textures,
                                    void* privData,
                                    void* func) {
    AutoLock mutex(m_lock);
    ScopedBind bind(m_colorBufferHelper);

    yuv_updater_t updater = (yuv_updater_t)func;
    uint32_t gtextures[3] = {0, 0, 0};

    if (type == FRAMEWORK_FORMAT_NV12) {
        gtextures[0] = s_gles2.glGetGlobalTexName(textures[0]);
        gtextures[1] = s_gles2.glGetGlobalTexName(textures[1]);
    } else if (type == FRAMEWORK_FORMAT_YUV_420_888) {
        gtextures[0] = s_gles2.glGetGlobalTexName(textures[0]);
        gtextures[1] = s_gles2.glGetGlobalTexName(textures[1]);
        gtextures[2] = s_gles2.glGetGlobalTexName(textures[2]);
    }

    updater(privData, type, gtextures);
}

void FrameBuffer::swapTexturesAndUpdateColorBuffer(uint32_t p_colorbuffer,
                                                   int x,
                                                   int y,
                                                   int width,
                                                   int height,
                                                   uint32_t format,
                                                   uint32_t type,
                                                   uint32_t texture_type,
                                                   uint32_t* textures) {
    {
        AutoLock mutex(m_lock);
        ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
        if (c == m_colorbuffers.end()) {
            // bad colorbuffer handle
            return;
        }
        (*c).second.cb->swapYUVTextures(texture_type, textures);
    }

    updateColorBuffer(p_colorbuffer, x, y, width, height, format, type,
                      nullptr);
}

bool FrameBuffer::updateColorBuffer(HandleType p_colorbuffer,
                                    int x,
                                    int y,
                                    int width,
                                    int height,
                                    GLenum format,
                                    GLenum type,
                                    void* pixels) {
    AutoLock mutex(m_lock);

    ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
    if (c == m_colorbuffers.end()) {
        // bad colorbuffer handle
        return false;
    }

    (*c).second.cb->subUpdate(x, y, width, height, format, type, pixels);

    return true;
}

bool FrameBuffer::replaceColorBufferContents(
    HandleType p_colorbuffer, const void* pixels, size_t numBytes) {
    AutoLock mutex(m_lock);

    ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
    if (c == m_colorbuffers.end()) {
        // bad colorbuffer handle
        return false;
    }

    return (*c).second.cb->replaceContents(pixels, numBytes);
}

bool FrameBuffer::readColorBufferContents(
    HandleType p_colorbuffer, size_t* numBytes, void* pixels) {

    AutoLock mutex(m_lock);

    ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
    if (c == m_colorbuffers.end()) {
        // bad colorbuffer handle
        return false;
    }

    return (*c).second.cb->readContents(numBytes, pixels);
}

bool FrameBuffer::getColorBufferInfo(
    HandleType p_colorbuffer, int* width, int* height, GLint* internalformat) {

    AutoLock mutex(m_lock);

    ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
    if (c == m_colorbuffers.end()) {
        // bad colorbuffer handle
        return false;
    }

    auto cb = (*c).second.cb;

    *width = cb->getWidth();
    *height = cb->getHeight();
    *internalformat = cb->getInternalFormat();

    return true;
}

bool FrameBuffer::updateYuv(HandleType p_colorbuffer,
                                    int x, int y, int width, int height,
                                    GLenum format, GLenum type, int32_t yuvFormat, void *pixels)
{
    emugl::Mutex::AutoLock mutex(m_lock);
    ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
    if (c == m_colorbuffers.end()) {
        ERR("colorbuffer handle %#x is not find", p_colorbuffer);
        return false;
    }
    ColorBufferPtr cb = c->second.cb;

    if (cb.get()) {
        cb->updateYuv(x, y, width, height, format, type, yuvFormat, pixels);
    }

    return true;
}
bool FrameBuffer::bindColorBufferToTexture(HandleType p_colorbuffer) {
    AutoLock mutex(m_lock);

    ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
    if (c == m_colorbuffers.end()) {
        // bad colorbuffer handle
        return false;
    }

    return (*c).second.cb->bindToTexture();
}

bool FrameBuffer::bindColorBufferToTexture2(HandleType p_colorbuffer) {
    AutoLock mutex(m_lock);

    ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
    if (c == m_colorbuffers.end()) {
        // bad colorbuffer handle
        return false;
    }

    return (*c).second.cb->bindToTexture2();
}

bool FrameBuffer::bindColorBufferToRenderbuffer(HandleType p_colorbuffer) {
    AutoLock mutex(m_lock);

    ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
    if (c == m_colorbuffers.end()) {
        // bad colorbuffer handle
        return false;
    }

    return (*c).second.cb->bindToRenderbuffer();
}

bool FrameBuffer::bindContext(HandleType p_context,
                              HandleType p_drawSurface,
                              HandleType p_readSurface) {
    if (m_shuttingDown) {
        return false;
    }

    AutoLock mutex(m_lock);

    WindowSurfacePtr draw, read;
    RenderContextPtr ctx;

    //
    // if this is not an unbind operation - make sure all handles are good
    //
    if (p_context || p_drawSurface || p_readSurface) {
        ctx = getContext_locked(p_context);
        if (!ctx)
            return false;
        WindowSurfaceMap::iterator w(m_windows.find(p_drawSurface));
        if (w == m_windows.end()) {
            // bad surface handle
            return false;
        }
        draw = (*w).second.first;

        if (p_readSurface != p_drawSurface) {
            WindowSurfaceMap::iterator w(m_windows.find(p_readSurface));
            if (w == m_windows.end()) {
                // bad surface handle
                return false;
            }
            read = (*w).second.first;
        } else {
            read = draw;
        }
    } else {
        // if unbind operation, sweep color buffers
        sweepColorBuffersLocked();
    }

    if (!s_egl.eglMakeCurrent(m_eglDisplay,
                              draw ? draw->getEGLSurface() : EGL_NO_SURFACE,
                              read ? read->getEGLSurface() : EGL_NO_SURFACE,
                              ctx ? ctx->getEGLContext() : EGL_NO_CONTEXT)) {
        ERR("eglMakeCurrent failed\n");
        return false;
    }

    //
    // Bind the surface(s) to the context
    //
    RenderThreadInfo* tinfo = RenderThreadInfo::get();
    WindowSurfacePtr bindDraw, bindRead;
    if (draw.get() == NULL && read.get() == NULL) {
        // Unbind the current read and draw surfaces from the context
        bindDraw = tinfo->currDrawSurf;
        bindRead = tinfo->currReadSurf;
    } else {
        bindDraw = draw;
        bindRead = read;
    }

    if (bindDraw.get() != NULL && bindRead.get() != NULL) {
        if (bindDraw.get() != bindRead.get()) {
            bindDraw->bind(ctx, WindowSurface::BIND_DRAW);
            bindRead->bind(ctx, WindowSurface::BIND_READ);
        } else {
            bindDraw->bind(ctx, WindowSurface::BIND_READDRAW);
        }
    }

    //
    // update thread info with current bound context
    //
    tinfo->currContext = ctx;
    tinfo->currDrawSurf = draw;
    tinfo->currReadSurf = read;
    if (ctx) {
        if (ctx->clientVersion() > GLESApi_CM)
            tinfo->m_gl2Dec.setContextData(&ctx->decoderContextData());
    } else {
        tinfo->m_gl2Dec.setContextData(NULL);
    }
    return true;
}

RenderContextPtr FrameBuffer::getContext_locked(HandleType p_context) {
    assert(m_lock.isLocked());
    return android::base::findOrDefault(m_contexts, p_context);
}

ColorBufferPtr FrameBuffer::getColorBuffer_locked(HandleType p_colorBuffer) {
    assert(m_lock.isLocked());
    return android::base::findOrDefault(m_colorbuffers, p_colorBuffer).cb;
}

WindowSurfacePtr FrameBuffer::getWindowSurface_locked(HandleType p_windowsurface) {
    assert(m_lock.isLocked());
    return android::base::findOrDefault(m_windows, p_windowsurface).first;
}

HandleType FrameBuffer::createClientImage(HandleType context,
                                          EGLenum target,
                                          GLuint buffer) {
    EGLContext eglContext = EGL_NO_CONTEXT;
    if (context) {
        AutoLock mutex(m_lock);
        RenderContextMap::const_iterator rcIt = m_contexts.find(context);
        if (rcIt == m_contexts.end()) {
            // bad context handle
            return false;
        }
        eglContext =
                rcIt->second ? rcIt->second->getEGLContext() : EGL_NO_CONTEXT;
    }

    EGLImageKHR image = s_egl.eglCreateImageKHR(
            m_eglDisplay, eglContext, target,
            reinterpret_cast<EGLClientBuffer>(buffer), NULL);
    HandleType imgHnd = (HandleType) reinterpret_cast<uintptr_t>(image);

    RenderThreadInfo* tInfo = RenderThreadInfo::get();
    uint64_t puid = tInfo->m_puid;
    if (puid) {
        AutoLock mutex(m_lock);
        m_procOwnedEGLImages[puid].insert(imgHnd);
    }
    return imgHnd;
}

EGLBoolean FrameBuffer::destroyClientImage(HandleType image) {
    // eglDestroyImageKHR has its own lock  already.
    EGLBoolean ret = s_egl.eglDestroyImageKHR(
            m_eglDisplay, reinterpret_cast<EGLImageKHR>(image));
    if (!ret)
        return false;
    RenderThreadInfo* tInfo = RenderThreadInfo::get();
    uint64_t puid = tInfo->m_puid;
    if (puid) {
        AutoLock mutex(m_lock);
        m_procOwnedEGLImages[puid].erase(image);
        // We don't explicitly call m_procOwnedEGLImages.erase(puid) when the
        // size reaches 0, since it could go between zero and one many times in
        // the lifetime of a process. It will be cleaned up by
        // cleanupProcGLObjects(puid) when the process is dead.
    }
    return true;
}

//
// The framebuffer lock should be held when calling this function !
//
bool FrameBuffer::bind_locked() {
    EGLContext prevContext = s_egl.eglGetCurrentContext();
    EGLSurface prevReadSurf = s_egl.eglGetCurrentSurface(EGL_READ);
    EGLSurface prevDrawSurf = s_egl.eglGetCurrentSurface(EGL_DRAW);

    if (prevContext != m_pbufContext || prevReadSurf != m_pbufSurface ||
        prevDrawSurf != m_pbufSurface) {
        if (!s_egl.eglMakeCurrent(m_eglDisplay, m_pbufSurface, m_pbufSurface,
                                  m_pbufContext)) {
            if (!m_shuttingDown)
                ERR("eglMakeCurrent failed\n");
            return false;
        }
    } else {
        ERR("Nested %s call detected, should never happen\n", __func__);
    }

    m_prevContext = prevContext;
    m_prevReadSurf = prevReadSurf;
    m_prevDrawSurf = prevDrawSurf;
    return true;
}

bool FrameBuffer::bindSubwin_locked() {
    EGLContext prevContext = s_egl.eglGetCurrentContext();
    EGLSurface prevReadSurf = s_egl.eglGetCurrentSurface(EGL_READ);
    EGLSurface prevDrawSurf = s_egl.eglGetCurrentSurface(EGL_DRAW);

    if (prevContext != m_eglContext || prevReadSurf != m_eglSurface ||
        prevDrawSurf != m_eglSurface) {
        if (!s_egl.eglMakeCurrent(m_eglDisplay, m_eglSurface, m_eglSurface,
                                  m_eglContext)) {
            ERR("eglMakeCurrent failed in binding subwindow!\n");
            return false;
        }
    }

    //
    // initialize GL state in eglContext if not yet initilaized
    //
    if (!m_eglContextInitialized) {
        m_eglContextInitialized = true;
    }

    m_prevContext = prevContext;
    m_prevReadSurf = prevReadSurf;
    m_prevDrawSurf = prevDrawSurf;
    return true;
}

bool FrameBuffer::bindFakeWindow_locked() {
    if (m_eglFakeWindowSurface == EGL_NO_SURFACE) {
        // initialize here
        m_eglFakeWindowContext = s_egl.eglCreateContext(
                m_eglDisplay, m_eglConfig, m_eglContext,
                getGlesMaxContextAttribs());

        static const EGLint kFakeWindowPbufAttribs[] = {
                EGL_WIDTH,          m_framebufferWidth, EGL_HEIGHT,
                m_framebufferWidth, EGL_NONE,
        };

        m_eglFakeWindowSurface = s_egl.eglCreatePbufferSurface(
                m_eglDisplay, m_eglConfig, kFakeWindowPbufAttribs);
    }

    if (!s_egl.eglMakeCurrent(m_eglDisplay, m_eglFakeWindowSurface,
                              m_eglFakeWindowSurface, m_eglFakeWindowContext)) {
        ERR("eglMakeCurrent failed in binding fake window!\n");
        return false;
    }
    return true;
}

bool FrameBuffer::unbind_locked() {
    EGLContext curContext = s_egl.eglGetCurrentContext();
    EGLSurface curReadSurf = s_egl.eglGetCurrentSurface(EGL_READ);
    EGLSurface curDrawSurf = s_egl.eglGetCurrentSurface(EGL_DRAW);

    if (m_prevContext != curContext || m_prevReadSurf != curReadSurf ||
        m_prevDrawSurf != curDrawSurf) {
        if (!s_egl.eglMakeCurrent(m_eglDisplay, m_prevDrawSurf, m_prevReadSurf,
                                  m_prevContext)) {
            return false;
        }
    }

    m_prevContext = EGL_NO_CONTEXT;
    m_prevReadSurf = EGL_NO_SURFACE;
    m_prevDrawSurf = EGL_NO_SURFACE;
    return true;
}

void FrameBuffer::createTrivialContext(HandleType shared,
                                       HandleType* contextOut,
                                       HandleType* surfOut) {
    assert(contextOut);
    assert(surfOut);

    *contextOut = createRenderContext(0, shared, GLESApi_2);
    // Zero size is formally allowed here, but SwiftShader doesn't like it and
    // fails.
    *surfOut = createWindowSurface(0, 1, 1);
}

void FrameBuffer::createAndBindTrivialSharedContext(EGLContext* contextOut,
                                                    EGLSurface* surfOut) {
    assert(contextOut);
    assert(surfOut);

    const FbConfig* config = getConfigs()->get(0 /* p_config */);
    if (!config) return;

    int maj, min;
    emugl::getGlesVersion(&maj, &min);

    const EGLint contextAttribs[] = {
        EGL_CONTEXT_MAJOR_VERSION_KHR, maj,
        EGL_CONTEXT_MINOR_VERSION_KHR, min,
        EGL_NONE };

    *contextOut = s_egl.eglCreateContext(
            m_eglDisplay, config->getEglConfig(), m_pbufContext, contextAttribs);

    const EGLint pbufAttribs[] = {
        EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };

    *surfOut = s_egl.eglCreatePbufferSurface(m_eglDisplay, config->getEglConfig(), pbufAttribs);

    s_egl.eglMakeCurrent(m_eglDisplay, *surfOut, *surfOut, *contextOut);
}

void FrameBuffer::unbindAndDestroyTrivialSharedContext(EGLContext context,
                                                       EGLSurface surface) {
    if (m_eglDisplay != EGL_NO_DISPLAY) {
        s_egl.eglMakeCurrent(m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
                             EGL_NO_CONTEXT);

        s_egl.eglDestroyContext(m_eglDisplay, context);
        s_egl.eglDestroySurface(m_eglDisplay, surface);
    }
}

bool FrameBuffer::post(HandleType p_colorbuffer, bool needLockAndBind) {
    bool res = postImpl(p_colorbuffer, needLockAndBind);
    if (res) setGuestPostedAFrame();
    return res;
}

bool FrameBuffer::postImpl(HandleType p_colorbuffer,
                           bool needLockAndBind,
                           bool repaint) {
    if (needLockAndBind) {
        m_lock.lock();
    }

    bool ret = false;

    ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
    if (c == m_colorbuffers.end()) {
        goto EXIT;
    }

    m_lastPostedColorBuffer = p_colorbuffer;

    ret = true;

    if (m_subWin) {
        markOpened(&c->second);
        c->second.cb->touch();

        Post postCmd;
        postCmd.cmd = PostCmd::Post;
        postCmd.cb = c->second.cb.get();
        sendPostWorkerCmd(postCmd);
    } else {
        markOpened(&c->second);
        c->second.cb->touch();
        c->second.cb->waitSync();
        c->second.cb->scale();
        s_gles2.glFlush();

        // If there is no sub-window, don't display anything, the client will
        // rely on m_onPost to get the pixels instead.
        ret = true;
    }

    //
    //
    // Send framebuffer (without FPS overlay) to callback
    //
    if (m_onPost.size() == 0) {
        goto EXIT;
    }
    for (auto& iter : m_onPost) {
        ColorBufferPtr cb;
        if (iter.first == 0) {
            cb = c->second.cb;
        } else {
            uint32_t colorBuffer;
            if (getDisplayColorBuffer(iter.first, &colorBuffer) < 0) {
                ERR("Failed to get color buffer for display %d, skip onPost", iter.first);
                continue;
            }
            cb = findColorBuffer(colorBuffer);
            if (!cb) {
                ERR("Failed to find colorbuffer %d, skip onPost", colorBuffer);
                continue;
            }
        }

        if (m_asyncReadbackSupported) {
            ensureReadbackWorker();
            m_readbackWorker->doNextReadback(iter.first, cb.get(), iter.second.img,
                repaint, iter.second.readBgra);
        } else {
            cb->readback(iter.second.img, iter.second.readBgra);
            doPostCallback(iter.second.img, iter.first);
        }
    }

EXIT:
    if (needLockAndBind) {
        m_lock.unlock();
    }
    return ret;
}

void FrameBuffer::doPostCallback(void* pixels, uint32_t displayId) {
    const auto& iter = m_onPost.find(displayId);
    if (iter == m_onPost.end()) {
        ERR("Cannot find post callback function for display %d", displayId);
        return;
    }
    iter->second.cb(iter->second.context, displayId, iter->second.width,
                    iter->second.height, -1, GL_RGBA, GL_UNSIGNED_BYTE,
                    (unsigned char*)pixels);
}

void FrameBuffer::getPixels(void* pixels, uint32_t bytes, uint32_t displayId) {
    const auto& iter = m_onPost.find(displayId);
    if (iter == m_onPost.end()) {
        ERR("Display %d not configured for recording yet", displayId);
        return;
    }
    m_readbackThread.enqueue({ ReadbackCmd::GetPixels, displayId,
                                           0, pixels, bytes });
    m_readbackThread.waitQueuedItems();
}

void FrameBuffer::flushReadPipeline(int displayId) {
    const auto& iter = m_onPost.find(displayId);
    if (iter == m_onPost.end()) {
        ERR("Cannot find onPost pixels for display %d", displayId);
        return;
    }

    ensureReadbackWorker();
    m_readbackWorker->flushPipeline(displayId);
}

void FrameBuffer::ensureReadbackWorker() {
    if (!m_readbackWorker) m_readbackWorker.reset(new ReadbackWorker);
}

static void sFrameBuffer_ReadPixelsCallback(
    void* pixels, uint32_t bytes, uint32_t displayId) {
    FrameBuffer::getFB()->getPixels(pixels, bytes, displayId);
}

static void sFrameBuffer_FlushReadPixelPipeline(int displayId) {
    FrameBuffer::getFB()->flushReadPipeline(displayId);
}

bool FrameBuffer::asyncReadbackSupported() {
    return m_asyncReadbackSupported;
}

emugl::Renderer::ReadPixelsCallback
FrameBuffer::getReadPixelsCallback() {
    return sFrameBuffer_ReadPixelsCallback;
}

emugl::Renderer::FlushReadPixelPipeline FrameBuffer::getFlushReadPixelPipeline() {
    return sFrameBuffer_FlushReadPixelPipeline;
}

bool FrameBuffer::repost(bool needLockAndBind) {
    GL_LOG("Reposting framebuffer.");
    if (m_lastPostedColorBuffer &&
        sInitialized.load(std::memory_order_relaxed)) {
        GL_LOG("Has last posted colorbuffer and is initialized; post.");
        return postImpl(m_lastPostedColorBuffer, needLockAndBind,
                        true /* need repaint */);
    } else {
        GL_LOG("No repost: no last posted color buffer");
        if (!sInitialized.load(std::memory_order_relaxed)) {
            GL_LOG("No repost: initialization is not finished.");
        }
    }
    return false;
}

template <class Collection>
static void saveProcOwnedCollection(Stream* stream, const Collection& c) {
    // Exclude empty handle lists from saving as they add no value but only
    // increase the snapshot size; keep the format compatible with
    // android::base::saveCollection() though.
    const int count =
            std::count_if(c.begin(), c.end(),
                          [](const typename Collection::value_type& pair) {
                              return !pair.second.empty();
                          });
    stream->putBe32(count);
    for (const auto& pair : c) {
        if (pair.second.empty()) {
            continue;
        }
        stream->putBe64(pair.first);
        saveCollection(stream, pair.second,
                       [](Stream* s, HandleType h) { s->putBe32(h); });
    }
}

template <class Collection>
static void loadProcOwnedCollection(Stream* stream, Collection* c) {
    loadCollection(stream, c,
                   [](Stream* stream) -> typename Collection::value_type {
        const int processId = stream->getBe64();
        typename Collection::mapped_type handles;
        loadCollection(stream, &handles, [](Stream* s) { return s->getBe32(); });
        return { processId, std::move(handles) };
    });
}

void FrameBuffer::getScreenshot(unsigned int nChannels, unsigned int* width,
        unsigned int* height, std::vector<unsigned char>& pixels, int displayId,
        int desiredWidth, int desiredHeight, SkinRotation desiredRotation) {
    AutoLock mutex(m_lock);
    uint32_t w, h, cb;
    if (false) {
        fprintf(stderr, "Screenshot of invalid display %d", displayId);
        *width = 0;
        *height = 0;
        pixels.resize(0);
        return;
    }
    if (nChannels != 3 && nChannels != 4) {
        fprintf(stderr, "Screenshot only support 3(RGB) or 4(RGBA) channels");
        *width = 0;
        *height = 0;
        pixels.resize(0);
        return;
    }

        cb = m_lastPostedColorBuffer;

    ColorBufferMap::iterator c(m_colorbuffers.find(cb));
    if (c == m_colorbuffers.end()) {
        *width = 0;
        *height = 0;
        pixels.resize(0);
        return;
    }

    *width = (desiredWidth == 0) ? w : desiredWidth;
    *height = (desiredHeight == 0) ? h : desiredHeight;
    if (desiredRotation == SKIN_ROTATION_90 || desiredRotation == SKIN_ROTATION_270) {
        std::swap(*width, *height);
    }
    pixels.resize(4 * (*width) * (*height));

    GLenum format = nChannels == 3 ? GL_RGB : GL_RGBA;

    Post scrCmd;
    scrCmd.cmd = PostCmd::Screenshot;
    scrCmd.screenshot.cb = c->second.cb.get();
    scrCmd.screenshot.screenwidth = *width;
    scrCmd.screenshot.screenheight = *height;
    scrCmd.screenshot.format = format;
    scrCmd.screenshot.type = GL_UNSIGNED_BYTE;
    scrCmd.screenshot.rotation = desiredRotation;
    scrCmd.screenshot.pixels = pixels.data();

    sendPostWorkerCmd(scrCmd);
}

void FrameBuffer::onLastColorBufferRef(uint32_t handle) {
    if (!mOutstandingColorBufferDestroys.trySend((HandleType)handle)) {
        fprintf(
            stderr,
            "%s: warning: too many outstanding "
            "color buffer destroys. leaking handle 0x%x\n",
            __func__, handle);
    }
}

bool FrameBuffer::decColorBufferRefCountLocked(HandleType p_colorbuffer) {
    const auto& it = m_colorbuffers.find(p_colorbuffer);
    if (it != m_colorbuffers.end()) {
        it->second.refcount -= 1;
        if (it->second.refcount == 0) {
            m_colorbuffers.erase(p_colorbuffer);
            return true;
        }
    }
    return false;
}

bool FrameBuffer::compose(uint32_t bufferSize, void* buffer) {
    ComposeDevice* p = (ComposeDevice*)buffer;
    AutoLock mutex(m_lock);

    switch (p->version) {
    case 1: {
        Post composeCmd;
        composeCmd.cmd = PostCmd::Compose;
        composeCmd.d = p;
        sendPostWorkerCmd(composeCmd);
        post(p->targetHandle, false);
        return true;
    }

    case 2: {
       // support for multi-display
       ComposeDevice_v2* p2 = (ComposeDevice_v2*)buffer;
       if (p2->displayId != 0) {
            mutex.unlock();
            setDisplayColorBuffer(p2->displayId, p2->targetHandle);
            mutex.lock();
       }
       Post composeCmd;
       composeCmd.cmd = PostCmd::Compose;
       composeCmd.d = p;
       sendPostWorkerCmd(composeCmd);
       if (p2->displayId == 0) {
           post(p2->targetHandle, false);
       }
       return true;
    }

    default:
       fprintf(stderr, "yet to handle composition device version: %d\n", p->version);
       return false;
    }
}

void FrameBuffer::onSave(Stream* stream,
                         const android::snapshot::ITextureSaverPtr& textureSaver) {
    // Things we do not need to snapshot:
    //     m_eglSurface
    //     m_eglContext
    //     m_pbufSurface
    //     m_pbufContext
    //     m_prevContext
    //     m_prevReadSurf
    //     m_prevDrawSurf
    AutoLock mutex(m_lock);
    // set up a context because some snapshot commands try using GL
    ScopedBind scopedBind(m_colorBufferHelper);
    // eglPreSaveContext labels all guest context textures to be saved
    // (textures created by the host are not saved!)
    // eglSaveAllImages labels all EGLImages (both host and guest) to be saved
    // and save all labeled textures and EGLImages.
    if (s_egl.eglPreSaveContext && s_egl.eglSaveAllImages) {
        for (const auto& ctx : m_contexts) {
            s_egl.eglPreSaveContext(m_eglDisplay, ctx.second->getEGLContext(),
                    stream);
        }
        s_egl.eglSaveAllImages(m_eglDisplay, stream, &textureSaver);
    }
    // Don't save subWindow's x/y/w/h here - those are related to the current
    // emulator UI state, not guest state that we're saving.
    stream->putBe32(m_framebufferWidth);
    stream->putBe32(m_framebufferHeight);
    stream->putFloat(m_dpr);

    stream->putBe32(m_useSubWindow);
    stream->putBe32(m_eglContextInitialized);

    stream->putBe32(m_fpsStats);
    stream->putBe32(m_statsNumFrames);
    stream->putBe64(m_statsStartTime);

    // Save all contexts.
    // Note: some of the contexts might not be restored yet. In such situation
    // we skip reading from GPU (for non-texture objects) or force a restore in
    // previous eglPreSaveContext and eglSaveAllImages calls (for texture
    // objects).
    // TODO: skip reading from GPU even for texture objects.
    saveCollection(stream, m_contexts,
                   [](Stream* s, const RenderContextMap::value_type& pair) {
        pair.second->onSave(s);
    });

    // We don't need to save |m_colorBufferCloseTsMap| here - there's enough
    // information to reconstruct it when loading.
    System::Duration now = System::get()->getUnixTime();

    saveCollection(stream, m_colorbuffers,
                   [now](Stream* s, const ColorBufferMap::value_type& pair) {
        pair.second.cb->onSave(s);
        s->putBe32(pair.second.refcount);
        s->putByte(pair.second.opened);
        s->putBe32(std::max<System::Duration>(0, now - pair.second.closedTs));
    });
    stream->putBe32(m_lastPostedColorBuffer);
    saveCollection(stream, m_windows,
                   [](Stream* s, const WindowSurfaceMap::value_type& pair) {
        pair.second.first->onSave(s);
        s->putBe32(pair.second.second); // Color buffer handle.
    });

    saveProcOwnedCollection(stream, m_procOwnedWindowSurfaces);
    saveProcOwnedCollection(stream, m_procOwnedEGLImages);
    saveProcOwnedCollection(stream, m_procOwnedRenderContext);

    if (s_egl.eglPostSaveContext) {
        for (const auto& ctx : m_contexts) {
            s_egl.eglPostSaveContext(m_eglDisplay, ctx.second->getEGLContext(),
                    stream);
        }
        // We need to run the post save step for m_eglContext and m_pbufContext
        // to mark their texture handles dirty
        if (m_eglContext != EGL_NO_CONTEXT) {
            s_egl.eglPostSaveContext(m_eglDisplay, m_eglContext, stream);
        }
        if (m_pbufContext != EGL_NO_CONTEXT) {
            s_egl.eglPostSaveContext(m_eglDisplay, m_pbufContext, stream);
        }
    }

}

bool FrameBuffer::onLoad(Stream* stream,
                         const android::snapshot::ITextureLoaderPtr& textureLoader) {
    AutoLock lock(m_lock);
    // cleanups
    {
        sweepColorBuffersLocked();

        ScopedBind scopedBind(m_colorBufferHelper);
        if (m_procOwnedWindowSurfaces.empty() &&
            m_procOwnedColorBuffers.empty() && m_procOwnedEGLImages.empty() &&
            m_procOwnedRenderContext.empty() &&
            m_procOwnedCleanupCallbacks.empty() &&
            (!m_contexts.empty() || !m_windows.empty() ||
             m_colorbuffers.size() > m_colorBufferDelayedCloseList.size())) {
            // we are likely on a legacy system image, which does not have
            // process owned objects. We need to force cleanup everything
            m_contexts.clear();
            m_windows.clear();
            m_colorbuffers.clear();
        } else {
            std::vector<HandleType> colorBuffersToCleanup;

            while (m_procOwnedWindowSurfaces.size()) {
                auto cleanupHandles = cleanupProcGLObjects_locked(
                        m_procOwnedWindowSurfaces.begin()->first, true);
                colorBuffersToCleanup.insert(colorBuffersToCleanup.end(),
                    cleanupHandles.begin(), cleanupHandles.end());
            }
            while (m_procOwnedColorBuffers.size()) {
                auto cleanupHandles = cleanupProcGLObjects_locked(
                        m_procOwnedColorBuffers.begin()->first, true);
                colorBuffersToCleanup.insert(colorBuffersToCleanup.end(),
                    cleanupHandles.begin(), cleanupHandles.end());
            }
            while (m_procOwnedEGLImages.size()) {
                auto cleanupHandles = cleanupProcGLObjects_locked(
                        m_procOwnedEGLImages.begin()->first, true);
                colorBuffersToCleanup.insert(colorBuffersToCleanup.end(),
                    cleanupHandles.begin(), cleanupHandles.end());
            }
            while (m_procOwnedRenderContext.size()) {
                auto cleanupHandles = cleanupProcGLObjects_locked(
                        m_procOwnedRenderContext.begin()->first, true);
                colorBuffersToCleanup.insert(colorBuffersToCleanup.end(),
                    cleanupHandles.begin(), cleanupHandles.end());
            }

            std::vector<std::function<void()>> cleanupCallbacks;

            while (m_procOwnedCleanupCallbacks.size()) {
                auto it = m_procOwnedCleanupCallbacks.begin();
                while (it != m_procOwnedCleanupCallbacks.end()) {
                    for (auto it2 : it->second) {
                        cleanupCallbacks.push_back(it2.second);
                    }
                    it = m_procOwnedCleanupCallbacks.erase(it);
                }
            }

            performDelayedColorBufferCloseLocked(true);

            lock.unlock();

            for (auto cb : cleanupCallbacks) {
                cb();
            }

            lock.lock();
        }
        m_colorBufferDelayedCloseList.clear();
        assert(m_contexts.empty());
        assert(m_windows.empty());
        if (!m_colorbuffers.empty()) {
            fprintf(stderr, "%s: warning: on load, stale colorbuffers: %zu\n", __func__, m_colorbuffers.size());
            m_colorbuffers.clear();
        }
        assert(m_colorbuffers.empty());
#ifdef SNAPSHOT_PROFILE
        System::Duration texTime = System::get()->getUnixTimeUs();
#endif
        if (s_egl.eglLoadAllImages) {
            s_egl.eglLoadAllImages(m_eglDisplay, stream, &textureLoader);
        }
#ifdef SNAPSHOT_PROFILE
        printf("Texture load time: %lld ms\n",
               (long long)(System::get()->getUnixTimeUs() - texTime) / 1000);
#endif
    }
    // See comment about subwindow position in onSave().
    m_framebufferWidth = stream->getBe32();
    m_framebufferHeight = stream->getBe32();
    m_dpr = stream->getFloat();
    // TODO: resize the window
    //
    m_useSubWindow = stream->getBe32();
    m_eglContextInitialized = stream->getBe32();

    m_fpsStats = stream->getBe32();
    m_statsNumFrames = stream->getBe32();
    m_statsStartTime = stream->getBe64();

    loadCollection(stream, &m_contexts,
                   [this](Stream* stream) -> RenderContextMap::value_type {
        RenderContextPtr ctx(RenderContext::onLoad(stream, m_eglDisplay));
        return { ctx ? ctx->getHndl() : 0, ctx };
    });
    assert(!android::base::find(m_contexts, 0));

    auto now = System::get()->getUnixTime();
    loadCollection(stream, &m_colorbuffers,
                   [this, now](Stream* stream) -> ColorBufferMap::value_type {
        ColorBufferPtr cb(ColorBuffer::onLoad(stream, m_eglDisplay,
                                              m_colorBufferHelper,
                                              m_fastBlitSupported));
        const HandleType handle = cb->getHndl();
        const unsigned refCount = stream->getBe32();
        const bool opened = stream->getByte();
        const System::Duration closedTs = now - stream->getBe32();
        if (refCount == 0) {
            m_colorBufferDelayedCloseList.push_back({closedTs, handle});
        }
        return { handle, { std::move(cb), refCount, opened, closedTs } };
    });
    m_lastPostedColorBuffer = static_cast<HandleType>(stream->getBe32());
    GL_LOG("Got lasted posted color buffer from snapshot");

    loadCollection(stream, &m_windows,
                   [this](Stream* stream) -> WindowSurfaceMap::value_type {
        WindowSurfacePtr window(WindowSurface::onLoad(stream, m_eglDisplay));
        HandleType handle = window->getHndl();
        HandleType colorBufferHandle = stream->getBe32();
        return { handle, { std::move(window), colorBufferHandle } };
    });

    loadProcOwnedCollection(stream, &m_procOwnedWindowSurfaces);
    loadProcOwnedCollection(stream, &m_procOwnedEGLImages);
    loadProcOwnedCollection(stream, &m_procOwnedRenderContext);

    if (s_egl.eglPostLoadAllImages) {
        s_egl.eglPostLoadAllImages(m_eglDisplay, stream);
    }


    {
        ScopedBind scopedBind(m_colorBufferHelper);
        for (auto& it : m_colorbuffers) {
            if (it.second.cb) {
                it.second.cb->touch();
            }
        }
    }

    return true;
    // TODO: restore memory management
}

void FrameBuffer::lock() {
    m_lock.lock();
}

void FrameBuffer::unlock() {
    m_lock.unlock();
}

ColorBufferPtr FrameBuffer::findColorBuffer(HandleType p_colorbuffer) {
    ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
    if (c == m_colorbuffers.end()) {
        return nullptr;
    }
    else {
        return c->second.cb;
    }
}

void FrameBuffer::registerProcessCleanupCallback(void* key, std::function<void()> cb) {
    AutoLock mutex(m_lock);
    RenderThreadInfo* tInfo = RenderThreadInfo::get();
    if (!tInfo) return;

    auto& callbackMap = m_procOwnedCleanupCallbacks[tInfo->m_puid];
    callbackMap[key] = cb;
}

void FrameBuffer::unregisterProcessCleanupCallback(void* key) {
    AutoLock mutex(m_lock);
    RenderThreadInfo* tInfo = RenderThreadInfo::get();
    if (!tInfo) return;

    auto& callbackMap = m_procOwnedCleanupCallbacks[tInfo->m_puid];
    if (callbackMap.find(key) == callbackMap.end()) {
        fprintf(
            stderr,
            "%s: warning: tried to erase nonexistent key %p "
            "associated with process %llu\n",
            __func__, key, (unsigned long long)(tInfo->m_puid));
    }
    callbackMap.erase(key);
}
int FrameBuffer::createDisplay(uint32_t *displayId) {
    return 0;
}

int FrameBuffer::destroyDisplay(uint32_t displayId) {
    return 0;
}

int FrameBuffer::setDisplayColorBuffer(uint32_t displayId, uint32_t colorBuffer) {
    return 0;
}

int FrameBuffer::getDisplayColorBuffer(uint32_t displayId, uint32_t* colorBuffer) {
    return 0;
}

int FrameBuffer::getColorBufferDisplay(uint32_t colorBuffer, uint32_t* displayId) {
    return 0;
}

int FrameBuffer::getDisplayPose(uint32_t displayId,
                                int32_t* x,
                                int32_t* y,
                                uint32_t* w,
                                uint32_t* h) {
    return 0;
}

int FrameBuffer::setDisplayPose(uint32_t displayId,
                                int32_t x,
                                int32_t y,
                                uint32_t w,
                                uint32_t h,
                                uint32_t dpi) {
    return 0;
}

void FrameBuffer::sweepColorBuffersLocked() {
    HandleType handleToDestroy;
    while (mOutstandingColorBufferDestroys.tryReceive(&handleToDestroy)) {
        bool needCleanup = decColorBufferRefCountLocked(handleToDestroy);
        if (needCleanup) {
            m_lock.unlock();
            m_lock.lock();
        }
    }
}

void FrameBuffer::waitForGpu(uint64_t eglsync) {
    FenceSync* fenceSync = FenceSync::getFromHandle(eglsync);

    if (!fenceSync) {
        fprintf(stderr, "%s: err: fence sync 0x%llx not found\n", __func__,
                (unsigned long long)eglsync);
        return;
    }

    SyncThread::get()->triggerBlockedWaitNoTimeline(fenceSync);
}
void FrameBuffer::SetProcNameOfThread(const char* procName, uint32_t processNameSize)
{
    std::string strProcName(procName, procName + processNameSize);
    RenderThreadInfo *tinfo = RenderThreadInfo::get();
    tinfo->procName = strProcName;
}
EGLint FrameBuffer::getMatchConfigs(EGLint hostConfig)
{
    if (m_configs == nullptr) {
        ERR("Failed to get config:%d", hostConfig);
        return -1;
    }
    return m_configs->getMatchConfigs(hostConfig);
}
