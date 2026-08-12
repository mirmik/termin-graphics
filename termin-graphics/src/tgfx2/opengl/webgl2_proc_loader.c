#include <GLES3/gl3.h>

#include <stdint.h>
#include <string.h>

/*
 * Emscripten intentionally recommends static GLES linking. Its generic
 * getProcAddress table only contains functions otherwise retained by the
 * linker, while GLAD discovers every function dynamically. This bridge makes
 * the WebGL2/ES3 core calls used by tgfx2 visible to the linker and presents
 * them through GLAD's loader contract.
 */
void* tgfx_webgl2_get_proc_address(const char* name) {
#define TGFX_WEBGL2_PROC(proc)                                                                                         \
    if (strcmp(name, #proc) == 0)                                                                                      \
        return (void*)(uintptr_t)&proc
    TGFX_WEBGL2_PROC(glActiveTexture);
    TGFX_WEBGL2_PROC(glAttachShader);
    TGFX_WEBGL2_PROC(glBindBuffer);
    TGFX_WEBGL2_PROC(glBindBufferBase);
    TGFX_WEBGL2_PROC(glBindBufferRange);
    TGFX_WEBGL2_PROC(glBindFramebuffer);
    TGFX_WEBGL2_PROC(glBindSampler);
    TGFX_WEBGL2_PROC(glBindTexture);
    TGFX_WEBGL2_PROC(glBindVertexArray);
    TGFX_WEBGL2_PROC(glBlendEquationSeparate);
    TGFX_WEBGL2_PROC(glBlendFuncSeparate);
    TGFX_WEBGL2_PROC(glBlitFramebuffer);
    TGFX_WEBGL2_PROC(glBufferData);
    TGFX_WEBGL2_PROC(glBufferSubData);
    TGFX_WEBGL2_PROC(glCheckFramebufferStatus);
    TGFX_WEBGL2_PROC(glClear);
    TGFX_WEBGL2_PROC(glClearBufferfv);
    TGFX_WEBGL2_PROC(glClearColor);
    TGFX_WEBGL2_PROC(glClientWaitSync);
    TGFX_WEBGL2_PROC(glColorMask);
    TGFX_WEBGL2_PROC(glCompileShader);
    TGFX_WEBGL2_PROC(glCopyBufferSubData);
    TGFX_WEBGL2_PROC(glCreateProgram);
    TGFX_WEBGL2_PROC(glCreateShader);
    TGFX_WEBGL2_PROC(glCullFace);
    TGFX_WEBGL2_PROC(glDeleteBuffers);
    TGFX_WEBGL2_PROC(glDeleteFramebuffers);
    TGFX_WEBGL2_PROC(glDeleteProgram);
    TGFX_WEBGL2_PROC(glDeleteSamplers);
    TGFX_WEBGL2_PROC(glDeleteShader);
    TGFX_WEBGL2_PROC(glDeleteSync);
    TGFX_WEBGL2_PROC(glDeleteTextures);
    TGFX_WEBGL2_PROC(glDeleteVertexArrays);
    TGFX_WEBGL2_PROC(glDepthFunc);
    TGFX_WEBGL2_PROC(glDepthMask);
    TGFX_WEBGL2_PROC(glDisable);
    TGFX_WEBGL2_PROC(glDrawArrays);
    TGFX_WEBGL2_PROC(glDrawArraysInstanced);
    TGFX_WEBGL2_PROC(glDrawBuffers);
    TGFX_WEBGL2_PROC(glDrawElements);
    TGFX_WEBGL2_PROC(glDrawElementsInstanced);
    TGFX_WEBGL2_PROC(glEnable);
    TGFX_WEBGL2_PROC(glEnableVertexAttribArray);
    TGFX_WEBGL2_PROC(glFenceSync);
    TGFX_WEBGL2_PROC(glFinish);
    TGFX_WEBGL2_PROC(glFlush);
    TGFX_WEBGL2_PROC(glFramebufferTexture2D);
    TGFX_WEBGL2_PROC(glFrontFace);
    TGFX_WEBGL2_PROC(glGenBuffers);
    TGFX_WEBGL2_PROC(glGenFramebuffers);
    TGFX_WEBGL2_PROC(glGenSamplers);
    TGFX_WEBGL2_PROC(glGenTextures);
    TGFX_WEBGL2_PROC(glGenVertexArrays);
    TGFX_WEBGL2_PROC(glGetBooleanv);
    TGFX_WEBGL2_PROC(glGetError);
    TGFX_WEBGL2_PROC(glGetIntegerv);
    TGFX_WEBGL2_PROC(glGetProgramInfoLog);
    TGFX_WEBGL2_PROC(glGetProgramiv);
    TGFX_WEBGL2_PROC(glGetShaderInfoLog);
    TGFX_WEBGL2_PROC(glGetShaderiv);
    TGFX_WEBGL2_PROC(glGetString);
    TGFX_WEBGL2_PROC(glGetStringi);
    TGFX_WEBGL2_PROC(glGetUniformBlockIndex);
    TGFX_WEBGL2_PROC(glGetUniformLocation);
    TGFX_WEBGL2_PROC(glIsEnabled);
    TGFX_WEBGL2_PROC(glLinkProgram);
    TGFX_WEBGL2_PROC(glMapBufferRange);
    TGFX_WEBGL2_PROC(glPixelStorei);
    TGFX_WEBGL2_PROC(glPolygonOffset);
    TGFX_WEBGL2_PROC(glReadBuffer);
    TGFX_WEBGL2_PROC(glReadPixels);
    TGFX_WEBGL2_PROC(glSamplerParameterf);
    TGFX_WEBGL2_PROC(glSamplerParameteri);
    TGFX_WEBGL2_PROC(glScissor);
    TGFX_WEBGL2_PROC(glShaderSource);
    TGFX_WEBGL2_PROC(glTexImage2D);
    TGFX_WEBGL2_PROC(glTexParameteri);
    TGFX_WEBGL2_PROC(glTexSubImage2D);
    TGFX_WEBGL2_PROC(glUniform1i);
    TGFX_WEBGL2_PROC(glUniformBlockBinding);
    TGFX_WEBGL2_PROC(glUnmapBuffer);
    TGFX_WEBGL2_PROC(glUseProgram);
    TGFX_WEBGL2_PROC(glVertexAttribDivisor);
    TGFX_WEBGL2_PROC(glVertexAttribIPointer);
    TGFX_WEBGL2_PROC(glVertexAttribPointer);
    TGFX_WEBGL2_PROC(glViewport);
#undef TGFX_WEBGL2_PROC
    return NULL;
}
