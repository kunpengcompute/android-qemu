/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2017-2021. All rights reserved.
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the Apache License version 2
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * Apache License for more details.
 */
#include "EmuglInterface.h"
#include "GLESv2_dec/GLESv2Decoder.h"
#include "OpenGLESDispatch/GLESv2Dispatch.h"
#include "libOpenglRender/RenderThreadInfo.h"
#include "libOpenglRender/RenderWindow.h"
#include "libOpenglRender/FrameBuffer.h"
#include <map>
#include <set>
#include <mutex>

#ifdef __cplusplus
extern "C" {
#endif

std::map<uint32_t, int> pidThreads;
std::mutex dataMutex;

void* CreateGLESv2Decoder(uint32_t pid, uint32_t tid)
{
    RenderThreadInfo *threadInfo = nullptr;
    threadInfo = new (std::nothrow) RenderThreadInfo(pid, tid);
    if (threadInfo == nullptr) {
        ERR("error: failed to create render thread info");
        return nullptr;
    }
    threadInfo->m_gl2Dec.initGL(gles2_dispatch_get_proc_func, nullptr);
    {
        std::lock_guard<std::mutex> lockGuard(dataMutex);
        pidThreads[pid]++;
    }
    INFO("pid:%u tid:%u is construction", threadInfo->m_pid, threadInfo->m_tid);
    return &threadInfo->m_gl2Dec;
}

void DestoryGLESv2Decoder(void* self)
{
    // 将当前线程持有的context与此线程解绑，保证客户端退出时删除此context时可正常释放资源
    FrameBuffer::getFB()->bindContext(0, 0, 0);
    RenderThreadInfo* curThread = RenderThreadInfo::get();
    if (curThread != nullptr) {
        INFO("pid:%u tid:%u is deconstruction", curThread->m_pid, curThread->m_tid);
        delete curThread;
    }
}

void rcExitRenderThread()
{
    FrameBuffer::getFB()->bindContext(0, 0, 0);
    FrameBuffer::getFB()->drainWindowSurface();
    FrameBuffer::getFB()->drainRenderContext();
    RenderThreadInfo* curThread = RenderThreadInfo::get();
    int needCleanPid = 0;
    {
        std::lock_guard<std::mutex> lockGuard(dataMutex);
        pidThreads[curThread->m_pid]--;
        if (pidThreads[curThread->m_pid] == 0) {
            needCleanPid = curThread->m_pid;
        } else if (pidThreads[curThread->m_pid] < 0) {
            ERR("cur process is be delete to negative, pid:%u, tid:%u", curThread->m_pid, curThread->m_tid);
        } else {
            INFO("pid:%u tid:%u is exist", curThread->m_pid, curThread->m_tid);
        }
    }
    if (needCleanPid != 0) {
        INFO("begin clean pid:%u resource", needCleanPid);
        {
            std::lock_guard<std::mutex> lockGuard(dataMutex);
            pidThreads.erase(needCleanPid);
        }
        FrameBuffer::getFB()->cleanupProcGLObjects(needCleanPid);
    }
}

void SetDeleteColorbufferCallBack(DeleteColorbufferFunc deleteColorbufferFunc)
{
    FrameBuffer::setDeleteColorbufferCallBack(deleteColorbufferFunc);
}

#define GET_ADDRESS(func) \
void* GetAddress_##func(void* self) \
{ \
    GLESv2Decoder* ctx = (GLESv2Decoder*) self; \
    if (ctx == nullptr) { \
        return nullptr; \
    } \
    return (void *)(ctx->func); \
}

GET_ADDRESS(glActiveTexture);
GET_ADDRESS(glAttachShader);
GET_ADDRESS(glBindAttribLocation);
GET_ADDRESS(glBindBuffer);
GET_ADDRESS(glBindFramebuffer);
GET_ADDRESS(glBindRenderbuffer);
GET_ADDRESS(glBindTexture);
GET_ADDRESS(glBlendColor);
GET_ADDRESS(glBlendEquation);
GET_ADDRESS(glBlendEquationSeparate);
GET_ADDRESS(glBlendFunc);
GET_ADDRESS(glBlendFuncSeparate);
GET_ADDRESS(glBufferData);
GET_ADDRESS(glBufferSubData);
GET_ADDRESS(glCheckFramebufferStatus);
GET_ADDRESS(glClear);
GET_ADDRESS(glClearColor);
GET_ADDRESS(glClearDepthf);
GET_ADDRESS(glClearStencil);
GET_ADDRESS(glColorMask);
GET_ADDRESS(glCompileShader);
GET_ADDRESS(glCompressedTexImage2D);
GET_ADDRESS(glCompressedTexSubImage2D);
GET_ADDRESS(glCopyTexImage2D);
GET_ADDRESS(glCopyTexSubImage2D);
GET_ADDRESS(glCreateProgram);
GET_ADDRESS(glCreateShader);
GET_ADDRESS(glCullFace);
GET_ADDRESS(glDeleteBuffers);
GET_ADDRESS(glDeleteFramebuffers);
GET_ADDRESS(glDeleteProgram);
GET_ADDRESS(glDeleteRenderbuffers);
GET_ADDRESS(glDeleteShader);
GET_ADDRESS(glDeleteTextures);
GET_ADDRESS(glDepthFunc);
GET_ADDRESS(glDepthMask);
GET_ADDRESS(glDepthRangef);
GET_ADDRESS(glDetachShader);
GET_ADDRESS(glDisable);
GET_ADDRESS(glDisableVertexAttribArray);
GET_ADDRESS(glDrawArrays);
GET_ADDRESS(glDrawElements);
GET_ADDRESS(glEnable);
GET_ADDRESS(glEnableVertexAttribArray);
GET_ADDRESS(glFinish);
GET_ADDRESS(glFlush);
GET_ADDRESS(glFramebufferRenderbuffer);
GET_ADDRESS(glFramebufferTexture2D);
GET_ADDRESS(glFrontFace);
GET_ADDRESS(glGenBuffers);
GET_ADDRESS(glGenerateMipmap);
GET_ADDRESS(glGenFramebuffers);
GET_ADDRESS(glGenRenderbuffers);
GET_ADDRESS(glGenTextures);
GET_ADDRESS(glGetActiveAttrib);
GET_ADDRESS(glGetActiveUniform);
GET_ADDRESS(glGetAttachedShaders);
GET_ADDRESS(glGetAttribLocation);
GET_ADDRESS(glGetBooleanv);
GET_ADDRESS(glGetBufferParameteriv);
GET_ADDRESS(glGetError);
GET_ADDRESS(glGetFloatv);
GET_ADDRESS(glGetFramebufferAttachmentParameteriv);
GET_ADDRESS(glGetIntegerv);
GET_ADDRESS(glGetProgramiv);
GET_ADDRESS(glGetProgramInfoLog);
GET_ADDRESS(glGetRenderbufferParameteriv);
GET_ADDRESS(glGetShaderiv);
GET_ADDRESS(glGetShaderInfoLog);
GET_ADDRESS(glGetShaderPrecisionFormat);
GET_ADDRESS(glGetShaderSource);
GET_ADDRESS(glGetString);
GET_ADDRESS(glGetTexParameterfv);
GET_ADDRESS(glGetTexParameteriv);
GET_ADDRESS(glGetUniformfv);
GET_ADDRESS(glGetUniformiv);
GET_ADDRESS(glGetUniformLocation);
GET_ADDRESS(glGetVertexAttribfv);
GET_ADDRESS(glGetVertexAttribiv);
GET_ADDRESS(glGetVertexAttribPointerv);
GET_ADDRESS(glHint);
GET_ADDRESS(glIsBuffer);
GET_ADDRESS(glIsEnabled);
GET_ADDRESS(glIsFramebuffer);
GET_ADDRESS(glIsProgram);
GET_ADDRESS(glIsRenderbuffer);
GET_ADDRESS(glIsShader);
GET_ADDRESS(glIsTexture);
GET_ADDRESS(glLineWidth);
GET_ADDRESS(glLinkProgram);
GET_ADDRESS(glPixelStorei);
GET_ADDRESS(glPolygonOffset);
GET_ADDRESS(glReadPixels);
GET_ADDRESS(glReleaseShaderCompiler);
GET_ADDRESS(glRenderbufferStorage);
GET_ADDRESS(glSampleCoverage);
GET_ADDRESS(glScissor);
GET_ADDRESS(glShaderBinary);
GET_ADDRESS(glShaderSource);
GET_ADDRESS(glStencilFunc);
GET_ADDRESS(glStencilFuncSeparate);
GET_ADDRESS(glStencilMask);
GET_ADDRESS(glStencilMaskSeparate);
GET_ADDRESS(glStencilOp);
GET_ADDRESS(glStencilOpSeparate);
GET_ADDRESS(glTexImage2D);
GET_ADDRESS(glTexParameterf);
GET_ADDRESS(glTexParameterfv);
GET_ADDRESS(glTexParameteri);
GET_ADDRESS(glTexParameteriv);
GET_ADDRESS(glTexSubImage2D);
GET_ADDRESS(glUniform1f);
GET_ADDRESS(glUniform1fv);
GET_ADDRESS(glUniform1i);
GET_ADDRESS(glUniform1iv);
GET_ADDRESS(glUniform2f);
GET_ADDRESS(glUniform2fv);
GET_ADDRESS(glUniform2i);
GET_ADDRESS(glUniform2iv);
GET_ADDRESS(glUniform3f);
GET_ADDRESS(glUniform3fv);
GET_ADDRESS(glUniform3i);
GET_ADDRESS(glUniform3iv);
GET_ADDRESS(glUniform4f);
GET_ADDRESS(glUniform4fv);
GET_ADDRESS(glUniform4i);
GET_ADDRESS(glUniform4iv);
GET_ADDRESS(glUniformMatrix2fv);
GET_ADDRESS(glUniformMatrix3fv);
GET_ADDRESS(glUniformMatrix4fv);
GET_ADDRESS(glUseProgram);
GET_ADDRESS(glValidateProgram);
GET_ADDRESS(glVertexAttrib1f);
GET_ADDRESS(glVertexAttrib1fv);
GET_ADDRESS(glVertexAttrib2f);
GET_ADDRESS(glVertexAttrib2fv);
GET_ADDRESS(glVertexAttrib3f);
GET_ADDRESS(glVertexAttrib3fv);
GET_ADDRESS(glVertexAttrib4f);
GET_ADDRESS(glVertexAttrib4fv);
GET_ADDRESS(glVertexAttribPointer);
GET_ADDRESS(glViewport);
GET_ADDRESS(glEGLImageTargetTexture2DOES);
GET_ADDRESS(glEGLImageTargetRenderbufferStorageOES);
GET_ADDRESS(glGetProgramBinaryOES);
GET_ADDRESS(glProgramBinaryOES);
GET_ADDRESS(glMapBufferOES);
GET_ADDRESS(glUnmapBufferOES);
GET_ADDRESS(glTexImage3DOES);
GET_ADDRESS(glTexSubImage3DOES);
GET_ADDRESS(glCopyTexSubImage3DOES);
GET_ADDRESS(glCompressedTexImage3DOES);
GET_ADDRESS(glCompressedTexSubImage3DOES);
GET_ADDRESS(glFramebufferTexture3DOES);
GET_ADDRESS(glBindVertexArrayOES);
GET_ADDRESS(glDeleteVertexArraysOES);
GET_ADDRESS(glGenVertexArraysOES);
GET_ADDRESS(glIsVertexArrayOES);
GET_ADDRESS(glDiscardFramebufferEXT);
GET_ADDRESS(glMultiDrawArraysEXT);
GET_ADDRESS(glMultiDrawElementsEXT);
GET_ADDRESS(glGetPerfMonitorGroupsAMD);
GET_ADDRESS(glGetPerfMonitorCountersAMD);
GET_ADDRESS(glGetPerfMonitorGroupStringAMD);
GET_ADDRESS(glGetPerfMonitorCounterStringAMD);
GET_ADDRESS(glGetPerfMonitorCounterInfoAMD);
GET_ADDRESS(glGenPerfMonitorsAMD);
GET_ADDRESS(glDeletePerfMonitorsAMD);
GET_ADDRESS(glSelectPerfMonitorCountersAMD);
GET_ADDRESS(glBeginPerfMonitorAMD);
GET_ADDRESS(glEndPerfMonitorAMD);
GET_ADDRESS(glGetPerfMonitorCounterDataAMD);
GET_ADDRESS(glRenderbufferStorageMultisampleIMG);
GET_ADDRESS(glFramebufferTexture2DMultisampleIMG);
GET_ADDRESS(glDeleteFencesNV);
GET_ADDRESS(glGenFencesNV);
GET_ADDRESS(glIsFenceNV);
GET_ADDRESS(glTestFenceNV);
GET_ADDRESS(glGetFenceivNV);
GET_ADDRESS(glFinishFenceNV);
GET_ADDRESS(glSetFenceNV);
GET_ADDRESS(glCoverageMaskNV);
GET_ADDRESS(glCoverageOperationNV);
GET_ADDRESS(glGetDriverControlsQCOM);
GET_ADDRESS(glGetDriverControlStringQCOM);
GET_ADDRESS(glEnableDriverControlQCOM);
GET_ADDRESS(glDisableDriverControlQCOM);
GET_ADDRESS(glExtGetTexturesQCOM);
GET_ADDRESS(glExtGetBuffersQCOM);
GET_ADDRESS(glExtGetRenderbuffersQCOM);
GET_ADDRESS(glExtGetFramebuffersQCOM);
GET_ADDRESS(glExtGetTexLevelParameterivQCOM);
GET_ADDRESS(glExtTexObjectStateOverrideiQCOM);
GET_ADDRESS(glExtGetTexSubImageQCOM);
GET_ADDRESS(glExtGetBufferPointervQCOM);
GET_ADDRESS(glExtGetShadersQCOM);
GET_ADDRESS(glExtGetProgramsQCOM);
GET_ADDRESS(glExtIsProgramBinaryQCOM);
GET_ADDRESS(glExtGetProgramBinarySourceQCOM);
GET_ADDRESS(glStartTilingQCOM);
GET_ADDRESS(glEndTilingQCOM);
GET_ADDRESS(glVertexAttribPointerData);
GET_ADDRESS(glVertexAttribPointerOffset);
GET_ADDRESS(glDrawElementsOffset);
GET_ADDRESS(glDrawElementsData);
GET_ADDRESS(glGetCompressedTextureFormats);
GET_ADDRESS(glShaderString);
GET_ADDRESS(glFinishRoundTrip);
GET_ADDRESS(glGenVertexArrays);
GET_ADDRESS(glBindVertexArray);
GET_ADDRESS(glDeleteVertexArrays);
GET_ADDRESS(glIsVertexArray);
GET_ADDRESS(glMapBufferRange);
GET_ADDRESS(glUnmapBuffer);
GET_ADDRESS(glFlushMappedBufferRange);
GET_ADDRESS(glMapBufferRangeAEMU);
GET_ADDRESS(glUnmapBufferAEMU);
GET_ADDRESS(glFlushMappedBufferRangeAEMU);
GET_ADDRESS(glReadPixelsOffsetAEMU);
GET_ADDRESS(glCompressedTexImage2DOffsetAEMU);
GET_ADDRESS(glCompressedTexSubImage2DOffsetAEMU);
GET_ADDRESS(glTexImage2DOffsetAEMU);
GET_ADDRESS(glTexSubImage2DOffsetAEMU);
GET_ADDRESS(glBindBufferRange);
GET_ADDRESS(glBindBufferBase);
GET_ADDRESS(glCopyBufferSubData);
GET_ADDRESS(glClearBufferiv);
GET_ADDRESS(glClearBufferuiv);
GET_ADDRESS(glClearBufferfv);
GET_ADDRESS(glClearBufferfi);
GET_ADDRESS(glGetBufferParameteri64v);
GET_ADDRESS(glGetBufferPointerv);
GET_ADDRESS(glUniformBlockBinding);
GET_ADDRESS(glGetUniformBlockIndex);
GET_ADDRESS(glGetUniformIndices);
GET_ADDRESS(glGetUniformIndicesAEMU);
GET_ADDRESS(glGetActiveUniformBlockiv);
GET_ADDRESS(glGetActiveUniformBlockName);
GET_ADDRESS(glUniform1ui);
GET_ADDRESS(glUniform2ui);
GET_ADDRESS(glUniform3ui);
GET_ADDRESS(glUniform4ui);
GET_ADDRESS(glUniform1uiv);
GET_ADDRESS(glUniform2uiv);
GET_ADDRESS(glUniform3uiv);
GET_ADDRESS(glUniform4uiv);
GET_ADDRESS(glUniformMatrix2x3fv);
GET_ADDRESS(glUniformMatrix3x2fv);
GET_ADDRESS(glUniformMatrix2x4fv);
GET_ADDRESS(glUniformMatrix4x2fv);
GET_ADDRESS(glUniformMatrix3x4fv);
GET_ADDRESS(glUniformMatrix4x3fv);
GET_ADDRESS(glGetUniformuiv);
GET_ADDRESS(glGetActiveUniformsiv);
GET_ADDRESS(glVertexAttribI4i);
GET_ADDRESS(glVertexAttribI4ui);
GET_ADDRESS(glVertexAttribI4iv);
GET_ADDRESS(glVertexAttribI4uiv);
GET_ADDRESS(glVertexAttribIPointer);
GET_ADDRESS(glVertexAttribIPointerOffsetAEMU);
GET_ADDRESS(glVertexAttribIPointerDataAEMU);
GET_ADDRESS(glGetVertexAttribIiv);
GET_ADDRESS(glGetVertexAttribIuiv);
GET_ADDRESS(glVertexAttribDivisor);
GET_ADDRESS(glDrawArraysInstanced);
GET_ADDRESS(glDrawElementsInstanced);
GET_ADDRESS(glDrawElementsInstancedDataAEMU);
GET_ADDRESS(glDrawElementsInstancedOffsetAEMU);
GET_ADDRESS(glDrawRangeElements);
GET_ADDRESS(glDrawRangeElementsDataAEMU);
GET_ADDRESS(glDrawRangeElementsOffsetAEMU);
GET_ADDRESS(glFenceSync);
GET_ADDRESS(glClientWaitSync);
GET_ADDRESS(glWaitSync);
GET_ADDRESS(glDeleteSync);
GET_ADDRESS(glIsSync);
GET_ADDRESS(glGetSynciv);
GET_ADDRESS(glFenceSyncAEMU);
GET_ADDRESS(glClientWaitSyncAEMU);
GET_ADDRESS(glWaitSyncAEMU);
GET_ADDRESS(glDeleteSyncAEMU);
GET_ADDRESS(glIsSyncAEMU);
GET_ADDRESS(glGetSyncivAEMU);
GET_ADDRESS(glDrawBuffers);
GET_ADDRESS(glReadBuffer);
GET_ADDRESS(glBlitFramebuffer);
GET_ADDRESS(glInvalidateFramebuffer);
GET_ADDRESS(glInvalidateSubFramebuffer);
GET_ADDRESS(glFramebufferTextureLayer);
GET_ADDRESS(glRenderbufferStorageMultisample);
GET_ADDRESS(glTexStorage2D);
GET_ADDRESS(glGetInternalformativ);
GET_ADDRESS(glBeginTransformFeedback);
GET_ADDRESS(glEndTransformFeedback);
GET_ADDRESS(glGenTransformFeedbacks);
GET_ADDRESS(glDeleteTransformFeedbacks);
GET_ADDRESS(glBindTransformFeedback);
GET_ADDRESS(glPauseTransformFeedback);
GET_ADDRESS(glResumeTransformFeedback);
GET_ADDRESS(glIsTransformFeedback);
GET_ADDRESS(glTransformFeedbackVaryings);
GET_ADDRESS(glTransformFeedbackVaryingsAEMU);
GET_ADDRESS(glGetTransformFeedbackVarying);
GET_ADDRESS(glGenSamplers);
GET_ADDRESS(glDeleteSamplers);
GET_ADDRESS(glBindSampler);
GET_ADDRESS(glSamplerParameterf);
GET_ADDRESS(glSamplerParameteri);
GET_ADDRESS(glSamplerParameterfv);
GET_ADDRESS(glSamplerParameteriv);
GET_ADDRESS(glGetSamplerParameterfv);
GET_ADDRESS(glGetSamplerParameteriv);
GET_ADDRESS(glIsSampler);
GET_ADDRESS(glGenQueries);
GET_ADDRESS(glDeleteQueries);
GET_ADDRESS(glBeginQuery);
GET_ADDRESS(glEndQuery);
GET_ADDRESS(glGetQueryiv);
GET_ADDRESS(glGetQueryObjectuiv);
GET_ADDRESS(glIsQuery);
GET_ADDRESS(glProgramParameteri);
GET_ADDRESS(glProgramBinary);
GET_ADDRESS(glGetProgramBinary);
GET_ADDRESS(glGetFragDataLocation);
GET_ADDRESS(glGetInteger64v);
GET_ADDRESS(glGetIntegeri_v);
GET_ADDRESS(glGetInteger64i_v);
GET_ADDRESS(glTexImage3D);
GET_ADDRESS(glTexImage3DOffsetAEMU);
GET_ADDRESS(glTexStorage3D);
GET_ADDRESS(glTexSubImage3D);
GET_ADDRESS(glTexSubImage3DOffsetAEMU);
GET_ADDRESS(glCompressedTexImage3D);
GET_ADDRESS(glCompressedTexImage3DOffsetAEMU);
GET_ADDRESS(glCompressedTexSubImage3D);
GET_ADDRESS(glCompressedTexSubImage3DOffsetAEMU);
GET_ADDRESS(glCopyTexSubImage3D);
GET_ADDRESS(glGetStringi);
GET_ADDRESS(glGetBooleani_v);
GET_ADDRESS(glMemoryBarrier);
GET_ADDRESS(glMemoryBarrierByRegion);
GET_ADDRESS(glGenProgramPipelines);
GET_ADDRESS(glDeleteProgramPipelines);
GET_ADDRESS(glBindProgramPipeline);
GET_ADDRESS(glGetProgramPipelineiv);
GET_ADDRESS(glGetProgramPipelineInfoLog);
GET_ADDRESS(glValidateProgramPipeline);
GET_ADDRESS(glIsProgramPipeline);
GET_ADDRESS(glUseProgramStages);
GET_ADDRESS(glActiveShaderProgram);
GET_ADDRESS(glCreateShaderProgramv);
GET_ADDRESS(glCreateShaderProgramvAEMU);
GET_ADDRESS(glProgramUniform1f);
GET_ADDRESS(glProgramUniform2f);
GET_ADDRESS(glProgramUniform3f);
GET_ADDRESS(glProgramUniform4f);
GET_ADDRESS(glProgramUniform1i);
GET_ADDRESS(glProgramUniform2i);
GET_ADDRESS(glProgramUniform3i);
GET_ADDRESS(glProgramUniform4i);
GET_ADDRESS(glProgramUniform1ui);
GET_ADDRESS(glProgramUniform2ui);
GET_ADDRESS(glProgramUniform3ui);
GET_ADDRESS(glProgramUniform4ui);
GET_ADDRESS(glProgramUniform1fv);
GET_ADDRESS(glProgramUniform2fv);
GET_ADDRESS(glProgramUniform3fv);
GET_ADDRESS(glProgramUniform4fv);
GET_ADDRESS(glProgramUniform1iv);
GET_ADDRESS(glProgramUniform2iv);
GET_ADDRESS(glProgramUniform3iv);
GET_ADDRESS(glProgramUniform4iv);
GET_ADDRESS(glProgramUniform1uiv);
GET_ADDRESS(glProgramUniform2uiv);
GET_ADDRESS(glProgramUniform3uiv);
GET_ADDRESS(glProgramUniform4uiv);
GET_ADDRESS(glProgramUniformMatrix2fv);
GET_ADDRESS(glProgramUniformMatrix3fv);
GET_ADDRESS(glProgramUniformMatrix4fv);
GET_ADDRESS(glProgramUniformMatrix2x3fv);
GET_ADDRESS(glProgramUniformMatrix3x2fv);
GET_ADDRESS(glProgramUniformMatrix2x4fv);
GET_ADDRESS(glProgramUniformMatrix4x2fv);
GET_ADDRESS(glProgramUniformMatrix3x4fv);
GET_ADDRESS(glProgramUniformMatrix4x3fv);
GET_ADDRESS(glGetProgramInterfaceiv);
GET_ADDRESS(glGetProgramResourceiv);
GET_ADDRESS(glGetProgramResourceIndex);
GET_ADDRESS(glGetProgramResourceLocation);
GET_ADDRESS(glGetProgramResourceName);
GET_ADDRESS(glBindImageTexture);
GET_ADDRESS(glDispatchCompute);
GET_ADDRESS(glDispatchComputeIndirect);
GET_ADDRESS(glBindVertexBuffer);
GET_ADDRESS(glVertexAttribBinding);
GET_ADDRESS(glVertexAttribFormat);
GET_ADDRESS(glVertexAttribIFormat);
GET_ADDRESS(glVertexBindingDivisor);
GET_ADDRESS(glDrawArraysIndirect);
GET_ADDRESS(glDrawArraysIndirectDataAEMU);
GET_ADDRESS(glDrawArraysIndirectOffsetAEMU);
GET_ADDRESS(glDrawElementsIndirect);
GET_ADDRESS(glDrawElementsIndirectDataAEMU);
GET_ADDRESS(glDrawElementsIndirectOffsetAEMU);
GET_ADDRESS(glTexStorage2DMultisample);
GET_ADDRESS(glSampleMaski);
GET_ADDRESS(glGetMultisamplefv);
GET_ADDRESS(glFramebufferParameteri);
GET_ADDRESS(glGetFramebufferParameteriv);
GET_ADDRESS(glGetTexLevelParameterfv);
GET_ADDRESS(glGetTexLevelParameteriv);
GET_ADDRESS(glMapBufferRangeDMA);
GET_ADDRESS(glUnmapBufferDMA);
GET_ADDRESS(glMapBufferRangeDirect);
GET_ADDRESS(glUnmapBufferDirect);
GET_ADDRESS(glFlushMappedBufferRangeDirect);
GET_ADDRESS(glGetGraphicsResetStatusEXT);
GET_ADDRESS(glReadnPixelsEXT);
GET_ADDRESS(glGetnUniformfvEXT);
GET_ADDRESS(glGetnUniformivEXT);
GET_ADDRESS(glDrawArraysNullAEMU);
GET_ADDRESS(glDrawElementsNullAEMU);
GET_ADDRESS(glDrawElementsOffsetNullAEMU);
GET_ADDRESS(glDrawElementsDataNullAEMU);
GET_ADDRESS(glUnmapBufferAsyncAEMU);
GET_ADDRESS(glFlushMappedBufferRangeAEMU2);
GET_ADDRESS(glEnableiEXT);
GET_ADDRESS(glDisableiEXT);
GET_ADDRESS(glBlendEquationiEXT);
GET_ADDRESS(glBlendEquationSeparateiEXT);
GET_ADDRESS(glBlendFunciEXT);
GET_ADDRESS(glBlendFuncSeparateiEXT);
GET_ADDRESS(glColorMaskiEXT);
GET_ADDRESS(glIsEnablediEXT);
GET_ADDRESS(glCopyImageSubDataEXT);
GET_ADDRESS(glBlendBarrierKHR);
GET_ADDRESS(glTexParameterIivEXT);
GET_ADDRESS(glTexParameterIuivEXT);
GET_ADDRESS(glGetTexParameterIivEXT);
GET_ADDRESS(glGetTexParameterIuivEXT);
GET_ADDRESS(glSamplerParameterIivEXT);
GET_ADDRESS(glSamplerParameterIuivEXT);
GET_ADDRESS(glGetSamplerParameterIivEXT);
GET_ADDRESS(glGetSamplerParameterIuivEXT);

std::unique_ptr<RenderWindow> g_renderWindow = nullptr;

enum WindowControlRetCode : uint32_t {
    WINDOW_CONTROL_SUCCESS = 0,
    WINDOW_CONTROL_INIT_FAILED = 0x0A050001,
    WINDOW_CONTROL_ALREADY_INITED = 0x0A050002,
};

int Initialize(unsigned int width, unsigned int height, bool useThread,
                           bool useSubWindow)
{
    if (g_renderWindow != nullptr) {
        ERR("render window already initialize");
        return WINDOW_CONTROL_ALREADY_INITED;
    }
    INFO("width:%u, height:%u", width, height);
    g_renderWindow = std::unique_ptr<RenderWindow>(new (std::nothrow) RenderWindow(width, height,
        useThread, useSubWindow));
    if (g_renderWindow == nullptr) {
        ERR("error: initialize render window failed");
        return WINDOW_CONTROL_INIT_FAILED;
    }
    return WINDOW_CONTROL_SUCCESS;
}

bool SetupSubWindow(EGLNativeWindowType nativeWindow, int x, int y, int width, int height, float zRot)
{
    if (g_renderWindow != nullptr) {
        if (!g_renderWindow->setupSubWindow(nativeWindow, x, y, width, height, zRot)) {
            ERR("error: setupSubWindow failed");
            return false;
        }
    } else {
        ERR("error: renderwindow hasn't been created yet");
        return false;
    }
    return true;
}

void RemoveSubWindow()
{
    if (g_renderWindow != nullptr) {
        g_renderWindow->removeSubWindow();
    } else {
        ERR("error: renderwindow hasn't been created yet");
    }
}

void Finalize()
{
    g_renderWindow = nullptr;
}

void SetRotation(int rotation)
{
    FrameBuffer *fb = FrameBuffer::getFB();
    fb->setRotation(rotation);
}

#ifdef __cplusplus
}
#endif
