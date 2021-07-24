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