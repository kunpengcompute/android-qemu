/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2017-2019. All rights reserved.
 * Description:  OpengGL ES draw yuv video
 */
#ifndef YUV_DRAW_H
#define YUV_DRAW_H
#include <iostream>
#include <GLES2/gl2.h>
#include "ErrorLog.h"

namespace {
    const char *VERTEX_SHADER_STRING =
        "#version 300 es                                \n"
        "layout(location = 0) in vec4 vertexPosition;   \n"
        "layout(location = 1) in vec2 texCoord;         \n"
        "out vec2 outTexCoord;                          \n"
        "void main()                                    \n"
        "{                                              \n"
        "   gl_Position = vertexPosition;               \n"
        "   outTexCoord = texCoord;                     \n"
        "}                                              \n";

    const char *FRAGMENT_SHADER_STRING =
        "#version 300 es                                                    \n"
        "precision mediump float;                                           \n"
        "in vec2 outTexCoord;                                               \n"
        "layout(location = 0) out vec4 outFragmentColor;                    \n"
        "uniform sampler2D yTexture;                                        \n"
        "uniform sampler2D uTexture;                                        \n"
        "uniform sampler2D vTexture;                                        \n"
        "uniform int imageType;                                             \n"
        "void main()                                                        \n"
        "{                                                                  \n"
        "   if (imageType == 21) {                                          \n"
        "	    vec3 yuv;										            \n"
        "       yuv.x = texture(yTexture, outTexCoord).r;  	                \n"
        "       yuv.y = texture(uTexture, outTexCoord).r - 0.5;	            \n"
        "       yuv.z = texture(uTexture, outTexCoord).a - 0.5;	            \n"
        "	    highp vec3 rgb = mat3(1.0,       1.0,       1.0,            \n"
        "                              0, 		-0.344, 	1.770,          \n"
        "                             1.403,    -0.714,     0) * yuv; 	    \n"
        "	    outFragmentColor = vec4(rgb, 1.0);		                    \n"
        "   } else if (imageType == 19) {                                   \n"
        "	    vec3 yuv;										            \n"
        "       yuv.x = texture(yTexture, outTexCoord).r;  	                \n"
        "       yuv.y = texture(uTexture, outTexCoord).r - 0.5;	            \n"
        "       yuv.z = texture(vTexture, outTexCoord).r - 0.5;	            \n"
        "	    highp vec3 rgb = mat3(1.0,       1.0,       1.0,            \n"
        "                              0, 		-0.344, 	1.770,          \n"
        "                             1.403,    -0.714,     0) * yuv; 	    \n"
        "	    outFragmentColor = vec4(rgb, 1.0);		                    \n"
        "   }                                                               \n"
        "}                                                                  \n";

    const GLfloat VERTICE_LOCATION[] = {
        1.0f,  -1.0f,  0.0f,
        1.0f,   1.0f,  0.0f,
        -1.0f,  1.0f,  0.0f,
        -1.0f, -1.0f,  0.0f
    };

    const GLfloat TEXTURE_LOCATION[] = {
        1.0f,  0.0f,
        1.0f,  1.0f,
        0.0f,  1.0f,
        0.0f,  0.0f
    };

    const GLushort INDICES[] = { 0, 1, 2, 0, 2, 3 };
    const int32_t YUV_FORMAT_I420 = 19;
    const int32_t YUV_FORMAT_NV12 = 21;
}

template <class T>
class YuvDraw {
public:
    /**
     * @brief: YuvDraw constructor function
     */
    YuvDraw() : m_program(GL_NONE),
        m_yTextureId(GL_NONE),
        m_uTextureId(GL_NONE),
        m_vTextureId(GL_NONE),
        m_vertexBuffer(GL_NONE),
        m_texBuffer(GL_NONE),
        m_vao(GL_NONE),
        m_ySampleLocation(GL_NONE),
        m_uSampleLocation(GL_NONE),
        m_vSampleLocation(GL_NONE),
        m_imageType(GL_NONE),
        m_glesContext(nullptr)
    {
    }

