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
#ifdef _EMUGL_INTERFACE_H_
#define _EMUGL_INTERFACE_H_

#ifdef __cplusplus
extern "C" {
#endif

// 创建GLESv2Decoder对象
void* CreateGLESv2Decoder(uint32_t pid, uint32_t tid);

// 销毁GLESv2Decoder对象
void DestoryGLESv2Decoder(void*);


#ifdef __cplusplus
}
#endif

#endif