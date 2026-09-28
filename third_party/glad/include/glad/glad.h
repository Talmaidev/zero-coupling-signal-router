#ifndef ZCSR_THIRD_PARTY_GLAD_H
#define ZCSR_THIRD_PARTY_GLAD_H
/* Minimal vendored OpenGL loader surface for zcsr glrender.
 *
 * Self-contained on purpose: the platform base GL header (GL/gl.h on Windows/Linux, OpenGL/gl3.h
 * on macOS) supplies the base types and the GL 1.1 entry points the renderer links directly
 * (textures, glDrawArrays, viewport/blend). The small set of GL 2.0/3.0/3.3 entry points actually
 * used here is declared as function-pointer typedefs and resolved at runtime by gladLoadGLLoader.
 * This deliberately avoids depending on a system glext.h/wglext.h — the Windows SDK ships neither.
 *
 * Generated-style single header/source boundary: modules outside glrender must not include this.
 */

#if defined(__APPLE__)
/* macOS OpenGL framework exports core-profile declarations directly (incl. the PFNGL* typedefs). */
#  include <OpenGL/gl3.h>
#  ifndef GLAPIENTRY
#    define GLAPIENTRY
#  endif
#elif defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>   /* Windows GL/gl.h needs WINGDIAPI/APIENTRY from wingdi.h defined first */
#  include <GL/gl.h>
#  ifndef GLAPIENTRY
#    define GLAPIENTRY APIENTRY
#  endif
#else
#  include <GL/gl.h>
#  ifndef GLAPIENTRY
#    define GLAPIENTRY
#  endif
#endif

#if !defined(__APPLE__)
/* GL 1.5 buffer-object pointer types — not present in the Windows GL 1.1 header (which has no
 * glext.h); guarded so a system glext.h/gl3.h definition wins where one exists. */
#include <stddef.h>
#ifndef GLsizeiptr
typedef ptrdiff_t GLsizeiptr;
#endif
#ifndef GLintptr
typedef ptrdiff_t GLintptr;
#endif

/* The handful of GL 1.2/2.0/3.0 constants this renderer uses. All guarded so a platform header
 * that already defines them takes precedence. Values are the fixed Khronos enum numbers. */
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif

/* Function-pointer typedefs for the GL 2.0+ subset used below (Khronos signatures). */
typedef void (GLAPIENTRY *PFNGLGENVERTEXARRAYSPROC)(GLsizei n, GLuint* arrays);
typedef void (GLAPIENTRY *PFNGLBINDVERTEXARRAYPROC)(GLuint array);
typedef void (GLAPIENTRY *PFNGLDELETEVERTEXARRAYSPROC)(GLsizei n, const GLuint* arrays);
typedef void (GLAPIENTRY *PFNGLGENBUFFERSPROC)(GLsizei n, GLuint* buffers);
typedef void (GLAPIENTRY *PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
typedef void (GLAPIENTRY *PFNGLBUFFERDATAPROC)(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
typedef void (GLAPIENTRY *PFNGLBUFFERSUBDATAPROC)(GLenum target, GLintptr offset, GLsizeiptr size, const void* data);
typedef void (GLAPIENTRY *PFNGLDELETEBUFFERSPROC)(GLsizei n, const GLuint* buffers);
typedef GLuint (GLAPIENTRY *PFNGLCREATESHADERPROC)(GLenum type);
typedef void (GLAPIENTRY *PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const char* const* string, const GLint* length);
typedef void (GLAPIENTRY *PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void (GLAPIENTRY *PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint* params);
typedef void (GLAPIENTRY *PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei bufSize, GLsizei* length, char* infoLog);
typedef void (GLAPIENTRY *PFNGLDELETESHADERPROC)(GLuint shader);
typedef GLuint (GLAPIENTRY *PFNGLCREATEPROGRAMPROC)(void);
typedef void (GLAPIENTRY *PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void (GLAPIENTRY *PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void (GLAPIENTRY *PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint* params);
typedef void (GLAPIENTRY *PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei bufSize, GLsizei* length, char* infoLog);
typedef void (GLAPIENTRY *PFNGLDELETEPROGRAMPROC)(GLuint program);
typedef void (GLAPIENTRY *PFNGLUSEPROGRAMPROC)(GLuint program);
typedef GLint (GLAPIENTRY *PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const char* name);
typedef void (GLAPIENTRY *PFNGLUNIFORM1IPROC)(GLint location, GLint v0);
typedef void (GLAPIENTRY *PFNGLUNIFORM2FPROC)(GLint location, GLfloat v0, GLfloat v1);
typedef void (GLAPIENTRY *PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint index);
typedef void (GLAPIENTRY *PFNGLVERTEXATTRIBPOINTERPROC)(GLuint index, GLint size, GLenum type,
                                                        GLboolean normalized, GLsizei stride, const void* pointer);
typedef void (GLAPIENTRY *PFNGLACTIVETEXTUREPROC)(GLenum texture); /* GL 1.3: not exported by opengl32 */
typedef void (GLAPIENTRY *PFNGLGENFRAMEBUFFERSPROC)(GLsizei n, GLuint* framebuffers);
typedef void (GLAPIENTRY *PFNGLBINDFRAMEBUFFERPROC)(GLenum target, GLuint framebuffer);
typedef void (GLAPIENTRY *PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum target, GLenum attachment,
                                                        GLenum textarget, GLuint texture, GLint level);
typedef GLenum (GLAPIENTRY *PFNGLCHECKFRAMEBUFFERSTATUSPROC)(GLenum target);
typedef void (GLAPIENTRY *PFNGLDELETEFRAMEBUFFERSPROC)(GLsizei n, const GLuint* framebuffers);
#endif /* !__APPLE__ */

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*GLADgenericproc)(void);
typedef GLADgenericproc (*GLADloadproc)(const char* name);
int gladLoadGLLoader(GLADloadproc load);

extern PFNGLGENVERTEXARRAYSPROC glad_glGenVertexArrays;
extern PFNGLBINDVERTEXARRAYPROC glad_glBindVertexArray;
extern PFNGLDELETEVERTEXARRAYSPROC glad_glDeleteVertexArrays;
extern PFNGLGENBUFFERSPROC glad_glGenBuffers;
extern PFNGLBINDBUFFERPROC glad_glBindBuffer;
extern PFNGLBUFFERDATAPROC glad_glBufferData;
extern PFNGLBUFFERSUBDATAPROC glad_glBufferSubData;
extern PFNGLDELETEBUFFERSPROC glad_glDeleteBuffers;
extern PFNGLCREATESHADERPROC glad_glCreateShader;
extern PFNGLSHADERSOURCEPROC glad_glShaderSource;
extern PFNGLCOMPILESHADERPROC glad_glCompileShader;
extern PFNGLGETSHADERIVPROC glad_glGetShaderiv;
extern PFNGLGETSHADERINFOLOGPROC glad_glGetShaderInfoLog;
extern PFNGLDELETESHADERPROC glad_glDeleteShader;
extern PFNGLCREATEPROGRAMPROC glad_glCreateProgram;
extern PFNGLATTACHSHADERPROC glad_glAttachShader;
extern PFNGLLINKPROGRAMPROC glad_glLinkProgram;
extern PFNGLGETPROGRAMIVPROC glad_glGetProgramiv;
extern PFNGLGETPROGRAMINFOLOGPROC glad_glGetProgramInfoLog;
extern PFNGLDELETEPROGRAMPROC glad_glDeleteProgram;
extern PFNGLUSEPROGRAMPROC glad_glUseProgram;
extern PFNGLGETUNIFORMLOCATIONPROC glad_glGetUniformLocation;
extern PFNGLUNIFORM1IPROC glad_glUniform1i;
extern PFNGLUNIFORM2FPROC glad_glUniform2f;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC glad_glEnableVertexAttribArray;
extern PFNGLVERTEXATTRIBPOINTERPROC glad_glVertexAttribPointer;
extern PFNGLACTIVETEXTUREPROC glad_glActiveTexture;
extern PFNGLGENFRAMEBUFFERSPROC glad_glGenFramebuffers;
extern PFNGLBINDFRAMEBUFFERPROC glad_glBindFramebuffer;
extern PFNGLFRAMEBUFFERTEXTURE2DPROC glad_glFramebufferTexture2D;
extern PFNGLCHECKFRAMEBUFFERSTATUSPROC glad_glCheckFramebufferStatus;
extern PFNGLDELETEFRAMEBUFFERSPROC glad_glDeleteFramebuffers;

#ifdef __cplusplus
}
#endif

/* Redirect the GL 2.0+ names to the loaded function pointers. GL 1.1 names (glGenTextures,
 * glDrawArrays, glViewport, ...) are NOT redirected and resolve directly against the GL lib. */
#define glGenVertexArrays glad_glGenVertexArrays
#define glBindVertexArray glad_glBindVertexArray
#define glDeleteVertexArrays glad_glDeleteVertexArrays
#define glGenBuffers glad_glGenBuffers
#define glBindBuffer glad_glBindBuffer
#define glBufferData glad_glBufferData
#define glBufferSubData glad_glBufferSubData
#define glDeleteBuffers glad_glDeleteBuffers
#define glCreateShader glad_glCreateShader
#define glShaderSource glad_glShaderSource
#define glCompileShader glad_glCompileShader
#define glGetShaderiv glad_glGetShaderiv
#define glGetShaderInfoLog glad_glGetShaderInfoLog
#define glDeleteShader glad_glDeleteShader
#define glCreateProgram glad_glCreateProgram
#define glAttachShader glad_glAttachShader
#define glLinkProgram glad_glLinkProgram
#define glGetProgramiv glad_glGetProgramiv
#define glGetProgramInfoLog glad_glGetProgramInfoLog
#define glDeleteProgram glad_glDeleteProgram
#define glUseProgram glad_glUseProgram
#define glGetUniformLocation glad_glGetUniformLocation
#define glUniform1i glad_glUniform1i
#define glUniform2f glad_glUniform2f
#define glEnableVertexAttribArray glad_glEnableVertexAttribArray
#define glVertexAttribPointer glad_glVertexAttribPointer
#define glActiveTexture glad_glActiveTexture
#define glGenFramebuffers glad_glGenFramebuffers
#define glBindFramebuffer glad_glBindFramebuffer
#define glFramebufferTexture2D glad_glFramebufferTexture2D
#define glCheckFramebufferStatus glad_glCheckFramebufferStatus
#define glDeleteFramebuffers glad_glDeleteFramebuffers

#endif /* ZCSR_THIRD_PARTY_GLAD_H */
