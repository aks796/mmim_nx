/* gl_table.c -- every OpenGL ES (2.0-3.2) and EGL entry point the Switch mesa
 * libraries export, for the system import table (the deko3d build has none:
 * dkgl answers every name through eglGetProcAddress). Generated from
 * libGLESv2.a/libEGL.a (nm); extension functions are not exported by mesa and
 * resolve through eglGetProcAddress instead (see main.c).
 *
 * Only the addresses matter here, so no GL headers: every function is
 * declared with a placeholder type.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef MVL_DEKO3D

#include <stdint.h>
#include "so_util.h"

extern void glActiveShaderProgram(void);
extern void glActiveTexture(void);
extern void glAttachShader(void);
extern void glBeginQuery(void);
extern void glBeginTransformFeedback(void);
extern void glBindAttribLocation(void);
extern void glBindBuffer(void);
extern void glBindBufferBase(void);
extern void glBindBufferRange(void);
extern void glBindFramebuffer(void);
extern void glBindImageTexture(void);
extern void glBindProgramPipeline(void);
extern void glBindRenderbuffer(void);
extern void glBindSampler(void);
extern void glBindTexture(void);
extern void glBindTransformFeedback(void);
extern void glBindVertexArray(void);
extern void glBindVertexBuffer(void);
extern void glBlendBarrier(void);
extern void glBlendColor(void);
extern void glBlendEquation(void);
extern void glBlendEquationSeparate(void);
extern void glBlendEquationSeparatei(void);
extern void glBlendEquationi(void);
extern void glBlendFunc(void);
extern void glBlendFuncSeparate(void);
extern void glBlendFuncSeparatei(void);
extern void glBlendFunci(void);
extern void glBlitFramebuffer(void);
extern void glBufferData(void);
extern void glBufferSubData(void);
extern void glCheckFramebufferStatus(void);
extern void glClear(void);
extern void glClearBufferfi(void);
extern void glClearBufferfv(void);
extern void glClearBufferiv(void);
extern void glClearBufferuiv(void);
extern void glClearColor(void);
extern void glClearDepthf(void);
extern void glClearStencil(void);
extern void glClientWaitSync(void);
extern void glColorMask(void);
extern void glColorMaski(void);
extern void glCompileShader(void);
extern void glCompressedTexImage2D(void);
extern void glCompressedTexImage3D(void);
extern void glCompressedTexSubImage2D(void);
extern void glCompressedTexSubImage3D(void);
extern void glCopyBufferSubData(void);
extern void glCopyImageSubData(void);
extern void glCopyTexImage2D(void);
extern void glCopyTexSubImage2D(void);
extern void glCopyTexSubImage3D(void);
extern void glCreateProgram(void);
extern void glCreateShader(void);
extern void glCreateShaderProgramv(void);
extern void glCullFace(void);
extern void glDebugMessageCallback(void);
extern void glDebugMessageControl(void);
extern void glDebugMessageInsert(void);
extern void glDeleteBuffers(void);
extern void glDeleteFramebuffers(void);
extern void glDeleteProgram(void);
extern void glDeleteProgramPipelines(void);
extern void glDeleteQueries(void);
extern void glDeleteRenderbuffers(void);
extern void glDeleteSamplers(void);
extern void glDeleteShader(void);
extern void glDeleteSync(void);
extern void glDeleteTextures(void);
extern void glDeleteTransformFeedbacks(void);
extern void glDeleteVertexArrays(void);
extern void glDepthFunc(void);
extern void glDepthMask(void);
extern void glDepthRangef(void);
extern void glDetachShader(void);
extern void glDisable(void);
extern void glDisableVertexAttribArray(void);
extern void glDisablei(void);
extern void glDispatchCompute(void);
extern void glDispatchComputeIndirect(void);
extern void glDrawArrays(void);
extern void glDrawArraysIndirect(void);
extern void glDrawArraysInstanced(void);
extern void glDrawBuffers(void);
extern void glDrawElements(void);
extern void glDrawElementsBaseVertex(void);
extern void glDrawElementsIndirect(void);
extern void glDrawElementsInstanced(void);
extern void glDrawElementsInstancedBaseVertex(void);
extern void glDrawRangeElements(void);
extern void glDrawRangeElementsBaseVertex(void);
extern void glEnable(void);
extern void glEnableVertexAttribArray(void);
extern void glEnablei(void);
extern void glEndQuery(void);
extern void glEndTransformFeedback(void);
extern void glFenceSync(void);
extern void glFinish(void);
extern void glFlush(void);
extern void glFlushMappedBufferRange(void);
extern void glFramebufferParameteri(void);
extern void glFramebufferRenderbuffer(void);
extern void glFramebufferTexture(void);
extern void glFramebufferTexture2D(void);
extern void glFramebufferTextureLayer(void);
extern void glFrontFace(void);
extern void glGenBuffers(void);
extern void glGenFramebuffers(void);
extern void glGenProgramPipelines(void);
extern void glGenQueries(void);
extern void glGenRenderbuffers(void);
extern void glGenSamplers(void);
extern void glGenTextures(void);
extern void glGenTransformFeedbacks(void);
extern void glGenVertexArrays(void);
extern void glGenerateMipmap(void);
extern void glGetActiveAttrib(void);
extern void glGetActiveUniform(void);
extern void glGetActiveUniformBlockName(void);
extern void glGetActiveUniformBlockiv(void);
extern void glGetActiveUniformsiv(void);
extern void glGetAttachedShaders(void);
extern void glGetAttribLocation(void);
extern void glGetBooleani_v(void);
extern void glGetBooleanv(void);
extern void glGetBufferParameteri64v(void);
extern void glGetBufferParameteriv(void);
extern void glGetBufferPointerv(void);
extern void glGetDebugMessageLog(void);
extern void glGetError(void);
extern void glGetFloatv(void);
extern void glGetFragDataLocation(void);
extern void glGetFramebufferAttachmentParameteriv(void);
extern void glGetFramebufferParameteriv(void);
extern void glGetGraphicsResetStatus(void);
extern void glGetInteger64i_v(void);
extern void glGetInteger64v(void);
extern void glGetIntegeri_v(void);
extern void glGetIntegerv(void);
extern void glGetInternalformativ(void);
extern void glGetMultisamplefv(void);
extern void glGetObjectLabel(void);
extern void glGetObjectPtrLabel(void);
extern void glGetPointerv(void);
extern void glGetProgramBinary(void);
extern void glGetProgramInfoLog(void);
extern void glGetProgramInterfaceiv(void);
extern void glGetProgramPipelineInfoLog(void);
extern void glGetProgramPipelineiv(void);
extern void glGetProgramResourceIndex(void);
extern void glGetProgramResourceLocation(void);
extern void glGetProgramResourceName(void);
extern void glGetProgramResourceiv(void);
extern void glGetProgramiv(void);
extern void glGetQueryObjectuiv(void);
extern void glGetQueryiv(void);
extern void glGetRenderbufferParameteriv(void);
extern void glGetSamplerParameterIiv(void);
extern void glGetSamplerParameterIuiv(void);
extern void glGetSamplerParameterfv(void);
extern void glGetSamplerParameteriv(void);
extern void glGetShaderInfoLog(void);
extern void glGetShaderPrecisionFormat(void);
extern void glGetShaderSource(void);
extern void glGetShaderiv(void);
extern void glGetString(void);
extern void glGetStringi(void);
extern void glGetSynciv(void);
extern void glGetTexLevelParameterfv(void);
extern void glGetTexLevelParameteriv(void);
extern void glGetTexParameterIiv(void);
extern void glGetTexParameterIuiv(void);
extern void glGetTexParameterfv(void);
extern void glGetTexParameteriv(void);
extern void glGetTransformFeedbackVarying(void);
extern void glGetUniformBlockIndex(void);
extern void glGetUniformIndices(void);
extern void glGetUniformLocation(void);
extern void glGetUniformfv(void);
extern void glGetUniformiv(void);
extern void glGetUniformuiv(void);
extern void glGetVertexAttribIiv(void);
extern void glGetVertexAttribIuiv(void);
extern void glGetVertexAttribPointerv(void);
extern void glGetVertexAttribfv(void);
extern void glGetVertexAttribiv(void);
extern void glGetnUniformfv(void);
extern void glGetnUniformiv(void);
extern void glGetnUniformuiv(void);
extern void glHint(void);
extern void glInvalidateFramebuffer(void);
extern void glInvalidateSubFramebuffer(void);
extern void glIsBuffer(void);
extern void glIsEnabled(void);
extern void glIsEnabledi(void);
extern void glIsFramebuffer(void);
extern void glIsProgram(void);
extern void glIsProgramPipeline(void);
extern void glIsQuery(void);
extern void glIsRenderbuffer(void);
extern void glIsSampler(void);
extern void glIsShader(void);
extern void glIsSync(void);
extern void glIsTexture(void);
extern void glIsTransformFeedback(void);
extern void glIsVertexArray(void);
extern void glLineWidth(void);
extern void glLinkProgram(void);
extern void glMapBufferRange(void);
extern void glMemoryBarrier(void);
extern void glMemoryBarrierByRegion(void);
extern void glMinSampleShading(void);
extern void glObjectLabel(void);
extern void glObjectPtrLabel(void);
extern void glPatchParameteri(void);
extern void glPauseTransformFeedback(void);
extern void glPixelStorei(void);
extern void glPolygonOffset(void);
extern void glPopDebugGroup(void);
extern void glPrimitiveBoundingBox(void);
extern void glProgramBinary(void);
extern void glProgramParameteri(void);
extern void glProgramUniform1f(void);
extern void glProgramUniform1fv(void);
extern void glProgramUniform1i(void);
extern void glProgramUniform1iv(void);
extern void glProgramUniform1ui(void);
extern void glProgramUniform1uiv(void);
extern void glProgramUniform2f(void);
extern void glProgramUniform2fv(void);
extern void glProgramUniform2i(void);
extern void glProgramUniform2iv(void);
extern void glProgramUniform2ui(void);
extern void glProgramUniform2uiv(void);
extern void glProgramUniform3f(void);
extern void glProgramUniform3fv(void);
extern void glProgramUniform3i(void);
extern void glProgramUniform3iv(void);
extern void glProgramUniform3ui(void);
extern void glProgramUniform3uiv(void);
extern void glProgramUniform4f(void);
extern void glProgramUniform4fv(void);
extern void glProgramUniform4i(void);
extern void glProgramUniform4iv(void);
extern void glProgramUniform4ui(void);
extern void glProgramUniform4uiv(void);
extern void glProgramUniformMatrix2fv(void);
extern void glProgramUniformMatrix2x3fv(void);
extern void glProgramUniformMatrix2x4fv(void);
extern void glProgramUniformMatrix3fv(void);
extern void glProgramUniformMatrix3x2fv(void);
extern void glProgramUniformMatrix3x4fv(void);
extern void glProgramUniformMatrix4fv(void);
extern void glProgramUniformMatrix4x2fv(void);
extern void glProgramUniformMatrix4x3fv(void);
extern void glPushDebugGroup(void);
extern void glReadBuffer(void);
extern void glReadPixels(void);
extern void glReadnPixels(void);
extern void glReleaseShaderCompiler(void);
extern void glRenderbufferStorage(void);
extern void glRenderbufferStorageMultisample(void);
extern void glResumeTransformFeedback(void);
extern void glSampleCoverage(void);
extern void glSampleMaski(void);
extern void glSamplerParameterIiv(void);
extern void glSamplerParameterIuiv(void);
extern void glSamplerParameterf(void);
extern void glSamplerParameterfv(void);
extern void glSamplerParameteri(void);
extern void glSamplerParameteriv(void);
extern void glScissor(void);
extern void glShaderBinary(void);
extern void glShaderSource(void);
extern void glStencilFunc(void);
extern void glStencilFuncSeparate(void);
extern void glStencilMask(void);
extern void glStencilMaskSeparate(void);
extern void glStencilOp(void);
extern void glStencilOpSeparate(void);
extern void glTexBuffer(void);
extern void glTexBufferRange(void);
extern void glTexImage2D(void);
extern void glTexImage3D(void);
extern void glTexParameterIiv(void);
extern void glTexParameterIuiv(void);
extern void glTexParameterf(void);
extern void glTexParameterfv(void);
extern void glTexParameteri(void);
extern void glTexParameteriv(void);
extern void glTexStorage2D(void);
extern void glTexStorage2DMultisample(void);
extern void glTexStorage3D(void);
extern void glTexStorage3DMultisample(void);
extern void glTexSubImage2D(void);
extern void glTexSubImage3D(void);
extern void glTransformFeedbackVaryings(void);
extern void glUniform1f(void);
extern void glUniform1fv(void);
extern void glUniform1i(void);
extern void glUniform1iv(void);
extern void glUniform1ui(void);
extern void glUniform1uiv(void);
extern void glUniform2f(void);
extern void glUniform2fv(void);
extern void glUniform2i(void);
extern void glUniform2iv(void);
extern void glUniform2ui(void);
extern void glUniform2uiv(void);
extern void glUniform3f(void);
extern void glUniform3fv(void);
extern void glUniform3i(void);
extern void glUniform3iv(void);
extern void glUniform3ui(void);
extern void glUniform3uiv(void);
extern void glUniform4f(void);
extern void glUniform4fv(void);
extern void glUniform4i(void);
extern void glUniform4iv(void);
extern void glUniform4ui(void);
extern void glUniform4uiv(void);
extern void glUniformBlockBinding(void);
extern void glUniformMatrix2fv(void);
extern void glUniformMatrix2x3fv(void);
extern void glUniformMatrix2x4fv(void);
extern void glUniformMatrix3fv(void);
extern void glUniformMatrix3x2fv(void);
extern void glUniformMatrix3x4fv(void);
extern void glUniformMatrix4fv(void);
extern void glUniformMatrix4x2fv(void);
extern void glUniformMatrix4x3fv(void);
extern void glUnmapBuffer(void);
extern void glUseProgram(void);
extern void glUseProgramStages(void);
extern void glValidateProgram(void);
extern void glValidateProgramPipeline(void);
extern void glVertexAttrib1f(void);
extern void glVertexAttrib1fv(void);
extern void glVertexAttrib2f(void);
extern void glVertexAttrib2fv(void);
extern void glVertexAttrib3f(void);
extern void glVertexAttrib3fv(void);
extern void glVertexAttrib4f(void);
extern void glVertexAttrib4fv(void);
extern void glVertexAttribBinding(void);
extern void glVertexAttribDivisor(void);
extern void glVertexAttribFormat(void);
extern void glVertexAttribI4i(void);
extern void glVertexAttribI4iv(void);
extern void glVertexAttribI4ui(void);
extern void glVertexAttribI4uiv(void);
extern void glVertexAttribIFormat(void);
extern void glVertexAttribIPointer(void);
extern void glVertexAttribPointer(void);
extern void glVertexBindingDivisor(void);
extern void glViewport(void);
extern void glWaitSync(void);
extern void eglBindAPI(void);
extern void eglBindTexImage(void);
extern void eglChooseConfig(void);
extern void eglClientWaitSync(void);
extern void eglCopyBuffers(void);
extern void eglCreateContext(void);
extern void eglCreateImage(void);
extern void eglCreatePbufferFromClientBuffer(void);
extern void eglCreatePbufferSurface(void);
extern void eglCreatePixmapSurface(void);
extern void eglCreatePlatformPixmapSurface(void);
extern void eglCreatePlatformWindowSurface(void);
extern void eglCreateSync(void);
extern void eglCreateWindowSurface(void);
extern void eglDestroyContext(void);
extern void eglDestroyImage(void);
extern void eglDestroySurface(void);
extern void eglDestroySync(void);
extern void eglGetConfigAttrib(void);
extern void eglGetConfigs(void);
extern void eglGetCurrentContext(void);
extern void eglGetCurrentDisplay(void);
extern void eglGetCurrentSurface(void);
extern void eglGetDisplay(void);
extern void eglGetError(void);
extern void eglGetPlatformDisplay(void);
extern void eglGetProcAddress(void);
extern void eglGetSyncAttrib(void);
extern void eglInitialize(void);
extern void eglMakeCurrent(void);
extern void eglQueryAPI(void);
extern void eglQueryContext(void);
extern void eglQueryString(void);
extern void eglQuerySurface(void);
extern void eglReleaseTexImage(void);
extern void eglReleaseThread(void);
extern void eglSurfaceAttrib(void);
extern void eglSwapBuffers(void);
extern void eglSwapInterval(void);
extern void eglTerminate(void);
extern void eglWaitClient(void);
extern void eglWaitGL(void);
extern void eglWaitNative(void);
extern void eglWaitSync(void);

DynLibFunction gl_functions[] = {
  { "glActiveShaderProgram", (uintptr_t)&glActiveShaderProgram },
  { "glActiveTexture", (uintptr_t)&glActiveTexture },
  { "glAttachShader", (uintptr_t)&glAttachShader },
  { "glBeginQuery", (uintptr_t)&glBeginQuery },
  { "glBeginTransformFeedback", (uintptr_t)&glBeginTransformFeedback },
  { "glBindAttribLocation", (uintptr_t)&glBindAttribLocation },
  { "glBindBuffer", (uintptr_t)&glBindBuffer },
  { "glBindBufferBase", (uintptr_t)&glBindBufferBase },
  { "glBindBufferRange", (uintptr_t)&glBindBufferRange },
  { "glBindFramebuffer", (uintptr_t)&glBindFramebuffer },
  { "glBindImageTexture", (uintptr_t)&glBindImageTexture },
  { "glBindProgramPipeline", (uintptr_t)&glBindProgramPipeline },
  { "glBindRenderbuffer", (uintptr_t)&glBindRenderbuffer },
  { "glBindSampler", (uintptr_t)&glBindSampler },
  { "glBindTexture", (uintptr_t)&glBindTexture },
  { "glBindTransformFeedback", (uintptr_t)&glBindTransformFeedback },
  { "glBindVertexArray", (uintptr_t)&glBindVertexArray },
  { "glBindVertexBuffer", (uintptr_t)&glBindVertexBuffer },
  { "glBlendBarrier", (uintptr_t)&glBlendBarrier },
  { "glBlendColor", (uintptr_t)&glBlendColor },
  { "glBlendEquation", (uintptr_t)&glBlendEquation },
  { "glBlendEquationSeparate", (uintptr_t)&glBlendEquationSeparate },
  { "glBlendEquationSeparatei", (uintptr_t)&glBlendEquationSeparatei },
  { "glBlendEquationi", (uintptr_t)&glBlendEquationi },
  { "glBlendFunc", (uintptr_t)&glBlendFunc },
  { "glBlendFuncSeparate", (uintptr_t)&glBlendFuncSeparate },
  { "glBlendFuncSeparatei", (uintptr_t)&glBlendFuncSeparatei },
  { "glBlendFunci", (uintptr_t)&glBlendFunci },
  { "glBlitFramebuffer", (uintptr_t)&glBlitFramebuffer },
  { "glBufferData", (uintptr_t)&glBufferData },
  { "glBufferSubData", (uintptr_t)&glBufferSubData },
  { "glCheckFramebufferStatus", (uintptr_t)&glCheckFramebufferStatus },
  { "glClear", (uintptr_t)&glClear },
  { "glClearBufferfi", (uintptr_t)&glClearBufferfi },
  { "glClearBufferfv", (uintptr_t)&glClearBufferfv },
  { "glClearBufferiv", (uintptr_t)&glClearBufferiv },
  { "glClearBufferuiv", (uintptr_t)&glClearBufferuiv },
  { "glClearColor", (uintptr_t)&glClearColor },
  { "glClearDepthf", (uintptr_t)&glClearDepthf },
  { "glClearStencil", (uintptr_t)&glClearStencil },
  { "glClientWaitSync", (uintptr_t)&glClientWaitSync },
  { "glColorMask", (uintptr_t)&glColorMask },
  { "glColorMaski", (uintptr_t)&glColorMaski },
  { "glCompileShader", (uintptr_t)&glCompileShader },
  { "glCompressedTexImage2D", (uintptr_t)&glCompressedTexImage2D },
  { "glCompressedTexImage3D", (uintptr_t)&glCompressedTexImage3D },
  { "glCompressedTexSubImage2D", (uintptr_t)&glCompressedTexSubImage2D },
  { "glCompressedTexSubImage3D", (uintptr_t)&glCompressedTexSubImage3D },
  { "glCopyBufferSubData", (uintptr_t)&glCopyBufferSubData },
  { "glCopyImageSubData", (uintptr_t)&glCopyImageSubData },
  { "glCopyTexImage2D", (uintptr_t)&glCopyTexImage2D },
  { "glCopyTexSubImage2D", (uintptr_t)&glCopyTexSubImage2D },
  { "glCopyTexSubImage3D", (uintptr_t)&glCopyTexSubImage3D },
  { "glCreateProgram", (uintptr_t)&glCreateProgram },
  { "glCreateShader", (uintptr_t)&glCreateShader },
  { "glCreateShaderProgramv", (uintptr_t)&glCreateShaderProgramv },
  { "glCullFace", (uintptr_t)&glCullFace },
  { "glDebugMessageCallback", (uintptr_t)&glDebugMessageCallback },
  { "glDebugMessageControl", (uintptr_t)&glDebugMessageControl },
  { "glDebugMessageInsert", (uintptr_t)&glDebugMessageInsert },
  { "glDeleteBuffers", (uintptr_t)&glDeleteBuffers },
  { "glDeleteFramebuffers", (uintptr_t)&glDeleteFramebuffers },
  { "glDeleteProgram", (uintptr_t)&glDeleteProgram },
  { "glDeleteProgramPipelines", (uintptr_t)&glDeleteProgramPipelines },
  { "glDeleteQueries", (uintptr_t)&glDeleteQueries },
  { "glDeleteRenderbuffers", (uintptr_t)&glDeleteRenderbuffers },
  { "glDeleteSamplers", (uintptr_t)&glDeleteSamplers },
  { "glDeleteShader", (uintptr_t)&glDeleteShader },
  { "glDeleteSync", (uintptr_t)&glDeleteSync },
  { "glDeleteTextures", (uintptr_t)&glDeleteTextures },
  { "glDeleteTransformFeedbacks", (uintptr_t)&glDeleteTransformFeedbacks },
  { "glDeleteVertexArrays", (uintptr_t)&glDeleteVertexArrays },
  { "glDepthFunc", (uintptr_t)&glDepthFunc },
  { "glDepthMask", (uintptr_t)&glDepthMask },
  { "glDepthRangef", (uintptr_t)&glDepthRangef },
  { "glDetachShader", (uintptr_t)&glDetachShader },
  { "glDisable", (uintptr_t)&glDisable },
  { "glDisableVertexAttribArray", (uintptr_t)&glDisableVertexAttribArray },
  { "glDisablei", (uintptr_t)&glDisablei },
  { "glDispatchCompute", (uintptr_t)&glDispatchCompute },
  { "glDispatchComputeIndirect", (uintptr_t)&glDispatchComputeIndirect },
  { "glDrawArrays", (uintptr_t)&glDrawArrays },
  { "glDrawArraysIndirect", (uintptr_t)&glDrawArraysIndirect },
  { "glDrawArraysInstanced", (uintptr_t)&glDrawArraysInstanced },
  { "glDrawBuffers", (uintptr_t)&glDrawBuffers },
  { "glDrawElements", (uintptr_t)&glDrawElements },
  { "glDrawElementsBaseVertex", (uintptr_t)&glDrawElementsBaseVertex },
  { "glDrawElementsIndirect", (uintptr_t)&glDrawElementsIndirect },
  { "glDrawElementsInstanced", (uintptr_t)&glDrawElementsInstanced },
  { "glDrawElementsInstancedBaseVertex", (uintptr_t)&glDrawElementsInstancedBaseVertex },
  { "glDrawRangeElements", (uintptr_t)&glDrawRangeElements },
  { "glDrawRangeElementsBaseVertex", (uintptr_t)&glDrawRangeElementsBaseVertex },
  { "glEnable", (uintptr_t)&glEnable },
  { "glEnableVertexAttribArray", (uintptr_t)&glEnableVertexAttribArray },
  { "glEnablei", (uintptr_t)&glEnablei },
  { "glEndQuery", (uintptr_t)&glEndQuery },
  { "glEndTransformFeedback", (uintptr_t)&glEndTransformFeedback },
  { "glFenceSync", (uintptr_t)&glFenceSync },
  { "glFinish", (uintptr_t)&glFinish },
  { "glFlush", (uintptr_t)&glFlush },
  { "glFlushMappedBufferRange", (uintptr_t)&glFlushMappedBufferRange },
  { "glFramebufferParameteri", (uintptr_t)&glFramebufferParameteri },
  { "glFramebufferRenderbuffer", (uintptr_t)&glFramebufferRenderbuffer },
  { "glFramebufferTexture", (uintptr_t)&glFramebufferTexture },
  { "glFramebufferTexture2D", (uintptr_t)&glFramebufferTexture2D },
  { "glFramebufferTextureLayer", (uintptr_t)&glFramebufferTextureLayer },
  { "glFrontFace", (uintptr_t)&glFrontFace },
  { "glGenBuffers", (uintptr_t)&glGenBuffers },
  { "glGenFramebuffers", (uintptr_t)&glGenFramebuffers },
  { "glGenProgramPipelines", (uintptr_t)&glGenProgramPipelines },
  { "glGenQueries", (uintptr_t)&glGenQueries },
  { "glGenRenderbuffers", (uintptr_t)&glGenRenderbuffers },
  { "glGenSamplers", (uintptr_t)&glGenSamplers },
  { "glGenTextures", (uintptr_t)&glGenTextures },
  { "glGenTransformFeedbacks", (uintptr_t)&glGenTransformFeedbacks },
  { "glGenVertexArrays", (uintptr_t)&glGenVertexArrays },
  { "glGenerateMipmap", (uintptr_t)&glGenerateMipmap },
  { "glGetActiveAttrib", (uintptr_t)&glGetActiveAttrib },
  { "glGetActiveUniform", (uintptr_t)&glGetActiveUniform },
  { "glGetActiveUniformBlockName", (uintptr_t)&glGetActiveUniformBlockName },
  { "glGetActiveUniformBlockiv", (uintptr_t)&glGetActiveUniformBlockiv },
  { "glGetActiveUniformsiv", (uintptr_t)&glGetActiveUniformsiv },
  { "glGetAttachedShaders", (uintptr_t)&glGetAttachedShaders },
  { "glGetAttribLocation", (uintptr_t)&glGetAttribLocation },
  { "glGetBooleani_v", (uintptr_t)&glGetBooleani_v },
  { "glGetBooleanv", (uintptr_t)&glGetBooleanv },
  { "glGetBufferParameteri64v", (uintptr_t)&glGetBufferParameteri64v },
  { "glGetBufferParameteriv", (uintptr_t)&glGetBufferParameteriv },
  { "glGetBufferPointerv", (uintptr_t)&glGetBufferPointerv },
  { "glGetDebugMessageLog", (uintptr_t)&glGetDebugMessageLog },
  { "glGetError", (uintptr_t)&glGetError },
  { "glGetFloatv", (uintptr_t)&glGetFloatv },
  { "glGetFragDataLocation", (uintptr_t)&glGetFragDataLocation },
  { "glGetFramebufferAttachmentParameteriv", (uintptr_t)&glGetFramebufferAttachmentParameteriv },
  { "glGetFramebufferParameteriv", (uintptr_t)&glGetFramebufferParameteriv },
  { "glGetGraphicsResetStatus", (uintptr_t)&glGetGraphicsResetStatus },
  { "glGetInteger64i_v", (uintptr_t)&glGetInteger64i_v },
  { "glGetInteger64v", (uintptr_t)&glGetInteger64v },
  { "glGetIntegeri_v", (uintptr_t)&glGetIntegeri_v },
  { "glGetIntegerv", (uintptr_t)&glGetIntegerv },
  { "glGetInternalformativ", (uintptr_t)&glGetInternalformativ },
  { "glGetMultisamplefv", (uintptr_t)&glGetMultisamplefv },
  { "glGetObjectLabel", (uintptr_t)&glGetObjectLabel },
  { "glGetObjectPtrLabel", (uintptr_t)&glGetObjectPtrLabel },
  { "glGetPointerv", (uintptr_t)&glGetPointerv },
  { "glGetProgramBinary", (uintptr_t)&glGetProgramBinary },
  { "glGetProgramInfoLog", (uintptr_t)&glGetProgramInfoLog },
  { "glGetProgramInterfaceiv", (uintptr_t)&glGetProgramInterfaceiv },
  { "glGetProgramPipelineInfoLog", (uintptr_t)&glGetProgramPipelineInfoLog },
  { "glGetProgramPipelineiv", (uintptr_t)&glGetProgramPipelineiv },
  { "glGetProgramResourceIndex", (uintptr_t)&glGetProgramResourceIndex },
  { "glGetProgramResourceLocation", (uintptr_t)&glGetProgramResourceLocation },
  { "glGetProgramResourceName", (uintptr_t)&glGetProgramResourceName },
  { "glGetProgramResourceiv", (uintptr_t)&glGetProgramResourceiv },
  { "glGetProgramiv", (uintptr_t)&glGetProgramiv },
  { "glGetQueryObjectuiv", (uintptr_t)&glGetQueryObjectuiv },
  { "glGetQueryiv", (uintptr_t)&glGetQueryiv },
  { "glGetRenderbufferParameteriv", (uintptr_t)&glGetRenderbufferParameteriv },
  { "glGetSamplerParameterIiv", (uintptr_t)&glGetSamplerParameterIiv },
  { "glGetSamplerParameterIuiv", (uintptr_t)&glGetSamplerParameterIuiv },
  { "glGetSamplerParameterfv", (uintptr_t)&glGetSamplerParameterfv },
  { "glGetSamplerParameteriv", (uintptr_t)&glGetSamplerParameteriv },
  { "glGetShaderInfoLog", (uintptr_t)&glGetShaderInfoLog },
  { "glGetShaderPrecisionFormat", (uintptr_t)&glGetShaderPrecisionFormat },
  { "glGetShaderSource", (uintptr_t)&glGetShaderSource },
  { "glGetShaderiv", (uintptr_t)&glGetShaderiv },
  { "glGetString", (uintptr_t)&glGetString },
  { "glGetStringi", (uintptr_t)&glGetStringi },
  { "glGetSynciv", (uintptr_t)&glGetSynciv },
  { "glGetTexLevelParameterfv", (uintptr_t)&glGetTexLevelParameterfv },
  { "glGetTexLevelParameteriv", (uintptr_t)&glGetTexLevelParameteriv },
  { "glGetTexParameterIiv", (uintptr_t)&glGetTexParameterIiv },
  { "glGetTexParameterIuiv", (uintptr_t)&glGetTexParameterIuiv },
  { "glGetTexParameterfv", (uintptr_t)&glGetTexParameterfv },
  { "glGetTexParameteriv", (uintptr_t)&glGetTexParameteriv },
  { "glGetTransformFeedbackVarying", (uintptr_t)&glGetTransformFeedbackVarying },
  { "glGetUniformBlockIndex", (uintptr_t)&glGetUniformBlockIndex },
  { "glGetUniformIndices", (uintptr_t)&glGetUniformIndices },
  { "glGetUniformLocation", (uintptr_t)&glGetUniformLocation },
  { "glGetUniformfv", (uintptr_t)&glGetUniformfv },
  { "glGetUniformiv", (uintptr_t)&glGetUniformiv },
  { "glGetUniformuiv", (uintptr_t)&glGetUniformuiv },
  { "glGetVertexAttribIiv", (uintptr_t)&glGetVertexAttribIiv },
  { "glGetVertexAttribIuiv", (uintptr_t)&glGetVertexAttribIuiv },
  { "glGetVertexAttribPointerv", (uintptr_t)&glGetVertexAttribPointerv },
  { "glGetVertexAttribfv", (uintptr_t)&glGetVertexAttribfv },
  { "glGetVertexAttribiv", (uintptr_t)&glGetVertexAttribiv },
  { "glGetnUniformfv", (uintptr_t)&glGetnUniformfv },
  { "glGetnUniformiv", (uintptr_t)&glGetnUniformiv },
  { "glGetnUniformuiv", (uintptr_t)&glGetnUniformuiv },
  { "glHint", (uintptr_t)&glHint },
  { "glInvalidateFramebuffer", (uintptr_t)&glInvalidateFramebuffer },
  { "glInvalidateSubFramebuffer", (uintptr_t)&glInvalidateSubFramebuffer },
  { "glIsBuffer", (uintptr_t)&glIsBuffer },
  { "glIsEnabled", (uintptr_t)&glIsEnabled },
  { "glIsEnabledi", (uintptr_t)&glIsEnabledi },
  { "glIsFramebuffer", (uintptr_t)&glIsFramebuffer },
  { "glIsProgram", (uintptr_t)&glIsProgram },
  { "glIsProgramPipeline", (uintptr_t)&glIsProgramPipeline },
  { "glIsQuery", (uintptr_t)&glIsQuery },
  { "glIsRenderbuffer", (uintptr_t)&glIsRenderbuffer },
  { "glIsSampler", (uintptr_t)&glIsSampler },
  { "glIsShader", (uintptr_t)&glIsShader },
  { "glIsSync", (uintptr_t)&glIsSync },
  { "glIsTexture", (uintptr_t)&glIsTexture },
  { "glIsTransformFeedback", (uintptr_t)&glIsTransformFeedback },
  { "glIsVertexArray", (uintptr_t)&glIsVertexArray },
  { "glLineWidth", (uintptr_t)&glLineWidth },
  { "glLinkProgram", (uintptr_t)&glLinkProgram },
  { "glMapBufferRange", (uintptr_t)&glMapBufferRange },
  { "glMemoryBarrier", (uintptr_t)&glMemoryBarrier },
  { "glMemoryBarrierByRegion", (uintptr_t)&glMemoryBarrierByRegion },
  { "glMinSampleShading", (uintptr_t)&glMinSampleShading },
  { "glObjectLabel", (uintptr_t)&glObjectLabel },
  { "glObjectPtrLabel", (uintptr_t)&glObjectPtrLabel },
  { "glPatchParameteri", (uintptr_t)&glPatchParameteri },
  { "glPauseTransformFeedback", (uintptr_t)&glPauseTransformFeedback },
  { "glPixelStorei", (uintptr_t)&glPixelStorei },
  { "glPolygonOffset", (uintptr_t)&glPolygonOffset },
  { "glPopDebugGroup", (uintptr_t)&glPopDebugGroup },
  { "glPrimitiveBoundingBox", (uintptr_t)&glPrimitiveBoundingBox },
  { "glProgramBinary", (uintptr_t)&glProgramBinary },
  { "glProgramParameteri", (uintptr_t)&glProgramParameteri },
  { "glProgramUniform1f", (uintptr_t)&glProgramUniform1f },
  { "glProgramUniform1fv", (uintptr_t)&glProgramUniform1fv },
  { "glProgramUniform1i", (uintptr_t)&glProgramUniform1i },
  { "glProgramUniform1iv", (uintptr_t)&glProgramUniform1iv },
  { "glProgramUniform1ui", (uintptr_t)&glProgramUniform1ui },
  { "glProgramUniform1uiv", (uintptr_t)&glProgramUniform1uiv },
  { "glProgramUniform2f", (uintptr_t)&glProgramUniform2f },
  { "glProgramUniform2fv", (uintptr_t)&glProgramUniform2fv },
  { "glProgramUniform2i", (uintptr_t)&glProgramUniform2i },
  { "glProgramUniform2iv", (uintptr_t)&glProgramUniform2iv },
  { "glProgramUniform2ui", (uintptr_t)&glProgramUniform2ui },
  { "glProgramUniform2uiv", (uintptr_t)&glProgramUniform2uiv },
  { "glProgramUniform3f", (uintptr_t)&glProgramUniform3f },
  { "glProgramUniform3fv", (uintptr_t)&glProgramUniform3fv },
  { "glProgramUniform3i", (uintptr_t)&glProgramUniform3i },
  { "glProgramUniform3iv", (uintptr_t)&glProgramUniform3iv },
  { "glProgramUniform3ui", (uintptr_t)&glProgramUniform3ui },
  { "glProgramUniform3uiv", (uintptr_t)&glProgramUniform3uiv },
  { "glProgramUniform4f", (uintptr_t)&glProgramUniform4f },
  { "glProgramUniform4fv", (uintptr_t)&glProgramUniform4fv },
  { "glProgramUniform4i", (uintptr_t)&glProgramUniform4i },
  { "glProgramUniform4iv", (uintptr_t)&glProgramUniform4iv },
  { "glProgramUniform4ui", (uintptr_t)&glProgramUniform4ui },
  { "glProgramUniform4uiv", (uintptr_t)&glProgramUniform4uiv },
  { "glProgramUniformMatrix2fv", (uintptr_t)&glProgramUniformMatrix2fv },
  { "glProgramUniformMatrix2x3fv", (uintptr_t)&glProgramUniformMatrix2x3fv },
  { "glProgramUniformMatrix2x4fv", (uintptr_t)&glProgramUniformMatrix2x4fv },
  { "glProgramUniformMatrix3fv", (uintptr_t)&glProgramUniformMatrix3fv },
  { "glProgramUniformMatrix3x2fv", (uintptr_t)&glProgramUniformMatrix3x2fv },
  { "glProgramUniformMatrix3x4fv", (uintptr_t)&glProgramUniformMatrix3x4fv },
  { "glProgramUniformMatrix4fv", (uintptr_t)&glProgramUniformMatrix4fv },
  { "glProgramUniformMatrix4x2fv", (uintptr_t)&glProgramUniformMatrix4x2fv },
  { "glProgramUniformMatrix4x3fv", (uintptr_t)&glProgramUniformMatrix4x3fv },
  { "glPushDebugGroup", (uintptr_t)&glPushDebugGroup },
  { "glReadBuffer", (uintptr_t)&glReadBuffer },
  { "glReadPixels", (uintptr_t)&glReadPixels },
  { "glReadnPixels", (uintptr_t)&glReadnPixels },
  { "glReleaseShaderCompiler", (uintptr_t)&glReleaseShaderCompiler },
  { "glRenderbufferStorage", (uintptr_t)&glRenderbufferStorage },
  { "glRenderbufferStorageMultisample", (uintptr_t)&glRenderbufferStorageMultisample },
  { "glResumeTransformFeedback", (uintptr_t)&glResumeTransformFeedback },
  { "glSampleCoverage", (uintptr_t)&glSampleCoverage },
  { "glSampleMaski", (uintptr_t)&glSampleMaski },
  { "glSamplerParameterIiv", (uintptr_t)&glSamplerParameterIiv },
  { "glSamplerParameterIuiv", (uintptr_t)&glSamplerParameterIuiv },
  { "glSamplerParameterf", (uintptr_t)&glSamplerParameterf },
  { "glSamplerParameterfv", (uintptr_t)&glSamplerParameterfv },
  { "glSamplerParameteri", (uintptr_t)&glSamplerParameteri },
  { "glSamplerParameteriv", (uintptr_t)&glSamplerParameteriv },
  { "glScissor", (uintptr_t)&glScissor },
  { "glShaderBinary", (uintptr_t)&glShaderBinary },
  { "glShaderSource", (uintptr_t)&glShaderSource },
  { "glStencilFunc", (uintptr_t)&glStencilFunc },
  { "glStencilFuncSeparate", (uintptr_t)&glStencilFuncSeparate },
  { "glStencilMask", (uintptr_t)&glStencilMask },
  { "glStencilMaskSeparate", (uintptr_t)&glStencilMaskSeparate },
  { "glStencilOp", (uintptr_t)&glStencilOp },
  { "glStencilOpSeparate", (uintptr_t)&glStencilOpSeparate },
  { "glTexBuffer", (uintptr_t)&glTexBuffer },
  { "glTexBufferRange", (uintptr_t)&glTexBufferRange },
  { "glTexImage2D", (uintptr_t)&glTexImage2D },
  { "glTexImage3D", (uintptr_t)&glTexImage3D },
  { "glTexParameterIiv", (uintptr_t)&glTexParameterIiv },
  { "glTexParameterIuiv", (uintptr_t)&glTexParameterIuiv },
  { "glTexParameterf", (uintptr_t)&glTexParameterf },
  { "glTexParameterfv", (uintptr_t)&glTexParameterfv },
  { "glTexParameteri", (uintptr_t)&glTexParameteri },
  { "glTexParameteriv", (uintptr_t)&glTexParameteriv },
  { "glTexStorage2D", (uintptr_t)&glTexStorage2D },
  { "glTexStorage2DMultisample", (uintptr_t)&glTexStorage2DMultisample },
  { "glTexStorage3D", (uintptr_t)&glTexStorage3D },
  { "glTexStorage3DMultisample", (uintptr_t)&glTexStorage3DMultisample },
  { "glTexSubImage2D", (uintptr_t)&glTexSubImage2D },
  { "glTexSubImage3D", (uintptr_t)&glTexSubImage3D },
  { "glTransformFeedbackVaryings", (uintptr_t)&glTransformFeedbackVaryings },
  { "glUniform1f", (uintptr_t)&glUniform1f },
  { "glUniform1fv", (uintptr_t)&glUniform1fv },
  { "glUniform1i", (uintptr_t)&glUniform1i },
  { "glUniform1iv", (uintptr_t)&glUniform1iv },
  { "glUniform1ui", (uintptr_t)&glUniform1ui },
  { "glUniform1uiv", (uintptr_t)&glUniform1uiv },
  { "glUniform2f", (uintptr_t)&glUniform2f },
  { "glUniform2fv", (uintptr_t)&glUniform2fv },
  { "glUniform2i", (uintptr_t)&glUniform2i },
  { "glUniform2iv", (uintptr_t)&glUniform2iv },
  { "glUniform2ui", (uintptr_t)&glUniform2ui },
  { "glUniform2uiv", (uintptr_t)&glUniform2uiv },
  { "glUniform3f", (uintptr_t)&glUniform3f },
  { "glUniform3fv", (uintptr_t)&glUniform3fv },
  { "glUniform3i", (uintptr_t)&glUniform3i },
  { "glUniform3iv", (uintptr_t)&glUniform3iv },
  { "glUniform3ui", (uintptr_t)&glUniform3ui },
  { "glUniform3uiv", (uintptr_t)&glUniform3uiv },
  { "glUniform4f", (uintptr_t)&glUniform4f },
  { "glUniform4fv", (uintptr_t)&glUniform4fv },
  { "glUniform4i", (uintptr_t)&glUniform4i },
  { "glUniform4iv", (uintptr_t)&glUniform4iv },
  { "glUniform4ui", (uintptr_t)&glUniform4ui },
  { "glUniform4uiv", (uintptr_t)&glUniform4uiv },
  { "glUniformBlockBinding", (uintptr_t)&glUniformBlockBinding },
  { "glUniformMatrix2fv", (uintptr_t)&glUniformMatrix2fv },
  { "glUniformMatrix2x3fv", (uintptr_t)&glUniformMatrix2x3fv },
  { "glUniformMatrix2x4fv", (uintptr_t)&glUniformMatrix2x4fv },
  { "glUniformMatrix3fv", (uintptr_t)&glUniformMatrix3fv },
  { "glUniformMatrix3x2fv", (uintptr_t)&glUniformMatrix3x2fv },
  { "glUniformMatrix3x4fv", (uintptr_t)&glUniformMatrix3x4fv },
  { "glUniformMatrix4fv", (uintptr_t)&glUniformMatrix4fv },
  { "glUniformMatrix4x2fv", (uintptr_t)&glUniformMatrix4x2fv },
  { "glUniformMatrix4x3fv", (uintptr_t)&glUniformMatrix4x3fv },
  { "glUnmapBuffer", (uintptr_t)&glUnmapBuffer },
  { "glUseProgram", (uintptr_t)&glUseProgram },
  { "glUseProgramStages", (uintptr_t)&glUseProgramStages },
  { "glValidateProgram", (uintptr_t)&glValidateProgram },
  { "glValidateProgramPipeline", (uintptr_t)&glValidateProgramPipeline },
  { "glVertexAttrib1f", (uintptr_t)&glVertexAttrib1f },
  { "glVertexAttrib1fv", (uintptr_t)&glVertexAttrib1fv },
  { "glVertexAttrib2f", (uintptr_t)&glVertexAttrib2f },
  { "glVertexAttrib2fv", (uintptr_t)&glVertexAttrib2fv },
  { "glVertexAttrib3f", (uintptr_t)&glVertexAttrib3f },
  { "glVertexAttrib3fv", (uintptr_t)&glVertexAttrib3fv },
  { "glVertexAttrib4f", (uintptr_t)&glVertexAttrib4f },
  { "glVertexAttrib4fv", (uintptr_t)&glVertexAttrib4fv },
  { "glVertexAttribBinding", (uintptr_t)&glVertexAttribBinding },
  { "glVertexAttribDivisor", (uintptr_t)&glVertexAttribDivisor },
  { "glVertexAttribFormat", (uintptr_t)&glVertexAttribFormat },
  { "glVertexAttribI4i", (uintptr_t)&glVertexAttribI4i },
  { "glVertexAttribI4iv", (uintptr_t)&glVertexAttribI4iv },
  { "glVertexAttribI4ui", (uintptr_t)&glVertexAttribI4ui },
  { "glVertexAttribI4uiv", (uintptr_t)&glVertexAttribI4uiv },
  { "glVertexAttribIFormat", (uintptr_t)&glVertexAttribIFormat },
  { "glVertexAttribIPointer", (uintptr_t)&glVertexAttribIPointer },
  { "glVertexAttribPointer", (uintptr_t)&glVertexAttribPointer },
  { "glVertexBindingDivisor", (uintptr_t)&glVertexBindingDivisor },
  { "glViewport", (uintptr_t)&glViewport },
  { "glWaitSync", (uintptr_t)&glWaitSync },
  { "eglBindAPI", (uintptr_t)&eglBindAPI },
  { "eglBindTexImage", (uintptr_t)&eglBindTexImage },
  { "eglChooseConfig", (uintptr_t)&eglChooseConfig },
  { "eglClientWaitSync", (uintptr_t)&eglClientWaitSync },
  { "eglCopyBuffers", (uintptr_t)&eglCopyBuffers },
  { "eglCreateContext", (uintptr_t)&eglCreateContext },
  { "eglCreateImage", (uintptr_t)&eglCreateImage },
  { "eglCreatePbufferFromClientBuffer", (uintptr_t)&eglCreatePbufferFromClientBuffer },
  { "eglCreatePbufferSurface", (uintptr_t)&eglCreatePbufferSurface },
  { "eglCreatePixmapSurface", (uintptr_t)&eglCreatePixmapSurface },
  { "eglCreatePlatformPixmapSurface", (uintptr_t)&eglCreatePlatformPixmapSurface },
  { "eglCreatePlatformWindowSurface", (uintptr_t)&eglCreatePlatformWindowSurface },
  { "eglCreateSync", (uintptr_t)&eglCreateSync },
  { "eglCreateWindowSurface", (uintptr_t)&eglCreateWindowSurface },
  { "eglDestroyContext", (uintptr_t)&eglDestroyContext },
  { "eglDestroyImage", (uintptr_t)&eglDestroyImage },
  { "eglDestroySurface", (uintptr_t)&eglDestroySurface },
  { "eglDestroySync", (uintptr_t)&eglDestroySync },
  { "eglGetConfigAttrib", (uintptr_t)&eglGetConfigAttrib },
  { "eglGetConfigs", (uintptr_t)&eglGetConfigs },
  { "eglGetCurrentContext", (uintptr_t)&eglGetCurrentContext },
  { "eglGetCurrentDisplay", (uintptr_t)&eglGetCurrentDisplay },
  { "eglGetCurrentSurface", (uintptr_t)&eglGetCurrentSurface },
  { "eglGetDisplay", (uintptr_t)&eglGetDisplay },
  { "eglGetError", (uintptr_t)&eglGetError },
  { "eglGetPlatformDisplay", (uintptr_t)&eglGetPlatformDisplay },
  { "eglGetProcAddress", (uintptr_t)&eglGetProcAddress },
  { "eglGetSyncAttrib", (uintptr_t)&eglGetSyncAttrib },
  { "eglInitialize", (uintptr_t)&eglInitialize },
  { "eglMakeCurrent", (uintptr_t)&eglMakeCurrent },
  { "eglQueryAPI", (uintptr_t)&eglQueryAPI },
  { "eglQueryContext", (uintptr_t)&eglQueryContext },
  { "eglQueryString", (uintptr_t)&eglQueryString },
  { "eglQuerySurface", (uintptr_t)&eglQuerySurface },
  { "eglReleaseTexImage", (uintptr_t)&eglReleaseTexImage },
  { "eglReleaseThread", (uintptr_t)&eglReleaseThread },
  { "eglSurfaceAttrib", (uintptr_t)&eglSurfaceAttrib },
  { "eglSwapBuffers", (uintptr_t)&eglSwapBuffers },
  { "eglSwapInterval", (uintptr_t)&eglSwapInterval },
  { "eglTerminate", (uintptr_t)&eglTerminate },
  { "eglWaitClient", (uintptr_t)&eglWaitClient },
  { "eglWaitGL", (uintptr_t)&eglWaitGL },
  { "eglWaitNative", (uintptr_t)&eglWaitNative },
  { "eglWaitSync", (uintptr_t)&eglWaitSync },
};

const size_t gl_numfunctions = sizeof(gl_functions) / sizeof(*gl_functions);

#else // MVL_DEKO3D: dkgl's GL and EGL, through the linker's fallback (main.c:
      // eglGetProcAddress -> dkgl_get_proc)

#include "so_util.h"

DynLibFunction gl_functions[] = { { "", 0 } };
const size_t gl_numfunctions = 0;

#endif
