/*
* Copyright (C) 2011 The Android Open Source Project
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
#pragma once

#include "ErrorLog.h"
#include "android/base/files/Stream.h"

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>

class IOStream {
protected:
    explicit IOStream(size_t bufSize) : m_bufsize(bufSize) {}

    ~IOStream() {
        // NOTE: m_buf was owned by the child class thus we expect it to be
        // released before the object destruction.
    }

public:
    virtual void *allocBuffer(size_t minSize) = 0;
    virtual int commitBuffer(size_t size) = 0;
    virtual int writeFully(const void* buf, size_t len) = 0;
    virtual const unsigned char *readFully( void *buf, size_t len) = 0;

    size_t read(void* buf, size_t bufLen) {
        if (!readRaw(buf, &bufLen)) {
            return 0;
        }
        return bufLen;
    }

    unsigned char* alloc(size_t len) {
        if (m_buf && len > m_free) {
            if (flush() < 0) {
                ERR("Failed to flush in alloc\n");
                return NULL; // we failed to flush so something is wrong
            }
        }

        if (!m_buf || len > m_bufsize) {
            int allocLen = m_bufsize < len ? len : m_bufsize;
            m_buf = (unsigned char *)allocBuffer(allocLen);
            if (!m_buf) {
                ERR("Alloc (%u bytes) failed\n", allocLen);
                return NULL;
            }
            m_bufsize = m_free = allocLen;
        }

        unsigned char* ptr = m_buf + (m_bufsize - m_free);
        m_free -= len;

        return ptr;
    }

    int flush() {
        if (!m_buf || m_free == m_bufsize) return 0;

        int stat = commitBuffer(m_bufsize - m_free);
        m_buf = NULL;
        m_free = 0;
        return stat;
    }

    const unsigned char *readback(void *buf, size_t len) {
        flush();
        return readFully(buf, len);
    }

    void save(android::base::Stream* stream) {
        stream->putBe32(m_bufsize);
        stream->putBe32(m_free);
        stream->putByte(m_buf != nullptr);
        onSave(stream);
    }

    void load(android::base::Stream* stream) {
        m_bufsize = stream->getBe32();
        m_free = stream->getBe32();
        const bool haveBuf = stream->getByte();
        const auto buf = onLoad(stream);
        m_buf = haveBuf ? buf : nullptr;
    }

    virtual void* getDmaForReading(uint64_t guest_paddr) = 0;
    virtual void unlockDma(uint64_t guest_paddr) = 0;

protected:
    virtual const unsigned char *readRaw(void *buf, size_t *inout_len) = 0;
    virtual void onSave(android::base::Stream* stream) = 0;
    virtual unsigned char* onLoad(android::base::Stream* stream) = 0;

    unsigned char* m_buf = nullptr;
    size_t m_bufsize;
    size_t m_free = 0;
};

using VmiWriteFullyFn = int (*)(uint32_t pid, uint32_t tid, const uint8_t *buf, size_t len);
class VkIOStream : public IOStream {
public:
    VkIOStream(uint32_t pid, uint32_t tid, VmiWriteFullyFn vmiWriteFully)
        : IOStream(8), m_pid(pid), m_tid(tid), m_vmiWriteFully(vmiWriteFully)
    {}

    ~VkIOStream() = default;

    unsigned char* alloc(size_t ) {
        ERR("Not support alloc()");
        return nullptr;
    }
    void *allocBuffer(size_t ) override {
        ERR("Not support allocBuffer()");
        return nullptr;
    }
    int commitBuffer(size_t ) override {
        ERR("Not support commitBuffer()");
        return -1;
    }

    int writeFully(const void* buf, size_t len) override {
        if (m_vmiWriteFully == nullptr) {
            ERR("VmiWriteFully is nullptr");
            return -1;
        }

        return m_vmiWriteFully(m_pid, m_tid, (const uint8_t *)buf, len);
    }

    int flush() {
        ERR("Not support flush()");
        return -1;
    }

    const unsigned char *readFully( void *, size_t ) override {
        ERR("Not support readFully()");
        return nullptr;
    }

    void* getDmaForReading(uint64_t ) override {
        ERR("Not support getDmaForReading()");
        return nullptr;
    }

    void unlockDma(uint64_t ) override {
        ERR("Not support unlockDma()");
    }

    const unsigned char *readRaw(void *, size_t *) override {
        ERR("Not support readRaw()");
        return nullptr;
    }

    void onSave(android::base::Stream* ) override {
        ERR("Not support onSave()");
    }

    unsigned char* onLoad(android::base::Stream* ) override {
        ERR("Not support onLoad()");
        return nullptr;
    }

private:
    uint32_t m_pid = 0;
    uint32_t m_tid = 0;
    VmiWriteFullyFn m_vmiWriteFully = nullptr;
};

