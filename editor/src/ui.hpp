#pragma once
// Minimal 2D overlay for the editor: filled rectangles and 5x7 bitmap text in window
// pixel coordinates, batched into one vertex buffer per frame.
#include "gl.hpp"
#include "font5x7.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace hgw {

class Ui {
public:
    explicit Ui(Gl& gl):gl_(gl) {
        const char* vs=R"(#version 330 core
layout(location=0) in vec2 position; layout(location=1) in vec4 colour;
uniform vec2 screen; out vec4 v_colour;
void main(){gl_Position=vec4(position.x/screen.x*2.0-1.0,1.0-position.y/screen.y*2.0,0.0,1.0);v_colour=colour;})";
        const char* fs=R"(#version 330 core
in vec4 v_colour; out vec4 result; void main(){result=v_colour;})";
        program_=gl.CreateProgram();
        const auto add=[&](GLenum kind,const char* src){
            const GLuint s=gl.CreateShader(kind);gl.ShaderSource(s,1,&src,nullptr);gl.CompileShader(s);gl.AttachShader(program_,s);};
        add(vertex_shader,vs);add(fragment_shader,fs);gl.LinkProgram(program_);
        screen_=gl.GetUniformLocation(program_,"screen");
        gl.GenVertexArrays(1,&vao_);gl.BindVertexArray(vao_);
        gl.GenBuffers(1,&vbo_);gl.BindBuffer(array_buffer,vbo_);
        gl.EnableVertexAttribArray(0);gl.VertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(V),nullptr);
        gl.EnableVertexAttribArray(1);gl.VertexAttribPointer(1,4,GL_UNSIGNED_BYTE,GL_TRUE,sizeof(V),reinterpret_cast<void*>(8));
    }
    static constexpr int glyph_w=5,glyph_h=7;
    int scale=2;
    int char_width() const {return (glyph_w+1)*scale;}
    int line_height() const {return (glyph_h+3)*scale;}

    void rect(float x,float y,float w,float h,std::uint32_t rgba) {
        const V a{x,y,rgba},b{x+w,y,rgba},c{x+w,y+h,rgba},d{x,y+h,rgba};
        vertices_.insert(vertices_.end(),{a,b,c,a,c,d});
    }
    // Returns the text width in pixels; text past `max_width` (if > 0) is cut.
    float text(float x,float y,const std::string& s,std::uint32_t rgba,float max_width=0) {
        float cx=x;
        for(const char raw:s) {
            if(max_width>0&&cx+char_width()>x+max_width)break;
            const int code=static_cast<unsigned char>(raw);
            const auto& glyph=font5x7[(code>=32&&code<127)?code-32:'?'-32];
            for(int row=0;row<glyph_h;++row)for(int col=0;col<glyph_w;++col)
                if(glyph[row]&(1<<(glyph_w-1-col)))rect(cx+col*scale,y+row*scale,float(scale),float(scale),rgba);
            cx+=char_width();
        }
        return cx-x;
    }
    void draw(int width,int height) {
        if(vertices_.empty())return;
        glDisable(GL_DEPTH_TEST);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
        gl_.UseProgram(program_);gl_.Uniform2f(screen_,float(width),float(height));
        gl_.BindVertexArray(vao_);gl_.BindBuffer(array_buffer,vbo_);
        gl_.BufferData(array_buffer,std::ptrdiff_t(vertices_.size()*sizeof(V)),vertices_.data(),0x88e8 /*GL_DYNAMIC_DRAW*/);
        glDrawArrays(GL_TRIANGLES,0,GLsizei(vertices_.size()));
        vertices_.clear();
        glEnable(GL_DEPTH_TEST);glDisable(GL_BLEND);
    }
private:
    struct V {float x,y;std::uint32_t rgba;};   // rgba stored as bytes r,g,b,a (little-endian 0xAABBGGRR)
    Gl& gl_;
    GLuint program_=0,vao_=0,vbo_=0;
    GLint screen_=-1;
    std::vector<V> vertices_;
};

// Colours as 0xAABBGGRR for the vertex byte order.
constexpr std::uint32_t ui_rgba(std::uint8_t r,std::uint8_t g,std::uint8_t b,std::uint8_t a=255) {
    return std::uint32_t(r)|std::uint32_t(g)<<8|std::uint32_t(b)<<16|std::uint32_t(a)<<24;
}
}