    /**
     * @brief: YuvDraw desctructor function
     */
    ~YuvDraw()
    {
        if (m_glesContext == nullptr) {
            return;
        }

        if (m_yTextureId != GL_NONE) {
            m_glesContext->glDeleteTextures(1, &m_yTextureId);
        }

        if (m_uTextureId != GL_NONE) {
            m_glesContext->glDeleteTextures(1, &m_uTextureId);
        }

        if (m_vTextureId != GL_NONE) {
            m_glesContext->glDeleteTextures(1, &m_vTextureId);
        }

        if (m_texBuffer != GL_NONE) {
            m_glesContext->glDeleteBuffers(1, &m_texBuffer);
        }

        if (m_vertexBuffer != GL_NONE) {
            m_glesContext->glDeleteBuffers(1, &m_vertexBuffer);
        }

        if (m_vao != GL_NONE) {
            m_glesContext->glDeleteVertexArrays(1, &m_vao);
        }

        if (m_program != GL_NONE) {
            m_glesContext->glDeleteProgram(m_program);
        }
    }

    /**
     * @brief: Initialize function pointers
     * @param [in] glesContext: opengles context handle
     * @return: init result, false is fail, true is success
     */
    bool Init(T glesContext)
    {
        if (glesContext == nullptr) {
            ERR("m_glesContext is nullptr");
            return false;
        }

        m_glesContext = glesContext;
        m_program = CreateProgram();
        if (m_program == GL_NONE) {
            ERR("Create Program failed");
            return false;
        }

        m_glesContext->glGenVertexArrays(1, &m_vao);
        const int vboSize = 2;
        const int vertexAttribSize = 3;
        const int texAttribSize = 2;
        GLuint vbo[vboSize] = {0};
        m_glesContext->glGenBuffers(vboSize, vbo);
        m_vertexBuffer = vbo[0];
        m_texBuffer = vbo[1];

        m_glesContext->glBindVertexArray(m_vao);
        m_glesContext->glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
        m_glesContext->glBufferData(GL_ARRAY_BUFFER, sizeof(VERTICE_LOCATION), VERTICE_LOCATION, GL_STATIC_DRAW);
        m_glesContext->glVertexAttribPointer(0, vertexAttribSize, GL_FLOAT, GL_FALSE,
                                             vertexAttribSize * sizeof(float), nullptr);
        m_glesContext->glEnableVertexAttribArray(0);

        m_glesContext->glBindBuffer(GL_ARRAY_BUFFER, m_texBuffer);
        m_glesContext->glBufferData(GL_ARRAY_BUFFER, sizeof(TEXTURE_LOCATION), TEXTURE_LOCATION, GL_STATIC_DRAW);
        m_glesContext->glVertexAttribPointer(1, texAttribSize, GL_FLOAT, GL_FALSE,
                                             texAttribSize * sizeof(float), nullptr);
        m_glesContext->glEnableVertexAttribArray(1);
        m_glesContext->glBindBuffer(GL_ARRAY_BUFFER, 0);
        m_glesContext->glBindVertexArray(0);

        m_ySampleLocation = m_glesContext->glGetUniformLocation(m_program, "yTexture");
        m_uSampleLocation = m_glesContext->glGetUniformLocation(m_program, "uTexture");
        m_vSampleLocation = m_glesContext->glGetUniformLocation(m_program, "vTexture");
        m_imageType = m_glesContext->glGetUniformLocation(m_program, "imageType");

        const int textureIdSize = 3;
        GLuint textureIds[textureIdSize] = {0};
        m_glesContext->glGenTextures(textureIdSize, textureIds);
        const int yTextureId = 0;
        const int uTextureId = 1;
        const int vTextureId = 2;
        m_yTextureId = textureIds[yTextureId];
        m_uTextureId = textureIds[uTextureId];
        m_vTextureId = textureIds[vTextureId];
        return true;
    }

