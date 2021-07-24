// Copyright (C) 2015 The Android Open Source Project
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

#include "FbConfigMy.h"

#include <stdio.h>
#include <string.h>
#include <sstream>
#include <iomanip> 
#include "FrameBuffer.h"
#include "GLESVersionDetector.h"
#include "emugl/common/misc.h"
#include "OpenGLESDispatch/EGLDispatch.h"

namespace {

const GLuint kConfigAttributes[] = {
    EGL_DEPTH_SIZE,     // must be first - see getDepthSize()
    EGL_STENCIL_SIZE,   // must be second - see getStencilSize()
    EGL_RENDERABLE_TYPE,// must be third - see getRenderableType()
    EGL_SURFACE_TYPE,   // must be fourth - see getSurfaceType()
    EGL_CONFIG_ID,      // must be fifth  - see chooseConfig()
    EGL_BUFFER_SIZE,
    EGL_ALPHA_SIZE,
    EGL_BLUE_SIZE,
    EGL_GREEN_SIZE,
    EGL_RED_SIZE,
    EGL_CONFIG_CAVEAT,
    EGL_LEVEL,
    EGL_MAX_PBUFFER_HEIGHT,
    EGL_MAX_PBUFFER_PIXELS,
    EGL_MAX_PBUFFER_WIDTH,
    EGL_NATIVE_RENDERABLE,
    EGL_NATIVE_VISUAL_ID,
    EGL_NATIVE_VISUAL_TYPE,
    EGL_SAMPLES,
    EGL_SAMPLE_BUFFERS,
    EGL_TRANSPARENT_TYPE,
    EGL_TRANSPARENT_BLUE_VALUE,
    EGL_TRANSPARENT_GREEN_VALUE,
    EGL_TRANSPARENT_RED_VALUE,
    EGL_BIND_TO_TEXTURE_RGB,
    EGL_BIND_TO_TEXTURE_RGBA,
    EGL_MIN_SWAP_INTERVAL,
    EGL_MAX_SWAP_INTERVAL,
    EGL_LUMINANCE_SIZE,
    EGL_ALPHA_MASK_SIZE,
    EGL_COLOR_BUFFER_TYPE,
    EGL_RECORDABLE_ANDROID,
    EGL_CONFORMANT
};

const size_t kConfigAttributesLen =
        sizeof(kConfigAttributes) / sizeof(kConfigAttributes[0]);

const size_t kServerConfigCount = 9;
// 服务端的config，若服务端的config变化了，这里的数据需改变
const EGLint serverConfig[kServerConfigCount][kConfigAttributesLen] = {
    {0,0,69,5,1,32,8,8,8,8,12344,0,4096,0,4096,1,1,1,0,0,12344,0,0,0,0,1,0,1,0,0,12430,1,69},
    {24,8,69,5,2,32,8,8,8,8,12344,0,4096,0,4096,1,1,1,0,0,12344,0,0,0,0,1,0,1,0,0,12430,1,69},
    {24,8,69,5,3,32,8,8,8,8,12344,0,4096,0,4096,1,1,1,4,1,12344,0,0,0,0,1,0,1,0,0,12430,1,69},
    {0,0,69,5,4,24,0,8,8,8,12344,0,4096,0,4096,1,2,2,0,0,12344,0,0,0,0,0,0,1,0,0,12430,1,69},
    {24,8,69,5,5,24,0,8,8,8,12344,0,4096,0,4096,1,2,2,0,0,12344,0,0,0,0,0,0,1,0,0,12430,1,69},
    {24,8,69,5,6,24,0,8,8,8,12344,0,4096,0,4096,1,2,2,4,1,12344,0,0,0,0,0,0,1,0,0,12430,1,69},
    {0,0,69,5,7,16,0,5,6,5,12344,0,4096,0,4096,1,4,4,0,0,12344,0,0,0,0,0,0,1,0,0,12430,1,69},
    {24,8,69,5,8,16,0,5,6,5,12344,0,4096,0,4096,1,4,4,0,0,12344,0,0,0,0,0,0,1,0,0,12430,1,69},
    {24,8,69,5,9,16,0,5,6,5,12344,0,4096,0,4096,1,4,4,4,1,12344,0,0,0,0,0,0,1,0,0,12430,1,69}
};

bool isCompatibleHostConfig(EGLConfig config, EGLDisplay display) {
    // Filter out configs which do not support pbuffers, since they
    // are used to implement window surfaces.
    EGLint surfaceType;
    s_egl.eglGetConfigAttrib(
            display, config, EGL_SURFACE_TYPE, &surfaceType);
    if (!(surfaceType & EGL_PBUFFER_BIT)) {
        return false;
    }

    // Filter out configs that do not support RGB pixel values.
    EGLint redSize = 0, greenSize = 0, blueSize = 0;
    s_egl.eglGetConfigAttrib(
            display, config,EGL_RED_SIZE, &redSize);
    s_egl.eglGetConfigAttrib(
            display, config, EGL_GREEN_SIZE, &greenSize);
    s_egl.eglGetConfigAttrib(
            display, config, EGL_BLUE_SIZE, &blueSize);

    if (!redSize || !greenSize || !blueSize) {
        return false;
    }

    return true;
}

}  // namespace

