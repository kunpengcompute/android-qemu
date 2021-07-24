cmake_minimum_required(VERSION 3.6)
set(FILE_PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/..)

set(GLESV2DEC_FILES
    ${FILE_PREFIX}/android/android-emugl/host/libs/GLESv2_dec/gles2_server_context.cpp
    ${FILE_PREFIX}/android/android-emugl/host/libs/GLESv2_dec/GLESv2Decoder.cpp
)

set(GLESDISPATCH_FILES
    ${FILE_PREFIX}/android/android-emugl/host/libs/libOpenGLESDispatch/EGLDispatch.cpp
    ${FILE_PREFIX}/android/android-emugl/host/libs/libOpenGLESDispatch/GLESv2Dispatch.cpp
    ${FILE_PREFIX}/android/android-emugl/host/libs/libOpenGLESDispatch/OpenGLDispatchLoader.cpp
    ${FILE_PREFIX}/android/android-emugl/host/libs/libOpenGLESDispatch/StaticDispatch.cpp
)

set(EMUGL_INTERFACE_FILES 
  ${FILE_PREFIX}/android/android-emugl/host/libs/EmuglInterface.cpp
)

set(OPENGLRENDER_FILES
    ${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/TextureDraw.cpp
    ${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/FrameBuffer.cpp
    ${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/FbMapConfig.cpp
    ${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/RenderWindow.cpp
    ${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/WindowSurface.cpp
    ${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/ColorBuffer.cpp
    ${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/RenderContext.cpp
    ${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/GLESVersionDetector.cpp
    ${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/RenderControl.cpp
    ${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/RenderThreadInfo.cpp
    ${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/PostWorker.cpp
    ${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/YUVConverter.cpp
    ${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/SyncThread.cpp
    ${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/ReadbackWorker.cpp
    ${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/FenceSync.cpp
)

set(GLSNAPSHOT_FILES
    ${FILE_PREFIX}/android/android-emugl/host/libs/libGLSnapshot/GLSnapshot.cpp)

set(OPENGLCODECOMMON_FILES
    ${FILE_PREFIX}/android/android-emugl/shared/OpenglCodecCommon/glUtils.cpp
    ${FILE_PREFIX}/android/android-emugl/shared/OpenglCodecCommon/ChecksumCalculator.cpp
    ${FILE_PREFIX}/android/android-emugl/shared/OpenglCodecCommon/ChecksumCalculatorThreadInfo.cpp
    ${FILE_PREFIX}/android/android-emugl/shared/emugl/common/shared_library.cpp
    ${FILE_PREFIX}/android/android-emugl/shared/emugl/common/dma_device.cpp
    ${FILE_PREFIX}/android/android-emugl/shared/emugl/common/vm_operations.cpp
    ${FILE_PREFIX}/android/android-emugl/shared/emugl/common/logging.cpp
    ${FILE_PREFIX}/android/android-emugl/shared/emugl/common/misc.cpp
    ${FILE_PREFIX}/android/android-emugl/shared/emugl/common/crash_reporter.cpp
    ${FILE_PREFIX}/android/android-emugl/shared/emugl/common/sync_device.cpp
    ${FILE_PREFIX}/android/android-emu/android/base/StringView.cpp
)

set(EMUANDROIDBASE_FILES
    ${FILE_PREFIX}/android/android-emu/android/base/memory/LazyInstance.cpp
    ${FILE_PREFIX}/android/android-emu/android/base/synchronization/MessageChannel.cpp
    ${FILE_PREFIX}/android/android-emu/android/base/files/Stream.cpp
    ${FILE_PREFIX}/android/android-emu/android/base/files/Fd.cpp
    ${FILE_PREFIX}/android/android-emu/android/base/threads/FunctorThread.cpp
    ${FILE_PREFIX}/android/android-emu/android/base/threads/Thread_pthread.cpp
    ${FILE_PREFIX}/android/android-emu/android/base/threads/ThreadStore.cpp
    ${FILE_PREFIX}/android/android-emu/android/base/system/System.cpp
    ${FILE_PREFIX}/android/android-emu/android/base/Log.cpp
    ${FILE_PREFIX}/android/android-emu/android/base/Tracing.cpp
    ${FILE_PREFIX}/android/android-emu/android/base/Debug.cpp
    ${FILE_PREFIX}/android/android-emu/android/base/files/PathUtils.cpp
    ${FILE_PREFIX}/android/android-emu/android/base/GLObjectCounter.cpp
    ${FILE_PREFIX}/android/android-emu/android/base/StringFormat.cpp
    ${FILE_PREFIX}/android/android-emu/android/base/misc/FileUtils.cpp
    ${FILE_PREFIX}/android/android-emu/android/base/misc/StringUtils.cpp
    ${FILE_PREFIX}/android/android-emu/android/utils/file_io.cpp
    ${FILE_PREFIX}/android/android-emu/android/utils/tempfile.c
    ${FILE_PREFIX}/android/android-emu/android/utils/path.cpp
    ${FILE_PREFIX}/android/android-emu/android/utils/bufprint.c
    ${FILE_PREFIX}/android/android-emu/android/utils/eintr_wrapper.c
    ${FILE_PREFIX}/android/android-emu/android/utils/panic.c
    ${FILE_PREFIX}/android/android-emu/android/utils/debug.c
    ${FILE_PREFIX}/android/android-emu/android/utils/bufprint_system.cpp
    ${FILE_PREFIX}/android/android-emu/android/utils/path_system.cpp
    ${FILE_PREFIX}/android/android-emu/android/utils/dirscanner.cpp
    ${FILE_PREFIX}/android/android-emu/android/utils/system.c
)

add_library(EmuGLRender SHARED ${GLESV2DEC_FILES} ${GLESDISPATCH_FILES}
    ${EMUGL_INTERFACE_FILES} ${OPENGLRENDER_FILES} ${OPENGLCODECOMMON_FILES} ${GLSNAPSHOT_FILES}
    ${EMUANDROIDBASE_FILES})

target_include_directories(EmuGLRender PUBLIC ${FILE_PREFIX}/android/android-emu)
target_include_directories(EmuGLRender PUBLIC ${FILE_PREFIX}/android/android-emugl)
target_include_directories(EmuGLRender PUBLIC ${FILE_PREFIX}/android/android-emugl/shared)
target_include_directories(EmuGLRender PUBLIC ${FILE_PREFIX}/android/android-emugl/host/include)
target_include_directories(EmuGLRender PUBLIC ${FILE_PREFIX}/android/android-emugl/host/include/OpenGLESDispatch)
target_include_directories(EmuGLRender PUBLIC ${FILE_PREFIX}/android/android-emugl/host/libs)
target_include_directories(EmuGLRender PUBLIC ${FILE_PREFIX}/android/android-emugl/shared/OpenglCodecCommon)
target_include_directories(EmuGLRender PUBLIC ${FILE_PREFIX}/android/android-emugl/host/include/OpenglRender)
target_include_directories(EmuGLRender PUBLIC ${FILE_PREFIX}/android/android-emugl/host/libs/GLESv2_dec)
target_include_directories(EmuGLRender PUBLIC ${FILE_PREFIX}/android/android-emugl/host/libs/renderControl_dec)
target_include_directories(EmuGLRender PUBLIC ${FILE_PREFIX}/android/android-emugl/host/libs/libGLSnapshot)
target_include_directories(EmuGLRender PUBLIC ${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender)
target_include_directories(EmuGLRender PUBLIC ${FILE_PREFIX}/android/android-emugl/shared/OpenglCodecCommon)

target_link_libraries(EmuGLRender log)