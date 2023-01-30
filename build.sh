#!/bin/bash
# android-qemu build shell
# Copyright © Huawei Technologies Co., Ltd. 2021-2021. All rights reserved.

cur_file_path=$(cd $(dirname "${0}");pwd)
cd "${cur_file_path}"

build_type=${2}

AN_JOBS="$(grep processor /proc/cpuinfo | wc -l)"
strip_tool=${AN_AOSPDIR}/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android-strip
strip_linux_tool=strip

error()
{
    echo -e  "\033[1;31m${*}\033[0m"
}

info()
{
    echo -e "\033[1;36m${*}\033[0m"
}

check_env()
{
    if [ "${build_type}" == "android" ];then
        [ -z "${AN_NDKDIR}" ] && error "please set AN_NDKDIR is the path of NDK" && return -1
    fi
    return 0
}

ndk_compile()
{
    target=${1}
    folder=${2}
    if [ "${build_type}" == "android" ];then
        echo "AN_NDKDIR:${AN_NDKDIR}"
        cmake -DCMAKE_TOOLCHAIN_FILE=${AN_NDKDIR}/build/cmake/android.toolchain.cmake -DANDROID_ABI="${target}" -DANDROID_NDK=${AN_NDKDIR} -DANDROID_PLATFORM=android-22 ${folder}
        [ ${?} != 0 ] && error "Failed to cmake" && return -1
    else
        echo "linux build"
        cmake ${cur_file_path}/cmake -DREMOTE_RENDER=1
        #cmake ${cur_file_path}/cmake
        [ ${?} != 0 ] && error "Failed to cmake" && return -1
    fi
    make -j${AN_JOBS}
    [ ${?} != 0 ] && error "Failed to cmake" && return -1
    rm -rf symbols
    mkdir -p symbols
    so_lists=$(find -name *.so)
    for each_so in ${so_lists[@]}
    do
        cp ${each_so} ./symbols
        [ ${?} != 0 ] && error "Failed to cp ${each_so} to symbol" && return -1
    if [ "${build_type}" == "android" ];then
        ${strip_tool} -s ${each_so}
        [ ${?} != 0 ] && error "Failed to strip ${each_so}" && return -1
    else
        ${strip_linux_tool} -s ${each_so}
        [ ${?} != 0 ] && error "Failed to strip ${each_so}" && return -1
    fi
    done
    return 0
}

generate_file()
{
    cd ${cur_file_path}/android/android-emugl/host/tools/emugen/cmake
    rm -rf build
    mkdir build && cd build
    cmake .. && make -j
    [ ${?} != 0 ] && error "Failed to build emugen tools" && return -1
    gles2_path=${cur_file_path}/android/android-emugl/host/libs/GLESv2_dec
    ./emugen -i ${gles2_path} -D ${gles2_path} gles2
    [ ${?} != 0 ] && error "Failed to generate gles2 file" && return -1
    cd ${cur_file_path}
}

package()
{
    output_dir=${MODULE_OUTPUT_DIR}
    output_symbols_dir=${MODULE_SYMBOL_DIR}
    [ -z "${output_dir}" ] && output_dir=${cur_file_path}/output && rm -rf ${output_dir} && mkdir -p ${output_dir}
    [ -z "${output_symbols_dir}" ] && output_symbols_dir=${cur_file_path}/output/symbols && rm -rf ${output_symbols_dir} && mkdir -p ${output_symbols_dir}
    cp ${cur_file_path}/build/libEmuGLRender.so ${output_dir}
    [ ${?} != 0 ] && error "Failed to copy libEmuGLRender" && return -1
    cp ${cur_file_path}/build/symbols/libEmuGLRender.so ${output_symbols_dir}
    [ ${?} != 0 ] && error "Failed to copy libEmuGLRender symbol" && return -1
    if [ -z "${MODULE_OUTPUT_DIR}" ]; then
        cd output
        tar zcvf EmuGLRender.tar.gz libEmuGLRender.so
        cd -
    fi
    if [ -z "${MODULE_SYMBOL_DIR}" ]; then
        cd output/symbols
        tar zcvf ../EmuGLRenderSymbols.tar.gz libEmuGLRender.so
        cd -
    fi
}

inc()
{
    check_env
    [ ${?} != 0 ] && return -1
    generate_file
    [ ${?} != 0 ] && return -1
    mkdir -p build
    cd build
    ndk_compile arm64-v8a ${cur_file_path}/cmake
    [ ${?} != 0 ] && error "Failed to compile qemu" && return -1
    cd -
    package
    [ ${?} != 0 ] && error "Failed to package qemu" && return -1
    return 0
}

clean()
{
    rm -rf ${cur_file_path}/output
    rm -rf ${cur_file_path}/build
    rm -rf ${cur_file_path}/android/android-emugl/host/tools/emugen/cmake/build
}

build()
{
    clean
    [ ${?} != 0 ] && return -1
    inc
    [ ${?} != 0 ] && return -1
    return 0
}

ACTION=$1; shift
case "$ACTION" in
    build) build "$@";;
    clean) clean "$@";;
    inc) inc "$@";;
    *) error "input command[$ACTION] not support.";;
esac