FbConfig::~FbConfig() {
    delete [] mAttribValues;
}

FbConfig::FbConfig(EGLConfig hostConfig, EGLDisplay hostDisplay) :
        mEglConfig(hostConfig), mAttribValues() {
    mAttribValues = new GLint[kConfigAttributesLen];
    for (size_t i = 0; i < kConfigAttributesLen; ++i) {
        mAttribValues[i] = 0;
        s_egl.eglGetConfigAttrib(hostDisplay,
                                 hostConfig,
                                 kConfigAttributes[i],
                                 &mAttribValues[i]);

        // This implementation supports guest window surfaces by wrapping
        // them around host Pbuffers, so always report it to the guest.
        if (kConfigAttributes[i] == EGL_SURFACE_TYPE) {
            mAttribValues[i] |= EGL_WINDOW_BIT;
        }
    }
}

std::ostream& operator<<(std::ostream& out, const FbConfig& config)
{
    out << config.getConfigId() << ":";
    for (size_t i = 0; i < kConfigAttributesLen; ++i) {
        out << config.mAttribValues[i] << ",";
    }
    out << std::endl;
    return out;
}

FbConfigList::FbConfigList(EGLDisplay display, bool isBasePhone) :
        mCount(0), mConfigs(nullptr), mDisplay(display), mIsBasePhone(isBasePhone) {
    if (display == EGL_NO_DISPLAY) {
        ERR("%s: Invalid display value %p (EGL_NO_DISPLAY)\n",
          __FUNCTION__, (void*)display);
        return;
    }

    EGLint numHostConfigs = 0;
    if (!s_egl.eglGetConfigs(display, NULL, 0, &numHostConfigs)) {
        ERR("%s: Could not get number of host EGL configs\n", __FUNCTION__);
        return;
    }
    EGLConfig* hostConfigs = new EGLConfig[numHostConfigs];
    s_egl.eglGetConfigs(display, hostConfigs, numHostConfigs, &numHostConfigs);
    mAllConfigCount = numHostConfigs;
    mConfigs = new FbConfig*[numHostConfigs];
    INFO("numHostConfigs  %d", numHostConfigs);
    for (EGLint i = 0;  i < numHostConfigs; ++i) {
        // Filter out configs that are not compatible with our implementation.
        if (!isCompatibleHostConfig(hostConfigs[i], display)) {
            continue;
        }
        INFO("config[%d]:%p", mCount, hostConfigs[i]);
        mConfigs[mCount] = new FbConfig(hostConfigs[i], display);
        mCount++;
    }

    delete [] hostConfigs;
}