    /**
     * @brief: yuv draw function
     * @param [in] x: yuv x coordinate start position
     * @param [in] y: yuv y coordinate start position
     * @param [in] width: yuv width
     * @param [in] height: yuv height
     * @param [in] yuvFormat: yuv format
     * @param [in] yuvData: yuv data
     */
    void Draw(int x, int y, int width, int height, int32_t yuvFormat, uint8_t* yuvData)
    {
        switch (yuvFormat) {
            case YUV_FORMAT_I420:
                FillI420Texture(width, height, yuvData);
                break;
            case YUV_FORMAT_NV12:
                FillNv12Texture(width, height, yuvData);
                break;
            default:
                ERR("unsupport format:%d", yuvFormat);
                return;
        }

        const int viewPortX = 0;
        const int viewPortY = 1;
        const int viewPortWidth = 2;
        const int viewPortHeight = 3;
        const int firstTexure = 0;
        const int secondTexture = 1;
        const int thirdTexture = 2;
        const int vertexCount = 6;
        GLint viewPort[4] = {0};
        m_glesContext->glGetIntegerv(GL_VIEWPORT, viewPort);
        m_glesContext->glViewport(x, y, width, height);

        m_glesContext->glUseProgram(m_program);
        m_glesContext->glBindVertexArray(m_vao);
        m_glesContext->glActiveTexture(GL_TEXTURE0);
        m_glesContext->glBindTexture(GL_TEXTURE_2D, m_yTextureId);
        m_glesContext->glUniform1i(m_ySampleLocation, firstTexure);
        m_glesContext->glActiveTexture(GL_TEXTURE1);
        m_glesContext->glBindTexture(GL_TEXTURE_2D, m_uTextureId);
        m_glesContext->glUniform1i(m_uSampleLocation, secondTexture);
        if (yuvFormat == YUV_FORMAT_I420) {
            m_glesContext->glActiveTexture(GL_TEXTURE2);
            m_glesContext->glBindTexture(GL_TEXTURE_2D, m_vTextureId);
            m_glesContext->glUniform1i(m_vSampleLocation, thirdTexture);
        }
        m_glesContext->glUniform1i(m_imageType, yuvFormat);
        m_glesContext->glDrawElements(GL_TRIANGLES, vertexCount, GL_UNSIGNED_SHORT, INDICES);
        m_glesContext->glBindVertexArray(0);
        m_glesContext->glViewport(viewPort[viewPortX], viewPort[viewPortY],
                                  viewPort[viewPortWidth], viewPort[viewPortHeight]);
        m_glesContext->glUseProgram(0);
    }

private:
    /**
     * @brief: create opengles program for yuv draw
     * @return: opengles program handle, zero is fail, non zero is success
     */
    GLuint CreateProgram()
    {
        GLuint program = GL_NONE;
        GLuint vertexShader = LoadShader(GL_VERTEX_SHADER, VERTEX_SHADER_STRING);
        if (vertexShader == GL_NONE) {
            ERR("create vertexShader failed");
            return program;
        }

        GLuint fragramShader = LoadShader(GL_FRAGMENT_SHADER, FRAGMENT_SHADER_STRING);
        if (fragramShader == GL_NONE) {
            ERR("create fragramShader failed");
            return program;
        }

        program = m_glesContext->glCreateProgram();
        m_glesContext->glAttachShader(program, vertexShader);
        m_glesContext->glAttachShader(program, fragramShader);
        m_glesContext->glLinkProgram(program);

        GLint status = GL_FALSE;
        m_glesContext->glGetProgramiv(program, GL_LINK_STATUS, &status);
        m_glesContext->glDeleteShader(vertexShader);
        m_glesContext->glDeleteShader(fragramShader);
        vertexShader = GL_NONE;
        fragramShader = GL_NONE;
        if (status == GL_TRUE) {
            return program;
        }

        const int maxInfoLength = 512;
        char linkInfo[maxInfoLength] = {0};
        m_glesContext->glGetProgramInfoLog(program, maxInfoLength, nullptr, linkInfo);
        ERR("can link program:%d, error message:%s", program, linkInfo);
        m_glesContext->glDeleteProgram(program);
        program = GL_NONE;
        return program;
    }

