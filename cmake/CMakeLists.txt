cmake_minimum_required(VERSION 3.6)
set(FILE_PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(NORMAL_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -fPIC -fstack-protector-all -fexceptions")
set(CMAKE_CXX_FLAGS "${NORMAL_CXX_FLAGS}")
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

set(OPENGLRENDER_VULKAN_FILES
	${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/vulkan/VkAndroidNativeBuffer.cpp
	${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/vulkan/VkCommonOperations.cpp
	${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/vulkan/VkDecoder.cpp
	${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/vulkan/VkDecoderGlobalState.cpp
	${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/vulkan/VkDecoderSnapshot.cpp
	${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/vulkan/VkReconstruction.cpp
	${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/vulkan/VulkanDispatch.cpp
	${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/vulkan/VulkanHandleMapping.cpp
	${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/vulkan/VulkanStream.cpp
	${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/vulkan/cereal/common/goldfish_vk_extension_structs.cpp
	${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/vulkan/cereal/common/goldfish_vk_unbox.cpp
	${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/vulkan/cereal/common/goldfish_vk_marshaling.cpp
	${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/vulkan/cereal/common/goldfish_vk_testing.cpp
	${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/vulkan/cereal/common/goldfish_vk_deepcopy.cpp
	${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/vulkan/cereal/common/goldfish_vk_handlemap.cpp
	${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/vulkan/cereal/common/goldfish_vk_dispatch.cpp
	${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/vulkan/cereal/common/goldfish_vk_transform.cpp
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
    ${FILE_PREFIX}/android/android-emugl/shared/emugl/common/feature_control.cpp
    ${FILE_PREFIX}/android/android-emugl/combined/GfxStreamBackend.cpp
    ${FILE_PREFIX}/android/android-emugl/combined/multi-display-agent-mock-impl.cpp
)

set(EMUANDROIDBASE_FILES
    ${FILE_PREFIX}/android/android-emu/android/base/memory/LazyInstance.cpp
    ${FILE_PREFIX}/android/android-emu/android/base/synchronization/MessageChannel.cpp
    ${FILE_PREFIX}/android/android-emu/android/base/files/Stream.cpp
    ${FILE_PREFIX}/android/android-emu/android/base/files/Fd.cpp
	${FILE_PREFIX}/android/android-emu/android/base/files/StreamSerializing.cpp
	${FILE_PREFIX}/android/android-emu/android/base/files/MemStream.cpp
    ${FILE_PREFIX}/android/android-emu/android/base/threads/FunctorThread.cpp
    ${FILE_PREFIX}/android/android-emu/android/base/threads/Thread_pthread.cpp
    ${FILE_PREFIX}/android/android-emu/android/base/threads/ThreadStore.cpp
    ${FILE_PREFIX}/android/android-emu/android/base/system/System.cpp
    ${FILE_PREFIX}/android/android-emu/android/base/Log.cpp
	${FILE_PREFIX}/android/android-emu/android/base/Pool.cpp
    ${FILE_PREFIX}/android/android-emu/android/base/Tracing.cpp
    ${FILE_PREFIX}/android/android-emu/android/base/ring_buffer.c
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
    ${FILE_PREFIX}/android/android-emu/android/base/StringView.cpp
    ${FILE_PREFIX}/android/android-emu/android/emulation/HostmemIdMapping.cpp
)
add_compile_options(-DVMICLIENT=1)

if(REMOTE_RENDER)
message(STATUS "-----------------LINUX BUILD-----------------")
add_definitions(-DREMOTE_RENDER=1)
add_library(EmuGLRender SHARED ${GLESV2DEC_FILES} ${GLESDISPATCH_FILES}
    ${EMUGL_INTERFACE_FILES} ${OPENGLRENDER_VULKAN_FILES} ${OPENGLRENDER_FILES} ${OPENGLCODECOMMON_FILES} ${GLSNAPSHOT_FILES}
    ${EMUANDROIDBASE_FILES})
else()
add_library(EmuGLRender SHARED ${GLESV2DEC_FILES} ${GLESDISPATCH_FILES}
    ${EMUGL_INTERFACE_FILES} ${OPENGLRENDER_VULKAN_FILES} ${OPENGLRENDER_FILES} ${OPENGLCODECOMMON_FILES} ${GLSNAPSHOT_FILES}
    ${EMUANDROIDBASE_FILES})
endif()

target_include_directories(EmuGLRender PUBLIC ${FILE_PREFIX}/include)
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
target_include_directories(EmuGLRender PUBLIC ${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/vulkan)
target_include_directories(EmuGLRender PUBLIC ${FILE_PREFIX}/android/android-emugl/host/libs/libOpenglRender/vulkan/cereal)
target_include_directories(EmuGLRender PUBLIC ${FILE_PREFIX}/android/android-emugl/host/libs/Translator/include)
target_include_directories(EmuGLRender PUBLIC ${FILE_PREFIX}/android/android-emugl/shared/OpenglCodecCommon)

if(ANDROID_ABI STREQUAL "arm64-v8a")
target_link_libraries(EmuGLRender log)
else()
target_link_libraries(EmuGLRender PUBLIC
    -static-libgcc
    -static-libstdc++
    -ldl
    -Wl,-rpath=./,--build-id=none
)
endif()
