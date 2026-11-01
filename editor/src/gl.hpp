#pragma once
// Minimal OpenGL 3.3 core loader for the standalone editor (no game code shared).
#define GLFW_INCLUDE_NONE
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include <GLFW/glfw3.h>
#include <GL/gl.h>
#include <cstddef>
#include <stdexcept>
#include <string>

#ifndef APIENTRY
#define APIENTRY
#endif

namespace hgw {
using GLchar_=char;
struct Gl {
    template<class T> static T proc(const char* name) {
        const auto p=glfwGetProcAddress(name);
        if(!p)throw std::runtime_error(std::string("missing OpenGL entry point ")+name);
        return reinterpret_cast<T>(p);
    }
#define HGW_GL(ret,name,args) ret(APIENTRY* name)args=proc<ret(APIENTRY*)args>("gl" #name);
    HGW_GL(GLuint,CreateShader,(GLenum))
    HGW_GL(void,ShaderSource,(GLuint,GLsizei,const GLchar_* const*,const GLint*))
    HGW_GL(void,CompileShader,(GLuint))
    HGW_GL(void,GetShaderiv,(GLuint,GLenum,GLint*))
    HGW_GL(void,GetShaderInfoLog,(GLuint,GLsizei,GLsizei*,GLchar_*))
    HGW_GL(GLuint,CreateProgram,())
    HGW_GL(void,AttachShader,(GLuint,GLuint))
    HGW_GL(void,LinkProgram,(GLuint))
    HGW_GL(void,GetProgramiv,(GLuint,GLenum,GLint*))
    HGW_GL(void,GetProgramInfoLog,(GLuint,GLsizei,GLsizei*,GLchar_*))
    HGW_GL(void,UseProgram,(GLuint))
    HGW_GL(GLint,GetUniformLocation,(GLuint,const GLchar_*))
    HGW_GL(void,UniformMatrix4fv,(GLint,GLsizei,GLboolean,const GLfloat*))
    HGW_GL(void,Uniform1i,(GLint,GLint))
    HGW_GL(void,Uniform1f,(GLint,GLfloat))
    HGW_GL(void,Uniform2f,(GLint,GLfloat,GLfloat))
    HGW_GL(void,GenVertexArrays,(GLsizei,GLuint*))
    HGW_GL(void,BindVertexArray,(GLuint))
    HGW_GL(void,DeleteVertexArrays,(GLsizei,const GLuint*))
    HGW_GL(void,GenBuffers,(GLsizei,GLuint*))
    HGW_GL(void,BindBuffer,(GLenum,GLuint))
    HGW_GL(void,BufferData,(GLenum,std::ptrdiff_t,const void*,GLenum))
    HGW_GL(void,DeleteBuffers,(GLsizei,const GLuint*))
    HGW_GL(void,EnableVertexAttribArray,(GLuint))
    HGW_GL(void,VertexAttribPointer,(GLuint,GLint,GLenum,GLboolean,GLsizei,const void*))
    HGW_GL(void,ActiveTexture,(GLenum))
    HGW_GL(void,GenerateMipmap,(GLenum))
#undef HGW_GL
};
constexpr GLenum array_buffer=0x8892,static_draw=0x88e4,vertex_shader=0x8b31,fragment_shader=0x8b30,
    compile_status=0x8b81,link_status=0x8b82,texture0=0x84c0,clamp_to_edge=0x812f;
}