    /**
     * @brief: create opengles shader function
     * @param [in] shaderType: shader type
     * @param [in] shaderString: shader source
     * @return: shader handle, zero is fail, non zero is success
     */
    GLuint LoadShader(GLenum shaderType, const char* shaderString)
    {
        GLuint shader = m_glesContext->glCreateShader(shaderType);
        m_glesContext->glShaderSource(shader, 1, &shaderString, nullptr);
        m_glesContext->glCompileShader(shader);
        GLint status = GL_FALSE;
        m_glesContext->glGetShaderiv(shader, GL_COMPILE_STATUS, &status);

        if (status == GL_TRUE) {
            return shader;
        }

        const int maxInfoLength = 512;
        char compileInfo[maxInfoLength] = {0};
        m_glesContext->glGetShaderInfoLog(shader, maxInfoLength, nullptr, compileInfo);
        ERR("compile shader failed, shader source:%s, error message:%s", shaderString, compileInfo);
        m_glesContext->glDeleteShader(shader);
        shader = GL_NONE;
        return shader;
    }

    /**
     * @brief: file nv12 texture
     * @param [in] width: yuv width
     * @param [in] height: yuv height
     * @param [in] yuvData: yuv data
     */
    void FillNv12Texture(int width, int height, uint8_t* yuvData)
    {
        uint8_t* yData = yuvData;
        m_glesContext->glBindTexture(GL_TEXTURE_2D, m_yTextureId);
        m_glesContext->glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width, height, 0,
            GL_LUMINANCE, GL_UNSIGNED_BYTE, yData);
        m_glesContext->glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        m_glesContext->glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_glesContext->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        m_glesContext->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        m_glesContext->glBindTexture(GL_TEXTURE_2D, GL_NONE);

        uint8_t* uvData = yuvData +  width * height;
        m_glesContext->glBindTexture(GL_TEXTURE_2D, m_uTextureId);
        m_glesContext->glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, width >> 1, height >> 1, 0,
            GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, uvData);
        m_glesContext->glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        m_glesContext->glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_glesContext->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        m_glesContext->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        m_glesContext->glBindTexture(GL_TEXTURE_2D, GL_NONE);
    }

    /**
     * @brief: file I420 texture
     * @param [in] width: yuv width
     * @param [in] height: yuv height
     * @param [in] yuvData: yuv data
     */
    void FillI420Texture(int width, int height, uint8_t* yuvData)
    {
        uint8_t* yData = yuvData;
        m_glesContext->glBindTexture(GL_TEXTURE_2D, m_yTextureId);
        m_glesContext->glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width, height, 0,
            GL_LUMINANCE, GL_UNSIGNED_BYTE, yData);
        m_glesContext->glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        m_glesContext->glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_glesContext->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        m_glesContext->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        m_glesContext->glBindTexture(GL_TEXTURE_2D, GL_NONE);

        uint8_t* uData = yuvData +  width * height;
        m_glesContext->glBindTexture(GL_TEXTURE_2D, m_uTextureId);
        m_glesContext->glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width >> 1, height >> 1, 0,
            GL_LUMINANCE, GL_UNSIGNED_BYTE, uData);
        m_glesContext->glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        m_glesContext->glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_glesContext->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        m_glesContext->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        m_glesContext->glBindTexture(GL_TEXTURE_2D, GL_NONE);

        const int vDataProportion = 4;
        uint8_t* vData = uData +  width * height / vDataProportion;
        m_glesContext->glBindTexture(GL_TEXTURE_2D, m_vTextureId);
        m_glesContext->glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width >> 1, height >> 1, 0,
            GL_LUMINANCE, GL_UNSIGNED_BYTE, vData);
        m_glesContext->glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        m_glesContext->glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_glesContext->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        m_glesContext->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        m_glesContext->glBindTexture(GL_TEXTURE_2D, GL_NONE);
    }

    GLuint m_program = GL_NONE;
    GLuint m_yTextureId = GL_NONE;
    GLuint m_uTextureId = GL_NONE;
    GLuint m_vTextureId = GL_NONE;
    GLuint m_vertexBuffer = GL_NONE;
    GLuint m_texBuffer = GL_NONE;
    GLuint m_vao = GL_NONE;
    GLint m_ySampleLocation = GL_NONE;
    GLint m_uSampleLocation = GL_NONE;
    GLint m_vSampleLocation = GL_NONE;
    GLint m_imageType = GL_NONE;
    T m_glesContext = nullptr;
};
#endif