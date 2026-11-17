/* dkgl_api.c -- GL/EGL entry point table
 *
 * Used for eglGetProcAddress and to point UE's gl and egl imports (the import
 * table in imports.c) straight at dkgl, bypassing the Mesa-era wrappers.
 */
#include "dkgl.h"

#include "so_util.h"
#include "util.h"

void *dkgl_egl_proc(const char *name);
void *dkgl_missing_proc(const char *name); /* dkgl_port.c */

/* prototypes the Khronos headers only declare for extensions */
void glTexBufferEXT(GLenum, GLenum, GLuint);
void glTexBufferRangeEXT(GLenum, GLenum, GLuint, GLintptr, GLsizeiptr);
void glCopyImageSubDataEXT(GLuint, GLenum, GLint, GLint, GLint, GLint, GLuint,
                           GLenum, GLint, GLint, GLint, GLint, GLsizei,
                           GLsizei, GLsizei);
void glEnableiEXT(GLenum, GLuint);
void glDisableiEXT(GLenum, GLuint);
void glBufferStorageEXT(GLenum, GLsizeiptr, const void *, GLbitfield);
void glDiscardFramebufferEXT(GLenum, GLsizei, const GLenum *);
void glRenderbufferStorageMultisampleEXT(GLenum, GLsizei, GLenum, GLsizei,
                                         GLsizei);
void glFramebufferTexture2DMultisampleEXT(GLenum, GLenum, GLenum, GLuint, GLint,
                                          GLsizei);
void *glMapBufferOES(GLenum, GLenum);
GLboolean glUnmapBufferOES(GLenum);
void glDebugMessageControlKHR(GLenum, GLenum, GLenum, GLsizei, const GLuint *,
                              GLboolean);
void glDebugMessageInsertKHR(GLenum, GLenum, GLuint, GLenum, GLsizei,
                             const GLchar *);
void glDebugMessageCallbackKHR(GLDEBUGPROCKHR, const void *);
GLuint glGetDebugMessageLogKHR(GLuint, GLsizei, GLenum *, GLenum *, GLuint *,
                               GLenum *, GLsizei *, GLchar *);
void glPushDebugGroupKHR(GLenum, GLuint, GLsizei, const GLchar *);
void glPopDebugGroupKHR(void);
void glObjectLabelKHR(GLenum, GLuint, GLsizei, const GLchar *);
void glGetObjectLabelKHR(GLenum, GLuint, GLsizei, GLsizei *, GLchar *);
void glObjectPtrLabelKHR(const void *, GLsizei, const GLchar *);
void glGetObjectPtrLabelKHR(const void *, GLsizei, GLsizei *, GLchar *);
void glGetPointervKHR(GLenum, void **);
void glMemoryBarrierByRegion(GLbitfield);

#define F(name) {#name, (void *)name}

