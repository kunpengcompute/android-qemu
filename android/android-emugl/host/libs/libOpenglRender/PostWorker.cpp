#include "PostWorker.h"

#include "ColorBuffer.h"
#include "DispatchTables.h"
#include "FrameBuffer.h"
#include "RenderThreadInfo.h"
#include "OpenGLESDispatch/EGLDispatch.h"
#include "OpenGLESDispatch/GLESv2Dispatch.h"
#include "emugl/common/misc.h"

#define POST_DEBUG 0
#if POST_DEBUG >= 1
#define DD(fmt, ...) \
    DBG("%s:%d| " fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define DD(fmt, ...) (void)0
#endif

PostWorker::PostWorker(PostWorker::BindSubwinCallback&& cb) :
    mFb(FrameBuffer::getFB()),
    mBindSubwin(cb) {}

void PostWorker::fillMultiDisplayPostStruct(ComposeLayer* l, int32_t x, int32_t y,
                                            uint32_t w, uint32_t h, ColorBuffer* cb) {
    l->composeMode = HWC2_COMPOSITION_DEVICE;
    l->blendMode = HWC2_BLEND_MODE_NONE;
    l->transform = (hwc_transform_t)0;
    l->alpha = 1.0;
    l->displayFrame.left = x;
    l->displayFrame.top = y;
    l->displayFrame.right = x + w;
    l->displayFrame.bottom =  y + h;
    l->crop.left = 0.0;
    l->crop.top = (float)cb->getHeight();
    l->crop.right = (float)cb->getWidth();
    l->crop.bottom = 0.0;
}

void PostWorker::post(ColorBuffer* cb) {
    // bind the subwindow eglSurface
    if (!m_initialized) {
        m_initialized = mBindSubwin();
    }

    float dpr = mFb->getDpr();
    int windowWidth = mFb->windowWidth();
    int windowHeight = mFb->windowHeight();
    float px = mFb->getPx();
    float py = mFb->getPy();
    int zRot = mFb->getZrot();

    cb->waitSync();
    if (zRot == 90 || zRot == 270) {
        s_gles2.glViewport(0, 0, mFb->getGuestHeight(), mFb->getGuestWidth());
    } else {
        s_gles2.glViewport(0, 0, mFb->getGuestWidth(), mFb->getGuestHeight());
    }
    // Find the x and y values at the origin when "fully scrolled."
    // Multiply by 2 because the texture goes from -1 to 1, not 0 to 1.
    // Multiply the windowing coordinates by DPR because they ignore
    // DPR, but the viewport includes DPR.
    float fx = 2.f * (m_viewportWidth  - windowWidth  * dpr) / (float)m_viewportWidth;
    float fy = 2.f * (m_viewportHeight - windowHeight * dpr) / (float)m_viewportHeight;

    // finally, compute translation values
    float dx = px * fx;
    float dy = py * fy;
    bool isSkipFlush = false;
    // render the color buffer to the window and apply the overlay
    GLuint tex = cb->scale(isSkipFlush);
    uint32_t encTexture = 0; // call nv interface get encode texture handle to copy render fb
    cb->postWithOverlay(tex, zRot, dx, dy, encTexture, isSkipFlush);
    s_egl.eglSwapBuffers(mFb->getDisplay(), mFb->getWindowSurface());
}

// Called whenever the subwindow needs a refresh (FrameBuffer::setupSubWindow).
// This rebinds the subwindow context (to account for
// when the refresh is a display change, for instance)
// and resets the posting viewport.
void PostWorker::viewport(int width, int height) {
    // rebind the subwindow eglSurface unconditionally---
    // this could be from a display change
    m_initialized = mBindSubwin();

    float dpr = mFb->getDpr();
    m_viewportWidth = width * dpr;
    m_viewportHeight = height * dpr;
    s_gles2.glViewport(0, 0, m_viewportWidth, m_viewportHeight);
}

// Called when the subwindow refreshes, but there is no
// last posted color buffer to show to the user. Instead of
// displaying whatever happens to be in the back buffer,
// clear() is useful for outputting consistent colors.
void PostWorker::clear() {
    s_gles2.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
                    GL_STENCIL_BUFFER_BIT);
    s_egl.eglSwapBuffers(mFb->getDisplay(), mFb->getWindowSurface());
}

void PostWorker::resetSubWindow() {
    if (m_initialized) {
        INFO("reset sub window");
        if (mFb->getDisplay() != EGL_NO_DISPLAY) {
            s_egl.eglMakeCurrent(mFb->getDisplay(), EGL_NO_SURFACE, EGL_NO_SURFACE,
                             EGL_NO_CONTEXT);
        }
        m_initialized = false;
    }
}

