// Chargeur minimal des points d'entrée OpenGL 3.3 dont le moteur a besoin.
//
// Windows n'expose que le noyau OpenGL 1.1 dans opengl32.dll ; tout le reste
// se récupère à l'exécution via wglGetProcAddress. Plutôt que d'embarquer GLEW
// ou GLAD, on déclare ici la petite poignée de fonctions réellement utilisées.
#pragma once

#include <windows.h>

#include <GL/gl.h>

#include <cstddef>
#include <cstdint>

// Constantes absentes de l'en-tête OpenGL 1.1 fourni par MinGW.
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_STREAM_DRAW 0x88E0
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_TEXTURE0 0x84C0
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_MIRRORED_REPEAT 0x8370
#define GL_MULTISAMPLE 0x809D
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_DEPTH24_STENCIL8 0x88F0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif

#ifndef GL_GEOMETRY_SHADER
#define GL_GEOMETRY_SHADER 0x8DD9
#endif
#ifndef GL_LINES_ADJACENCY
#define GL_LINES_ADJACENCY 0x000A
#endif

using GLchar = char;
using GLsizeiptr = std::ptrdiff_t;
using GLintptr = std::ptrdiff_t;

namespace sfc::gfx {

// X(nom, type de retour, paramètres)
#define SFC_GL_FUNCTIONS(X)                                                       \
    X(glCreateShader,            GLuint,  (GLenum))                               \
    X(glShaderSource,            void,    (GLuint, GLsizei, const GLchar* const*, const GLint*)) \
    X(glCompileShader,           void,    (GLuint))                               \
    X(glGetShaderiv,             void,    (GLuint, GLenum, GLint*))               \
    X(glGetShaderInfoLog,        void,    (GLuint, GLsizei, GLsizei*, GLchar*))   \
    X(glDeleteShader,            void,    (GLuint))                               \
    X(glCreateProgram,           GLuint,  (void))                                 \
    X(glAttachShader,            void,    (GLuint, GLuint))                       \
    X(glLinkProgram,             void,    (GLuint))                               \
    X(glGetProgramiv,            void,    (GLuint, GLenum, GLint*))               \
    X(glGetProgramInfoLog,       void,    (GLuint, GLsizei, GLsizei*, GLchar*))   \
    X(glUseProgram,              void,    (GLuint))                               \
    X(glDeleteProgram,           void,    (GLuint))                               \
    X(glGetUniformLocation,      GLint,   (GLuint, const GLchar*))                \
    X(glUniform1i,               void,    (GLint, GLint))                         \
    X(glUniform1f,               void,    (GLint, GLfloat))                       \
    X(glUniform3f,               void,    (GLint, GLfloat, GLfloat, GLfloat))     \
    X(glUniform4f,               void,    (GLint, GLfloat, GLfloat, GLfloat, GLfloat))     X(glUniform1fv,              void,    (GLint, GLsizei, const GLfloat*))       \
    X(glUniformMatrix4fv,        void,    (GLint, GLsizei, GLboolean, const GLfloat*)) \
    X(glGenVertexArrays,         void,    (GLsizei, GLuint*))                     \
    X(glBindVertexArray,         void,    (GLuint))                               \
    X(glDeleteVertexArrays,      void,    (GLsizei, const GLuint*))               \
    X(glGenBuffers,              void,    (GLsizei, GLuint*))                     \
    X(glBindBuffer,              void,    (GLenum, GLuint))                       \
    X(glBufferData,              void,    (GLenum, GLsizeiptr, const void*, GLenum)) \
    X(glDeleteBuffers,           void,    (GLsizei, const GLuint*))               \
    X(glVertexAttribPointer,     void,    (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)) \
    X(glEnableVertexAttribArray, void,    (GLuint))                               \
    X(glActiveTexture,           void,    (GLenum))                               \
    X(glGenerateMipmap,          void,    (GLenum))                                 X(glGenFramebuffers,         void,    (GLsizei, GLuint*))                         X(glBindFramebuffer,         void,    (GLenum, GLuint))                           X(glDeleteFramebuffers,      void,    (GLsizei, const GLuint*))                   X(glFramebufferTexture2D,    void,    (GLenum, GLenum, GLenum, GLuint, GLint))     X(glGenRenderbuffers,        void,    (GLsizei, GLuint*))                         X(glBindRenderbuffer,        void,    (GLenum, GLuint))                           X(glDeleteRenderbuffers,     void,    (GLsizei, const GLuint*))                   X(glRenderbufferStorage,     void,    (GLenum, GLenum, GLsizei, GLsizei))         X(glFramebufferRenderbuffer, void,    (GLenum, GLenum, GLenum, GLuint))           X(glCheckFramebufferStatus,  GLenum,  (GLenum))                                   X(glBlitFramebuffer,         void,    (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum))

#define SFC_DECLARE(name, ret, params) \
    using PFN_##name = ret(APIENTRY*) params; \
    extern PFN_##name name;
SFC_GL_FUNCTIONS(SFC_DECLARE)
#undef SFC_DECLARE

// Résout tous les points d'entrée ; renvoie false si l'un d'eux manque.
bool loadGlFunctions(const char** missing);

}  // namespace sfc::gfx