FbConfigList::~FbConfigList() {
    for (int n = 0; n < mCount; ++n) {
        delete mConfigs[n];
    }
    delete [] mConfigs;
}

const FbConfig* FbConfigList::get(EGLConfig config, int& index) const
{
    EGLint hostConfigId = 0;
    s_egl.eglGetConfigAttrib(
                mDisplay, config, EGL_CONFIG_ID, &hostConfigId);
    const FbConfig* retConfig = nullptr;
    for (int k = 0; k < mCount; ++k) {
        int guestConfigId = mConfigs[k]->getConfigId();
        if (guestConfigId == hostConfigId) {
            // There is a match. Write it to |configs| if it is not NULL.
            retConfig = mConfigs[k];
            index = k;
            break;
        }
    }
    return retConfig;
}

bool FbConfigList::MatchAllConfig()
{
    serverMatchConfigId.resize(kServerConfigCount, -1);
    for (size_t i = 0; i < kServerConfigCount; i++) {
        if (!chooseConfig(serverConfig[i], &serverMatchConfigId[i])) {
            ERR("Failed to match server config:%zu", i);
            return false;
        }
    }
    // 客户端和服务端的config匹配成功，打印服务端和客户端的映射关系
    std::stringstream ss;
    ss << "config match";
    ss  << "hostIndex,clientIndex,clientConfigId" << std::endl;
    for (size_t i = 0; i < kServerConfigCount; i++) {
        const FbConfig* config = get(i);
        if (config == nullptr) {
            ERR("Failed to get server:%zu match client config:%d", i, serverMatchConfigId[i]);
            return false;
        }
        ss << std::setw(5) << i << std::setw(5) << serverMatchConfigId[i] << std::setw(5) <<
            config->getConfigId() << std::endl;
    }
    INFO("%s", ss.str().c_str());
    return true;
}

EGLint FbConfigList::getMatchConfigs(EGLint hostConfig) const
{
    hostConfig = hostConfig - 1;  //服务端的config id从1开始
    if (hostConfig < 0 || hostConfig >= kServerConfigCount) {
        ERR("Failed to match config, host config:%d error", hostConfig);
        return -1;
    }
    return serverMatchConfigId[hostConfig];
}

bool FbConfigList::MatchBestConfig(const EGLint* hostAttribs, EGLConfig* candidateConfig,
    EGLint candidateNum, EGLint* clientConfig, bool isPrint) const
{
    for (EGLint i = 0; i < candidateNum; i++) {
        if (!isCompatibleHostConfig(candidateConfig[i], mDisplay)) {
            if (isPrint) {
                EGLint configId = 0;
                s_egl.eglGetConfigAttrib(mDisplay, candidateConfig[i], EGL_CONFIG_ID, &configId);
                ERR("config:%d is not compatible", configId);
            }
            continue;
        }
        int index = 0;
        const FbConfig* config = get(candidateConfig[i], index);
        if (config == nullptr) {
            ERR("can't clientconfig:%d", config->getConfigId());
            continue;
        }
        if (isPrint) {
            std::stringstream ss;
            ss << *config;
            ERR("%s", ss.str().c_str());
        }
        bool isFind = true;
        for (size_t i = 0; i < kConfigAttributesLen; i++) {
            GLuint attriName = kConfigAttributes[i];
            if (mIsBasePhone && attriName == EGL_SAMPLES) {
                // 基础云手机当前，samples大于0时，rendertype就不支持pbuffer
                // 所以在基础云手机上,EGL_SAMPLES不作为强匹配
                continue;
            }
            if (attriName != EGL_ALPHA_SIZE && attriName != EGL_BLUE_SIZE &&
                attriName != EGL_GREEN_SIZE && attriName != EGL_RED_SIZE &&
                attriName != EGL_DEPTH_SIZE && attriName != EGL_STENCIL_SIZE &&
                attriName != EGL_SAMPLES) {
                continue;
            }
            // 除掉continue的属性都需要强匹配
            GLuint clientAttriValue = config->getAttribValue(i);
            GLuint hostAttriValue = hostAttribs[i];
            if (clientAttriValue != hostAttriValue) {
                if (isPrint) {
                    ERR("config:%d attri:%d host:%d, client:%d", config->getConfigId(), attriName,
                        hostAttriValue, clientAttriValue);
                }
                isFind = false;
                break;
            }
        }
        if (isFind) {
            *clientConfig = index;
            return true;
        }
    }
    if (!isPrint) {
        // 若出现某个config无法匹配客户端的config，则将详细的匹配细节输出
        const int configIdIndex = 4;
        ERR("host config:%d unable to match client config, candidateNum:%d", hostAttribs[configIdIndex], candidateNum);
        (void) MatchBestConfig(hostAttribs, candidateConfig, candidateNum, clientConfig, true);
    }
    return false;
}