void PostWorker::compose(ComposeDevice* p) {
    // bind the subwindow eglSurface
    if (!m_initialized) {
        m_initialized = mBindSubwin();
    }

    ComposeLayer* l = (ComposeLayer*)p->layer;
    GLint vport[4] = { 0, };
    s_gles2.glGetIntegerv(GL_VIEWPORT, vport);
    s_gles2.glViewport(0, 0, mFb->getWidth(),mFb->getHeight());
    if (!m_composeFbo) {
        s_gles2.glGenFramebuffers(1, &m_composeFbo);
    }
    s_gles2.glBindFramebuffer(GL_FRAMEBUFFER, m_composeFbo);
    s_gles2.glFramebufferTexture2D(GL_FRAMEBUFFER,
                                   GL_COLOR_ATTACHMENT0_OES,
                                   GL_TEXTURE_2D,
                                   mFb->findColorBuffer(p->targetHandle)->getTexture(),
                                   0);

    DD("worker compose %d layers\n", p->numLayers);
    mFb->getTextureDraw()->prepareForDrawLayer();
    for (int i = 0; i < p->numLayers; i++, l++) {
        DD("\tcomposeMode %d color %d %d %d %d blendMode "
               "%d alpha %f transform %d %d %d %d %d "
               "%f %f %f %f\n",
               l->composeMode, l->color.r, l->color.g, l->color.b,
               l->color.a, l->blendMode, l->alpha, l->transform,
               l->displayFrame.left, l->displayFrame.top,
               l->displayFrame.right, l->displayFrame.bottom,
               l->crop.left, l->crop.top, l->crop.right,
               l->crop.bottom);
        composeLayer(l);
    }

    mFb->findColorBuffer(p->targetHandle)->setSync();
    s_gles2.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    s_gles2.glViewport(vport[0], vport[1], vport[2], vport[3]);
    mFb->getTextureDraw()->cleanupForDrawLayer();
}

void PostWorker::compose(ComposeDevice_v2* p) {
    // bind the subwindow eglSurface
    if (!m_initialized) {
        m_initialized = mBindSubwin();
    }

    ComposeLayer* l = (ComposeLayer*)p->layer;
    GLint vport[4] = { 0, };
    s_gles2.glGetIntegerv(GL_VIEWPORT, vport);
    s_gles2.glViewport(0, 0, mFb->getWidth(),mFb->getHeight());
    if (!m_composeFbo) {
        s_gles2.glGenFramebuffers(1, &m_composeFbo);
    }
    s_gles2.glBindFramebuffer(GL_FRAMEBUFFER, m_composeFbo);
    s_gles2.glFramebufferTexture2D(GL_FRAMEBUFFER,
                                   GL_COLOR_ATTACHMENT0_OES,
                                   GL_TEXTURE_2D,
                                   mFb->findColorBuffer(p->targetHandle)->getTexture(),
                                   0);

    DD("worker compose %d layers\n", p->numLayers);
    mFb->getTextureDraw()->prepareForDrawLayer();
    for (int i = 0; i < p->numLayers; i++, l++) {
        INFO("\tCBHandle:%u composeMode %d color %d %d %d %d blendMode "
               "%d alpha %f transform %d %d %d %d %d "
               "%f %f %f %f\n", l->cbHandle,
               l->composeMode, l->color.r, l->color.g, l->color.b,
               l->color.a, l->blendMode, l->alpha, l->transform,
               l->displayFrame.left, l->displayFrame.top,
               l->displayFrame.right, l->displayFrame.bottom,
               l->crop.left, l->crop.top, l->crop.right,
               l->crop.bottom);
        composeLayer(l);
    }

    mFb->findColorBuffer(p->targetHandle)->setSync();
    s_gles2.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    s_gles2.glViewport(vport[0], vport[1], vport[2], vport[3]);
    mFb->getTextureDraw()->cleanupForDrawLayer();
}

void PostWorker::composeLayer(ComposeLayer* l) {
    if (l->composeMode == HWC2_COMPOSITION_DEVICE) {
        ColorBufferPtr cb = mFb->findColorBuffer(l->cbHandle);
        if (cb == nullptr) {
            // bad colorbuffer handle
            ERR("%s: fail to find colorbuffer %d\n", __FUNCTION__, l->cbHandle);
            return;
        }
        cb->postLayer(l, mFb->getWidth(), mFb->getHeight());
    }
    else {
        // no Colorbuffer associated with SOLID_COLOR mode
        mFb->getTextureDraw()->drawLayer(l, mFb->getWidth(), mFb->getHeight(),
                                         1, 1, 0);
    }
}

void PostWorker::screenshot(
    ColorBuffer* cb,
    int width,
    int height,
    GLenum format,
    GLenum type,
    SkinRotation rotation,
    void* pixels) {
    cb->readPixelsScaled(
        width, height, format, type, rotation, pixels);
}

PostWorker::~PostWorker() {
    if (mFb->getDisplay() != EGL_NO_DISPLAY) {
        s_egl.eglMakeCurrent(mFb->getDisplay(), EGL_NO_SURFACE, EGL_NO_SURFACE,
                             EGL_NO_CONTEXT);
    }
}