static const struct {
  const char *name;
  void *func;
} procs[] = {
    /* EGL */
    F(eglGetDisplay), F(eglInitialize), F(eglTerminate), F(eglBindAPI),
    F(eglQueryAPI), F(eglGetError), F(eglQueryString), F(eglGetConfigAttrib),
    F(eglGetConfigs), F(eglChooseConfig), F(eglCreateContext),
    F(eglDestroyContext), F(eglGetCurrentContext), F(eglGetCurrentDisplay),
    F(eglGetCurrentSurface), F(eglCreateWindowSurface),
    F(eglCreatePbufferSurface), F(eglDestroySurface), F(eglQuerySurface),
    F(eglSurfaceAttrib), F(eglMakeCurrent), F(eglSwapInterval),
    F(eglSwapBuffers), F(eglGetProcAddress), F(eglReleaseThread),
    F(eglWaitClient), F(eglWaitGL), F(eglWaitNative),
    /* buffers */
    F(glGenBuffers), F(glDeleteBuffers), F(glIsBuffer), F(glBindBuffer),
    F(glBindBufferRange), F(glBindBufferBase), F(glBufferData),
    F(glBufferStorageEXT), F(glBufferSubData), F(glMapBufferRange),
    F(glFlushMappedBufferRange), F(glUnmapBuffer), F(glCopyBufferSubData),
    F(glGetBufferParameteriv), F(glGetBufferParameteri64v),
    F(glGetBufferPointerv), F(glMapBufferOES), F(glUnmapBufferOES),
    /* textures and samplers */
    F(glGenTextures), F(glDeleteTextures), F(glIsTexture), F(glActiveTexture),
    F(glBindTexture), F(glTexParameteri), F(glTexParameterf),
    F(glTexParameteriv), F(glTexParameterfv), F(glGetTexParameteriv),
    F(glGetTexParameterfv), F(glTexImage2D), F(glTexImage3D),
    F(glCompressedTexImage2D), F(glCompressedTexImage3D), F(glTexSubImage2D),
    F(glTexSubImage3D), F(glCompressedTexSubImage2D),
    F(glCompressedTexSubImage3D), F(glTexStorage2D), F(glTexStorage3D),
    F(glTexStorage2DMultisample), F(glGenerateMipmap), F(glTexBuffer),
    F(glTexBufferRange), F(glTexBufferEXT), F(glTexBufferRangeEXT),
    F(glBindImageTexture), F(glCopyImageSubData), F(glCopyImageSubDataEXT),
    F(glGenSamplers), F(glDeleteSamplers), F(glIsSampler), F(glBindSampler),
    F(glSamplerParameteri), F(glSamplerParameterf), F(glSamplerParameteriv),
    F(glSamplerParameterfv), F(glGetSamplerParameteriv),
    F(glGetSamplerParameterfv), F(glCopyTexImage2D), F(glCopyTexSubImage2D),
    F(glCopyTexSubImage3D),
    /* framebuffers */
    F(glGenRenderbuffers), F(glDeleteRenderbuffers), F(glIsRenderbuffer),
    F(glBindRenderbuffer), F(glRenderbufferStorage),
    F(glRenderbufferStorageMultisample), F(glRenderbufferStorageMultisampleEXT),
    F(glGetRenderbufferParameteriv), F(glGenFramebuffers),
    F(glDeleteFramebuffers), F(glIsFramebuffer), F(glBindFramebuffer),
    F(glFramebufferTexture2D), F(glFramebufferTextureLayer),
    F(glFramebufferTexture), F(glFramebufferRenderbuffer),
    F(glFramebufferTexture2DMultisampleEXT), F(glCheckFramebufferStatus),
    F(glGetFramebufferAttachmentParameteriv), F(glDrawBuffers),
    F(glReadBuffer), F(glInvalidateFramebuffer),
    F(glInvalidateSubFramebuffer), F(glDiscardFramebufferEXT), F(glClear),
    F(glClearBufferfv), F(glClearBufferiv), F(glClearBufferuiv),
    F(glClearBufferfi), F(glBlitFramebuffer), F(glReadPixels),
    /* shaders and programs */
    F(glCreateShader), F(glDeleteShader), F(glIsShader), F(glShaderSource),
    F(glCompileShader), F(glGetShaderiv), F(glGetShaderInfoLog),
    F(glGetShaderSource), F(glShaderBinary), F(glReleaseShaderCompiler),
    F(glGetShaderPrecisionFormat), F(glCreateProgram), F(glDeleteProgram),
    F(glIsProgram), F(glAttachShader), F(glDetachShader),
    F(glGetAttachedShaders), F(glBindAttribLocation), F(glLinkProgram),
    F(glUseProgram), F(glValidateProgram), F(glProgramParameteri),
    F(glGetProgramBinary), F(glProgramBinary), F(glGetProgramiv),
    F(glGetActiveUniformsiv), F(glGetUniformIndices),
    F(glGetProgramInterfaceiv), F(glGetProgramResourceiv), F(glGetProgramResourceName),
    F(glGetProgramResourceIndex), F(glGetProgramResourceLocation),
    F(glGetProgramInfoLog), F(glGetAttribLocation), F(glGetActiveAttrib),
    F(glGetUniformLocation), F(glGetActiveUniform), F(glGetUniformBlockIndex),
    F(glUniformBlockBinding), F(glGetActiveUniformBlockiv),
    F(glGetActiveUniformBlockName), F(glUniform1f), F(glUniform2f),
    F(glUniform3f), F(glUniform4f), F(glUniform1i), F(glUniform2i),
    F(glUniform3i), F(glUniform4i), F(glUniform1ui), F(glUniform2ui),
    F(glUniform3ui), F(glUniform4ui), F(glUniform1fv), F(glUniform2fv),
    F(glUniform3fv), F(glUniform4fv), F(glUniform1iv), F(glUniform2iv),
    F(glUniform3iv), F(glUniform4iv), F(glUniform1uiv), F(glUniform2uiv),
    F(glUniform3uiv), F(glUniform4uiv), F(glUniformMatrix2fv),
    F(glUniformMatrix3fv), F(glUniformMatrix4fv), F(glUniformMatrix2x3fv),
    F(glUniformMatrix3x2fv), F(glUniformMatrix2x4fv), F(glUniformMatrix4x2fv),
    F(glUniformMatrix3x4fv), F(glUniformMatrix4x3fv), F(glGetUniformfv),
    F(glGetUniformiv), F(glGetUniformuiv),
    /* vertex arrays and draws */
    F(glGenVertexArrays), F(glDeleteVertexArrays), F(glIsVertexArray),
    F(glBindVertexArray), F(glVertexAttribPointer), F(glVertexAttribIPointer),
    F(glEnableVertexAttribArray), F(glDisableVertexAttribArray),
    F(glBindVertexBuffer), F(glVertexAttribFormat), F(glVertexAttribIFormat),
    F(glVertexAttribBinding), F(glVertexBindingDivisor),
    F(glVertexAttribDivisor), F(glVertexAttrib1f), F(glVertexAttrib2f),
    F(glVertexAttrib3f), F(glVertexAttrib4f), F(glVertexAttrib1fv),
    F(glVertexAttrib2fv), F(glVertexAttrib3fv), F(glVertexAttrib4fv),
    F(glVertexAttribI4i), F(glVertexAttribI4ui), F(glVertexAttribI4iv),
    F(glVertexAttribI4uiv), F(glGetVertexAttribiv), F(glGetVertexAttribfv),
    F(glGetVertexAttribPointerv), F(glDrawArrays), F(glDrawArraysInstanced),
    F(glDrawElements), F(glDrawElementsInstanced), F(glDrawRangeElements),
    F(glDrawArraysIndirect), F(glDrawElementsIndirect), F(glDispatchCompute),
    F(glDispatchComputeIndirect), F(glMemoryBarrier),
    F(glMemoryBarrierByRegion),
    /* state */
    F(glEnable), F(glDisable), F(glEnablei), F(glDisablei), F(glEnableiEXT),
    F(glDisableiEXT), F(glIsEnabled), F(glIsEnabledi), F(glBlendColor),
    F(glBlendEquation), F(glBlendEquationSeparate), F(glBlendEquationi),
    F(glBlendEquationSeparatei), F(glBlendFunc), F(glBlendFuncSeparate),
    F(glBlendFunci), F(glBlendFuncSeparatei), F(glColorMask), F(glColorMaski),
    F(glCullFace), F(glFrontFace), F(glDepthFunc), F(glDepthMask),
    F(glDepthRangef), F(glLineWidth), F(glPolygonOffset), F(glSampleCoverage),
    F(glStencilFunc), F(glStencilFuncSeparate), F(glStencilMask),
    F(glStencilMaskSeparate), F(glStencilOp), F(glStencilOpSeparate),
    F(glViewport), F(glScissor), F(glClearColor), F(glClearDepthf),
    F(glClearStencil), F(glHint), F(glPixelStorei), F(glGetError), F(glFlush),
    F(glFinish), F(glGetString), F(glGetStringi), F(glGetIntegerv),
    F(glGetInteger64v), F(glGetFloatv), F(glGetBooleanv), F(glGetIntegeri_v),
    F(glGetInteger64i_v), F(glGetBooleani_v), F(glGetInternalformativ),
    F(glGenQueries), F(glDeleteQueries), F(glIsQuery), F(glBeginQuery),
    F(glEndQuery), F(glGetQueryiv), F(glGetQueryObjectuiv), F(glFenceSync),
    F(glDeleteSync), F(glIsSync), F(glClientWaitSync), F(glWaitSync),
    F(glGetSynciv),
    /* KHR_debug */
    F(glDebugMessageControlKHR), F(glDebugMessageInsertKHR),
    F(glDebugMessageCallbackKHR), F(glGetDebugMessageLogKHR),
    F(glPushDebugGroupKHR), F(glPopDebugGroupKHR), F(glObjectLabelKHR),
    F(glGetObjectLabelKHR), F(glObjectPtrLabelKHR), F(glGetObjectPtrLabelKHR),
    F(glGetPointervKHR),
    /* base vertex (Unity's ES 2.0 renderer, through the OES spelling) */
    F(glDrawElementsBaseVertex), F(glDrawElementsInstancedBaseVertex),
};