bool FbConfigList::chooseConfig(const EGLint* hostAttribs, EGLint* clientConfig) const {
    if (hostAttribs == nullptr || clientConfig == nullptr || empty()) {
        return false;
    }
    size_t clientConfigNum = getAllConfigCount();
    EGLConfig* eglConfigs = new (std::nothrow) EGLConfig[clientConfigNum];
    if (eglConfigs == nullptr) {
        ERR("Failed to new %zu EGLConfig", clientConfigNum);
        return false;
    }
    std::unique_ptr<EGLConfig []> retClientConfigs(eglConfigs);
    memset(retClientConfigs.get(), 0, clientConfigNum * sizeof(EGLConfig));
    EGLint requireAttri[kConfigAttributesLen * 2 + 1] = {EGL_NONE};
    int curPos = 0;
    for (size_t i = 0; i < kConfigAttributesLen; i++) {
        GLuint attriName = kConfigAttributes[i];
        EGLint hostAttriValue = hostAttribs[i];
        if (attriName == EGL_MAX_PBUFFER_HEIGHT || attriName == EGL_MAX_PBUFFER_PIXELS || attriName == EGL_MAX_PBUFFER_WIDTH ||
            attriName == EGL_NATIVE_VISUAL_ID || attriName == EGL_NATIVE_VISUAL_TYPE  || attriName == EGL_RECORDABLE_ANDROID ||
            attriName == EGL_CONFIG_ID || attriName == EGL_NATIVE_RENDERABLE || attriName == EGL_MAX_SWAP_INTERVAL) {
            // 以上服务端config的这些属性将忽略，不参与eglChooseConfig的流程
            // 其中：
            // 1.EGL_MAX_PBUFFER_HEIGHT、EGL_MAX_PBUFFER_PIXELS、EGL_MAX_PBUFFER_WIDTH、EGL_NATIVE_VISUAL_ID
            // EGL_NATIVE_VISUAL_TYPE、EGL_RECORDABLE_ANDROID：eglChooseConfig不可传入这些参数，所以忽略
            // 2.EGL_CONFIG_ID：此参数在跨手机之间无意义，只是本机的config标识
            // 3.EGL_NATIVE_RENDERABLE：各个真机此参数所有的config都相同，匹配无意义
            // 4.EGL_MAX_SWAP_INTERVAL：此参数各个手机上大部分都是1，但发现华为畅享10e是5，若此参数传入，在畅享10e上将无法匹配，将此参数屏蔽
            continue;
        }
        if (mIsBasePhone && (attriName == EGL_SAMPLES || attriName == EGL_SAMPLE_BUFFERS)) {
            // 基础云手机大部分的config都不支持pbuffer（EGL_SURFACE_TYPE对应的属性），但分离渲染的客户端，对应的config必须支持pbuffer
            // 同时观察到基础云手机EGL_SAMPLES只能为0且EGL_SAMPLE_BUFFERS也为0的时候，EGL_SURFACE_TYPE才支持pbuffer
            // 所以在基础云手机上，让EGL_SAMPLES、EGL_SAMPLE_BUFFERS不参与匹配，防止刷选出来都是不支持pbuffer的
            continue;
        }
        if (attriName == EGL_BIND_TO_TEXTURE_RGB && hostAttriValue == 0) {
            // 若服务端config的EGL_BIND_TO_TEXTURE_RGB属性值为0，则不参与匹配，认为服务端无需该功能
            // 做此特殊处理的原因是：基础云手机及华为畅享10e上，若此参数参与eglChooseConfig，而且传0，会导致无法匹配出服务端的config
            // 因为他们的config里面此参数都是大于0
            continue;
        }
        requireAttri[curPos++] = attriName;
        requireAttri[curPos++] = hostAttriValue;
    }
    requireAttri[curPos] = EGL_NONE;
    EGLint retNumConfigs = 0;
    if (!s_egl.eglChooseConfig(mDisplay, requireAttri, retClientConfigs.get(), clientConfigNum, &retNumConfigs)) {
        ERR("Failed to call eglChooseConfig");
        return false;
    }
    if (retNumConfigs == 0) {
        ERR("eglChooseConfig ret num is zero");
        return false;
    }
    if (!MatchBestConfig(hostAttribs, retClientConfigs.get(), retNumConfigs, clientConfig)) {
        ERR("Failed to find best match config, retNum:%d", retNumConfigs);
        return false;
    }
    return true;
}

