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
#ifndef _EMUGL_INTERFACE_H_
#define _EMUGL_INTERFACE_H_
#include <cstdint>
#include <functional>

#ifdef REMOTE_RENDER
class EncoderGLInterface {
public:
    static std::function<void(int width, int height)> initEncoder;
    static std::function<void()> encodeTex;
    static std::function<void(uint32_t *texture, uint32_t *target)> getEncodeTex;
};
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef REMOTE_RENDER
void SetInitEncoder(std::function<void(int width, int height)> func);

void SetEncodeTex(std::function<void()> func);

void SetGetEncodeTex(std::function<void(uint32_t *texture, uint32_t *target)> func);
#endif

// 创建GLESv2Decoder对象
void *CreateGLESv2Decoder(uint32_t pid, uint32_t tid);

// 销毁GLESv2Decoder对象
void DestoryGLESv2Decoder(void *);

#ifdef __cplusplus
}
#endif

#endif