#undef F

/* Extension spellings UE may ask for, mapped to the core function. */
static const struct {
  const char *alias;
  const char *name;
} aliases[] = {
    {"glBindVertexArrayOES", "glBindVertexArray"},
    {"glGenVertexArraysOES", "glGenVertexArrays"},
    {"glDeleteVertexArraysOES", "glDeleteVertexArrays"},
    {"glIsVertexArrayOES", "glIsVertexArray"},
    {"glMapBufferRangeEXT", "glMapBufferRange"},
    {"glFlushMappedBufferRangeEXT", "glFlushMappedBufferRange"},
    {"glGenQueriesEXT", "glGenQueries"},
    {"glDeleteQueriesEXT", "glDeleteQueries"},
    {"glIsQueryEXT", "glIsQuery"},
    {"glBeginQueryEXT", "glBeginQuery"},
    {"glEndQueryEXT", "glEndQuery"},
    {"glGetQueryivEXT", "glGetQueryiv"},
    {"glGetQueryObjectuivEXT", "glGetQueryObjectuiv"},
    {"glBlendEquationiEXT", "glBlendEquationi"},
    {"glBlendEquationSeparateiEXT", "glBlendEquationSeparatei"},
    {"glBlendFunciEXT", "glBlendFunci"},
    {"glBlendFuncSeparateiEXT", "glBlendFuncSeparatei"},
    {"glColorMaskiEXT", "glColorMaski"},
    {"glIsEnablediEXT", "glIsEnabledi"},
    {"glDebugMessageControl", "glDebugMessageControlKHR"},
    {"glDebugMessageInsert", "glDebugMessageInsertKHR"},
    {"glDebugMessageCallback", "glDebugMessageCallbackKHR"},
    {"glPushDebugGroup", "glPushDebugGroupKHR"},
    {"glPopDebugGroup", "glPopDebugGroupKHR"},
    {"glObjectLabel", "glObjectLabelKHR"},
    {"glGetPointerv", "glGetPointervKHR"},
    /* GLES 2.0 extension spellings (Unity's ES 2.0 renderer) */
    {"glTexImage3DOES", "glTexImage3D"},
    {"glTexSubImage3DOES", "glTexSubImage3D"},
    {"glCompressedTexImage3DOES", "glCompressedTexImage3D"},
    {"glCompressedTexSubImage3DOES", "glCompressedTexSubImage3D"},
    {"glCopyTexSubImage3DOES", "glCopyTexSubImage3D"},
    {"glDrawBuffersEXT", "glDrawBuffers"},
    {"glDrawBuffersNV", "glDrawBuffers"},
    {"glDrawElementsBaseVertexOES", "glDrawElementsBaseVertex"},
    {"glDrawElementsBaseVertexEXT", "glDrawElementsBaseVertex"},
    {"glDrawElementsInstancedBaseVertexOES", "glDrawElementsInstancedBaseVertex"},
    {"glDrawElementsInstancedBaseVertexEXT", "glDrawElementsInstancedBaseVertex"},
    {"glDrawArraysInstancedEXT", "glDrawArraysInstanced"},
    {"glDrawElementsInstancedEXT", "glDrawElementsInstanced"},
    {"glVertexAttribDivisorEXT", "glVertexAttribDivisor"},
    {"glGetProgramBinaryOES", "glGetProgramBinary"},
    {"glProgramBinaryOES", "glProgramBinary"},
    {"glGetBufferPointervOES", "glGetBufferPointerv"},
    {"glBlitFramebufferNV", "glBlitFramebuffer"},
    {"glRenderbufferStorageMultisampleNV", "glRenderbufferStorageMultisample"},
};