int FbConfigList::chooseConfig(const EGLint* attribs,
                               EGLint* configs,
                               EGLint configsSize,
                               bool isFramebufferDepth24) const {
    EGLint numHostConfigs = 0;
    if (!s_egl.eglGetConfigs(mDisplay, NULL, 0, &numHostConfigs)) {
        ERR("Could not get number of host EGL configs\n");
        return 0;
    }

    EGLConfig* matchedConfigs = new EGLConfig[numHostConfigs];

    // If EGL_SURFACE_TYPE appears in |attribs|, the value passed to
    // eglChooseConfig should be forced to EGL_PBUFFER_BIT because that's
    // what it used by the current implementation, exclusively. This forces
    // the rewrite of |attribs| into a new array.
    bool hasSurfaceType = false;
    bool mustReplaceSurfaceType = false;
    bool mustAddDepthSize = isFramebufferDepth24 ? true : false;
    int numAttribs = 0;
    while (attribs[numAttribs] != EGL_NONE) {
        if (attribs[numAttribs] == EGL_SURFACE_TYPE) {
            hasSurfaceType = true;
            if (attribs[numAttribs + 1] != EGL_PBUFFER_BIT) {
                mustReplaceSurfaceType = true;
            }
        } else if (attribs[numAttribs] == EGL_DEPTH_SIZE) {
            mustAddDepthSize = false;
        }
        numAttribs += 2;
    }

    EGLint* newAttribs = nullptr;

    if (mustReplaceSurfaceType) {
        // There is at least on EGL_SURFACE_TYPE in |attribs|. Copy the
        // array and replace all values with EGL_PBUFFER_BIT
        newAttribs = new GLint[numAttribs + 1];
        if (!newAttribs) {
            ERR("new newAttribs failed!");
            delete[] matchedConfigs;
            return 0;
        }
        memcpy(newAttribs, attribs, numAttribs * sizeof(GLint));
        newAttribs[numAttribs] = EGL_NONE;
        for (int n = 0; n < numAttribs; n += 2) {
            if (newAttribs[n] == EGL_SURFACE_TYPE) {
                newAttribs[n + 1] = EGL_PBUFFER_BIT;
            }
        }
    } else if (!hasSurfaceType) {
        // There is no EGL_SURFACE_TYPE in |attribs|, then add one entry
        // with the value EGL_PBUFFER_BIT.
        newAttribs = new GLint[numAttribs + 3];
        if (!newAttribs) {
            ERR("new newAttribs failed!");
            delete[] matchedConfigs;
            return 0;
        }
        memcpy(newAttribs, attribs, numAttribs * sizeof(GLint));
        newAttribs[numAttribs] = EGL_SURFACE_TYPE;
        newAttribs[numAttribs + 1] = EGL_PBUFFER_BIT;
        newAttribs[numAttribs + 2] = EGL_NONE;
    }

    EGLint* newAttribs1 = nullptr;

    if (mustAddDepthSize) {
        const EGLint* attribsTmp = newAttribs ? newAttribs : attribs;
        int size = sizeof(attribsTmp) / sizeof (EGLint);
        newAttribs1 = new EGLint[size + 2];
        if (!newAttribs1) {
            delete[] newAttribs;
            delete[] matchedConfigs;
            ERR("new newAttribs1 failed!");
            return 0;
        }
        memcpy(newAttribs1, attribsTmp, sizeof(attribs));
        newAttribs1[size] = EGL_DEPTH_SIZE;
        newAttribs1[size + 1] = 24;
        newAttribs1[size + 2] = EGL_NONE;
    }

    if (newAttribs1) {
        int i = 0;
        while (newAttribs1[i] != EGL_NONE) {
            INFO("LDQ require newAttribut 0x%x:%d", newAttribs1[i], newAttribs1[i + 1]);
            i += 2;
        }
    }

    if (!s_egl.eglChooseConfig(mDisplay,
                               newAttribs1 ? newAttribs1 : (newAttribs ? newAttribs : attribs),
                               matchedConfigs,
                               numHostConfigs,
                               &numHostConfigs)) {
        ERR("cant not find correct config in KGPU");
        numHostConfigs = 0;
    }

    delete [] newAttribs;
    delete [] newAttribs1;

    int result = 0;
    for (int n = 0; n < numHostConfigs; ++n) {
        // Don't count or write more than |configsSize| items if |configs|
        // is not NULL.
        if (configs && configsSize > 0 && result >= configsSize) {
            break;
        }
        // Skip incompatible host configs.
        if (!isCompatibleHostConfig(matchedConfigs[n], mDisplay)) {
            // continue;
        }
        // Find the FbConfig with the same EGL_CONFIG_ID
        EGLint hostConfigId;
        s_egl.eglGetConfigAttrib(
                mDisplay, matchedConfigs[n], EGL_CONFIG_ID, &hostConfigId);
        for (int k = 0; k < mCount; ++k) {
            int guestConfigId = mConfigs[k]->getConfigId();
            if (guestConfigId == hostConfigId) {
                // There is a match. Write it to |configs| if it is not NULL.
                if (configs && result < configsSize) {
                    configs[result] = (uint32_t)k;
                }
                result ++;
                break;
            }
        }
    }

    delete [] matchedConfigs;

    return result;
}


void FbConfigList::getPackInfo(EGLint* numConfigs,
                               EGLint* numAttributes) const {
    if (numConfigs) {
        *numConfigs = mCount;
    }
    if (numAttributes) {
        *numAttributes = static_cast<EGLint>(kConfigAttributesLen);
    }
}

EGLint FbConfigList::packConfigs(GLuint bufferByteSize, GLuint* buffer) const {
    GLuint numAttribs = static_cast<GLuint>(kConfigAttributesLen);
    GLuint kGLuintSize = static_cast<GLuint>(sizeof(GLuint));
    GLuint neededByteSize = (mCount + 1) * numAttribs * kGLuintSize;
    if (!buffer || bufferByteSize < neededByteSize) {
        return -neededByteSize;
    }
    // Write to the buffer the config attribute ids, followed for each one
    // of the configs, their values.
    memcpy(buffer, kConfigAttributes, kConfigAttributesLen * kGLuintSize);

    for (int i = 0; i < mCount; ++i) {
        memcpy(buffer + (i + 1) * kConfigAttributesLen,
            mConfigs[i]->mAttribValues,
            kConfigAttributesLen * kGLuintSize);
    }
    return mCount;
}