void *dkgl_get_proc(const char *name) {
  if (!name)
    return NULL;
  for (size_t i = 0; i < sizeof(procs) / sizeof(procs[0]); i++)
    if (!strcmp(procs[i].name, name))
      return procs[i].func;
  for (size_t i = 0; i < sizeof(aliases) / sizeof(aliases[0]); i++)
    if (!strcmp(aliases[i].alias, name))
      return dkgl_get_proc(aliases[i].name);
  void *egl = dkgl_egl_proc(name);
  if (egl || strncmp(name, "gl", 2) != 0)
    return egl;
  /* a GL function Mesa had and dkgl has not: a stub that says so (Mesa
   * offered every name; the engine may keep any pointer it gets) */
  return dkgl_missing_proc(name);
}

static void unimplemented_gl(void) {
  dkgl_log_once("an unimplemented GL/EGL import was called");
}

int dkgl_install_imports(void *table, size_t count) {
  DynLibFunction *funcs = table;
  int replaced = 0, missing = 0;
  for (size_t i = 0; i < count; i++) {
    const char *name = funcs[i].symbol;
    if (!name || !((name[0] == 'g' && name[1] == 'l') ||
                   (name[0] == 'e' && name[1] == 'g' && name[2] == 'l')))
      continue;
    void *p = dkgl_get_proc(name);
    if (p) {
      funcs[i].func = (uintptr_t)p;
      replaced++;
    } else {
      funcs[i].func = (uintptr_t)unimplemented_gl;
      missing++;
      debugPrintf("dkgl: no implementation for import %s\n", name);
    }
  }
  debugPrintf("dkgl: %d GL/EGL imports routed to deko3d (%d unimplemented)\n",
              replaced, missing);
  return missing;
}
