#include "opengl_support.hpp"
#include "hg/native_host.hpp"
#include "hg/keyboard_input.hpp"
#include "hg/controls_panel.hpp"
#include "hg/host_pacing.hpp"
#include "gl_gs.hpp"
#include "hg/gs_triangle_acceleration.hpp"
#include <atomic>
#include <mutex>
#include <thread>
#include <sstream>

namespace {
struct Frame {unsigned width=0,height=0;std::vector<std::uint32_t> pixels;};
struct Session {
    std::atomic<bool> closing{false},done{false},limit_30{false};
    std::mutex mutex;
    Frame pending;
    hg::HostInput input;
    std::uint64_t input_version=0,read_version=0,produced=0;
    std::string status;
    int result=0;
    bool refresh_requested=true; // UI-thread only; repaint even without a new game frame.
    std::atomic<unsigned> checkpoint_requests{0}; // F5 presses not yet taken by the game thread.
    std::string notice; // Guarded by mutex; shown in the title until notice_until.
    std::chrono::steady_clock::time_point notice_until{};
};
hg::HostInput read_input(GLFWwindow* window) {
    const bool focused=glfwGetWindowAttrib(window,GLFW_FOCUSED)!=0;
    hg::KeyboardKeys keys{};
    if(focused) {
        // Matches KeyboardKey order. Physical keys: the D-pad slots (up/right/down/left)
        // are I/L/K/J and the right-stick slots (left/right/up/down) are the arrow keys
        // (user request 2026-10-16: arrows drive the camera stick).
        constexpr std::array<int,hg::keyboard_key_count> codes{{
            GLFW_KEY_BACKSPACE,GLFW_KEY_LEFT_SHIFT,GLFW_KEY_RIGHT_SHIFT,GLFW_KEY_ENTER,
            GLFW_KEY_I,GLFW_KEY_L,GLFW_KEY_K,GLFW_KEY_J,
            GLFW_KEY_1,GLFW_KEY_3,GLFW_KEY_Q,GLFW_KEY_E,GLFW_KEY_V,GLFW_KEY_X,GLFW_KEY_SPACE,GLFW_KEY_C,
            GLFW_KEY_A,GLFW_KEY_D,GLFW_KEY_W,GLFW_KEY_S,GLFW_KEY_LEFT,GLFW_KEY_RIGHT,GLFW_KEY_UP,GLFW_KEY_DOWN
        }};
        static_assert(codes[static_cast<std::size_t>(hg::KeyboardKey::backspace)]==GLFW_KEY_BACKSPACE);
        static_assert(codes[static_cast<std::size_t>(hg::KeyboardKey::q)]==GLFW_KEY_Q);
        static_assert(codes[static_cast<std::size_t>(hg::KeyboardKey::e)]==GLFW_KEY_E);
        static_assert(codes[static_cast<std::size_t>(hg::KeyboardKey::digit1)]==GLFW_KEY_1);
        static_assert(codes[static_cast<std::size_t>(hg::KeyboardKey::digit3)]==GLFW_KEY_3);
        for(std::size_t n=0;n<codes.size();++n)keys[n]=glfwGetKey(window,codes[n])==GLFW_PRESS;
    }
    const bool tab=focused&&glfwGetKey(window,GLFW_KEY_TAB)==GLFW_PRESS;
    auto input=hg::host_controls::keyboard_with_select_alias(keys,tab,focused);
    if(!focused)return input;
    const auto press=[&](unsigned bit,bool down,int pressure=-1) {
        if(down){input.buttons&=std::uint16_t(~(1u<<bit));if(pressure>=0)input.pressures[pressure]=255;}
    };
    GLFWgamepadstate pad{};
    if(glfwGetGamepadState(GLFW_JOYSTICK_1,&pad)) {
        const unsigned bits[]{14,13,15,12,10,11,0,3,4,5,6,7,1,2};
        const int buttons[]{GLFW_GAMEPAD_BUTTON_A,GLFW_GAMEPAD_BUTTON_B,GLFW_GAMEPAD_BUTTON_X,GLFW_GAMEPAD_BUTTON_Y,
            GLFW_GAMEPAD_BUTTON_LEFT_BUMPER,GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER,GLFW_GAMEPAD_BUTTON_BACK,GLFW_GAMEPAD_BUTTON_START,
            GLFW_GAMEPAD_BUTTON_DPAD_UP,GLFW_GAMEPAD_BUTTON_DPAD_RIGHT,GLFW_GAMEPAD_BUTTON_DPAD_DOWN,GLFW_GAMEPAD_BUTTON_DPAD_LEFT,
            GLFW_GAMEPAD_BUTTON_LEFT_THUMB,GLFW_GAMEPAD_BUTTON_RIGHT_THUMB};
        const int pressures[]{6,5,7,4,8,9,-1,-1,2,0,3,1,-1,-1};
        for(unsigned i=0;i<14;++i)press(bits[i],pad.buttons[buttons[i]]==GLFW_PRESS,pressures[i]);
        press(8,pad.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER]>0,10);
        press(9,pad.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER]>0,11);
        const int axes[]{GLFW_GAMEPAD_AXIS_RIGHT_X,GLFW_GAMEPAD_AXIS_RIGHT_Y,GLFW_GAMEPAD_AXIS_LEFT_X,GLFW_GAMEPAD_AXIS_LEFT_Y};
        for(unsigned i=0;i<4;++i)if(input.axes[i]==128) {
            const auto value=pad.axes[axes[i]];
            if(value<-0.12f||value>0.12f)input.axes[i]=std::uint8_t(std::clamp(int((value+1)*127.5f+0.5f),0,255));
        }
    }
    return input;
}
}

int main(int argc,char** argv) {
    Session session;std::thread worker;GLFWwindow* window=nullptr;GLFWwindow* gpu_window=nullptr;
    try {
    std::vector<std::string> arguments{argv[0]};unsigned frame_limit=0;
    bool have_clock=false,have_slices=false,verify=false,unlimited=false,have_realtime=false,no_input=false,gpu_sprites=false,gpu_resident=false;
    bool video_rate_log=false;
    bool gpu_triangles=false;
    bool hidden_host=false;
    for(int i=1;i<argc;++i) {
        const std::string arg=argv[i];
        if(arg=="--host-frames"&&i+1<argc)frame_limit=unsigned(std::stoul(argv[++i]));
        else if(arg=="--verify-presentation")verify=true;
        else if(arg=="--unlimited")unlimited=true;
        else if(arg=="--gpu-sprites")gpu_sprites=true; // HG-DIAG-016: checked experimental GS offload.
        else if(arg=="--gpu-resident"){gpu_sprites=true;gpu_resident=true;} // HG-DIAG-017: page coherence experiment.
        else if(arg=="--gpu-triangles"){gpu_sprites=true;gpu_triangles=true;} // HG-DIAG-018: bounded integer tile batches.
        else if(arg=="--no-host-input")no_input=true; // HG-DIAG-011: deterministic presentation comparison.
        else if(arg=="--hidden-host"){hidden_host=true;no_input=true;} // HG-DIAG-011: unattended correctness replay.
        else if(arg=="--video-rate-log")video_rate_log=true; // HG-DIAG-011: host update counters, not swaps.
        else {arguments.push_back(arg);have_clock|=arg=="--clock-profile";have_slices|=arg=="--slices";have_realtime|=arg=="--realtime";}
    }
    if(!have_clock){arguments.push_back("--clock-profile");arguments.push_back("issue-slots");}
    if(!have_slices){arguments.push_back("--slices");arguments.push_back("3000000000");}
    if(!unlimited&&!have_realtime)arguments.push_back("--realtime");
    // Interactive playback defaults to the game's 30 Hz frame cadence. Hidden
    // correctness/performance runs and explicit unlimited runs remain untouched.
    session.limit_30=!hidden_host&&!unlimited;
    std::vector<char*> native_args;for(auto& arg:arguments)native_args.push_back(arg.data());
#ifdef _WIN32
        SetCurrentProcessExplicitAppUserModelID(L"HauntingGround.StaticRecomp.Viewer");
#endif
        glfwSetErrorCallback([](int code,const char* text){std::cerr<<"GLFW "<<code<<": "<<text<<'\n';});
        if(!glfwInit())throw std::runtime_error("Cannot initialize GLFW");
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,gpu_sprites?4:3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
        glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        window=glfwCreateWindow(960,672,"Haunting Ground — starting",nullptr,nullptr);
        if(!window)throw std::runtime_error("Cannot create game window");
        glfwSetWindowUserPointer(window,&session);
        glfwSetWindowRefreshCallback(window,[](GLFWwindow* target) {
            static_cast<Session*>(glfwGetWindowUserPointer(target))->refresh_requested=true;
        });
        if(!hidden_host) {
#ifdef _WIN32
        configure_taskbar_window(window);
#endif
        glfwShowWindow(window);glfwSetWindowPos(window,100,100);
#ifdef _WIN32
        register_taskbar_window(window);
#endif
        }
        glfwMakeContextCurrent(window);glfwSwapInterval(1);
        std::cerr<<"OpenGL renderer: "<<glGetString(GL_RENDERER)
                 <<" | version: "<<glGetString(GL_VERSION)<<'\n';
        ProbeGl gl;const auto program=make_probe_program(gl);
        GLuint vao=0,vbo=0,texture=0,controls_texture=0;gl.gen_vertex_arrays(1,&vao);gl.gen_buffers(1,&vbo);
        hg::host_controls::PanelState controls;
        const std::array<ProbeVertex,6> vertices{{{-1,-1,0,1},{1,-1,1,1},{-1,1,0,0},{-1,1,0,0},{1,-1,1,1},{1,1,1,0}}};
        gl.bind_vertex_array(vao);gl.bind_buffer(GL_ARRAY_BUFFER_,vbo);
        gl.buffer_data(GL_ARRAY_BUFFER_,sizeof(vertices),vertices.data(),GL_STATIC_DRAW_);
        gl.enable_attrib(0);gl.attrib_pointer(0,2,GL_FLOAT,GL_FALSE,sizeof(ProbeVertex),nullptr);
        gl.enable_attrib(1);gl.attrib_pointer(1,2,GL_FLOAT,GL_FALSE,sizeof(ProbeVertex),reinterpret_cast<void*>(offsetof(ProbeVertex,u)));
        glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        hg::NativeHost host;
        hg::HostFrameLimiter30 frame_limiter;
        host.closing=[&]{return session.closing.load();};
        host.input=[&](hg::HostInput& value) {
            std::lock_guard<std::mutex> lock(session.mutex);
            if(session.read_version==session.input_version)return false;
            value=session.input;session.read_version=session.input_version;return true;
        };
        host.present=[&](unsigned w,unsigned h,std::vector<std::uint32_t>&& pixels) {
            {
                std::lock_guard<std::mutex> lock(session.mutex);
                session.pending={w,h,std::move(pixels)};++session.produced;
            }
            glfwPostEmptyEvent(); // Wake the UI without touching either GL context.
        };
        host.stopped=[&](const std::string& text) {
            {std::lock_guard<std::mutex> lock(session.mutex);session.status=text;}
            glfwPostEmptyEvent();
        };
        host.checkpoint_requested=[&]{return session.checkpoint_requests.exchange(0)!=0;};
        host.notice=[&](const std::string& text) {
            {std::lock_guard<std::mutex> lock(session.mutex);
             session.notice=text;session.notice_until=std::chrono::steady_clock::now()+std::chrono::seconds(5);}
            glfwPostEmptyEvent();
        };
        if(gpu_sprites) {
            gpu_window=glfwCreateWindow(16,16,"GS worker",nullptr,window);
            if(!gpu_window)throw std::runtime_error("Cannot create OpenGL4.3 GS worker context");
            host.gs_context=[&](bool current){glfwMakeContextCurrent(current?gpu_window:nullptr);};
        }
        worker=std::thread([&] {
            std::unique_ptr<hg::GsSpriteAccelerator> accelerator;
            try {
                if(gpu_sprites){glfwMakeContextCurrent(gpu_window);accelerator=hg::make_gl_sprite_accelerator(gpu_resident,gpu_triangles,true);hg::gs_sprite_accelerator=accelerator.get();
                    if(gpu_triangles)hg::gs_triangle_accelerator=dynamic_cast<hg::GsTriangleAccelerator*>(accelerator.get());}
                session.result=hg::run_native_game(int(native_args.size()),native_args.data(),&host);
                if(accelerator)accelerator->finish();
            }
            catch(const std::exception& error){host.stopped(error.what());session.result=1;std::cerr<<error.what()<<'\n';}
            hg::gs_triangle_accelerator=nullptr;hg::gs_sprite_accelerator=nullptr;accelerator.reset();
            if(gpu_sprites)glfwMakeContextCurrent(nullptr);
            session.done=true;glfwPostEmptyEvent();
        });
        unsigned width=0,height=0,presented=0;bool verified=false;
        bool previous_frame_lock_key=false,previous_checkpoint_key=false;
        int last_framebuffer_width=-1,last_framebuffer_height=-1;
        std::uint64_t last_produced=0;unsigned last_presented=0;
        Frame previous_video;unsigned changed_presented=0,last_changed=0;
        auto report=std::chrono::steady_clock::now();const auto video_start=report;
        while(!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            const bool was_open=controls.open;
            if(!hidden_host) {
                const bool focused=glfwGetWindowAttrib(window,GLFW_FOCUSED)!=0;
                if(controls.update(focused&&!no_input&&glfwGetKey(window,GLFW_KEY_F1)==GLFW_PRESS,
                                   focused&&glfwGetKey(window,GLFW_KEY_ESCAPE)==GLFW_PRESS))break;
                const bool frame_lock_key=focused&&glfwGetKey(window,GLFW_KEY_F2)==GLFW_PRESS;
                if(frame_lock_key&&!previous_frame_lock_key) {
                    session.limit_30=!session.limit_30.load();
                    frame_limiter.reset();
                    session.refresh_requested=true;
                }
                previous_frame_lock_key=frame_lock_key;
                // F5: request an input-replay checkpoint (written by the game thread).
                const bool checkpoint_key=focused&&glfwGetKey(window,GLFW_KEY_F5)==GLFW_PRESS;
                if(checkpoint_key&&!previous_checkpoint_key)++session.checkpoint_requests;
                previous_checkpoint_key=checkpoint_key;
            }
            if(was_open!=controls.open)session.refresh_requested=true;
            Frame incoming;std::uint64_t produced;std::string status;
            const auto input=(no_input||controls.open)?hg::HostInput{}:read_input(window);
            const bool accept_frame=!session.limit_30.load()||
                frame_limiter.due(hg::HostFrameLimiter30::Clock::now());
            {
                std::lock_guard<std::mutex> lock(session.mutex);
                if(!(input==session.input)){session.input=input;++session.input_version;}
                if(accept_frame){incoming=std::move(session.pending);session.pending={};}
                produced=session.produced;status=session.status;
            }
            const bool fresh=!incoming.pixels.empty();
            if(video_rate_log&&fresh&&(incoming.width!=previous_video.width||incoming.height!=previous_video.height||
                incoming.pixels!=previous_video.pixels))++changed_presented;
            if(fresh) {
                glBindTexture(GL_TEXTURE_2D,texture);
                if(width!=incoming.width||height!=incoming.height) {
                    width=incoming.width;height=incoming.height;
                    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,incoming.pixels.data());
                } else glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,incoming.pixels.data());
            }
            int w=0,h=0;glfwGetFramebufferSize(window,&w,&h);
            const bool redraw=fresh||session.refresh_requested||w!=last_framebuffer_width||h!=last_framebuffer_height;
            if(redraw) {
            session.refresh_requested=false;
            glViewport(0,0,w,h);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
            if(width&&height&&w>0&&h>0) {
                const auto scale=std::min(double(w)/width,double(h)/height);
                const int vw=int(width*scale),vh=int(height*scale),vx=(w-vw)/2,vy=(h-vh)/2;
                glViewport(vx,vy,vw,vh);gl.use_program(program);glBindTexture(GL_TEXTURE_2D,texture);
                gl.bind_vertex_array(vao);gl.draw_arrays(GL_TRIANGLES,0,6);
                if(verify&&!verified&&fresh&&vw>0&&vh>0) {
                    const int px=vx+vw/2,py=vy+vh/2;
                    const auto sx=std::min(width-1,unsigned((px-vx+0.5)*width/vw));
                    const auto sy=std::min(height-1,unsigned((1.0-(py-vy+0.5)/vh)*height));
                    unsigned char pixel[4]{};glReadPixels(px,py,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
                    const auto expected=incoming.pixels[std::size_t(sy)*width+sx];
                    if(pixel[0]!=std::uint8_t(expected)||pixel[1]!=std::uint8_t(expected>>8)||pixel[2]!=std::uint8_t(expected>>16))
                        throw std::runtime_error("OpenGL presentation pixel mismatch");
                    if(expected&0xffffffu){verified=true;std::cerr<<"Direct OpenGL colored-pixel presentation verified\n";}
                }
            }
            // Host-only reference overlay. No guest pixel/state mutation or extra swaps.
            // The texture is built once, lazily; closed help performs no bitmap work.
            if(controls.open&&w>0&&h>0) {
                if(!controls_texture) {
                    const auto panel=hg::host_controls::make_panel();
                    glGenTextures(1,&controls_texture);glBindTexture(GL_TEXTURE_2D,controls_texture);
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
                    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,panel.width,panel.height,0,GL_RGBA,GL_UNSIGNED_BYTE,panel.pixels.data());
                }
                const auto scale=std::min(double(std::max(w-24,1))/hg::host_controls::PanelImage::width,
                                          double(std::max(h-24,1))/hg::host_controls::PanelImage::height);
                const int pw=std::max(1,int(hg::host_controls::PanelImage::width*scale));
                const int ph=std::max(1,int(hg::host_controls::PanelImage::height*scale));
                glViewport((w-pw)/2,(h-ph)/2,pw,ph);
                gl.use_program(program);glBindTexture(GL_TEXTURE_2D,controls_texture);
                gl.bind_vertex_array(vao);gl.draw_arrays(GL_TRIANGLES,0,6);
            }
            glfwSwapBuffers(window);if(fresh) {
                ++presented;
                if(session.limit_30.load())frame_limiter.presented(hg::HostFrameLimiter30::Clock::now());
            }
            last_framebuffer_width=w;last_framebuffer_height=h;
            }
            if(video_rate_log&&fresh)previous_video=std::move(incoming);
            const auto now=std::chrono::steady_clock::now();const double seconds=std::chrono::duration<double>(now-report).count();
            if(seconds>=1||session.done) {
                if(video_rate_log)std::cerr<<"Video rate seconds="<<std::chrono::duration<double>(now-video_start).count()
                    <<" interval="<<seconds<<" produced="<<(produced-last_produced)
                    <<" presented="<<(presented-last_presented)<<" changed="<<(changed_presented-last_changed)<<'\n';
                std::ostringstream title;title<<"Haunting Ground | ";
                if(session.done)title<<(status.empty()?"Stopped":status);
                else title<<std::fixed<<std::setprecision(1)<<double(produced-last_produced)/seconds<<" video updates/s | WASD / Arrows | Space / X / C / V";
                title<<" | F1: Controls | F2: 30 FPS Lock "<<(session.limit_30.load()?"On":"Off")<<" | F5: Save checkpoint";
                {std::lock_guard<std::mutex> lock(session.mutex);
                 if(!session.notice.empty()&&now<session.notice_until)title<<" | "<<session.notice;}
                glfwSetWindowTitle(window,title.str().c_str());last_produced=produced;last_presented=presented;
                last_changed=changed_presented;report=now;
            }
            if(frame_limit&&presented>=frame_limit)break;
            if(session.done&&frame_limit)break;
            if(w==0||h==0)glfwWaitEventsTimeout(.05);
            else if(!redraw)glfwWaitEventsTimeout(.004); // Events/new frames wake early; gamepad polling remains bounded.
        }
        session.closing=true;worker.join();
        std::cerr<<"Native host produced="<<session.produced<<" presented="<<presented<<" result="<<session.result<<'\n';
        if(controls_texture)glDeleteTextures(1,&controls_texture);
        glDeleteTextures(1,&texture);gl.delete_buffers(1,&vbo);gl.delete_vertex_arrays(1,&vao);gl.delete_program(program);
        if(gpu_window)glfwDestroyWindow(gpu_window);
        glfwDestroyWindow(window);glfwTerminate();
        if(verify&&!verified)return 1;
        return session.result==2?2:session.result;
    } catch(const std::exception& error) {
        session.closing=true;if(worker.joinable())worker.join();std::cerr<<error.what()<<'\n';
        if(gpu_window)glfwDestroyWindow(gpu_window);
        if(window)glfwDestroyWindow(window);glfwTerminate();return 1;
    }
}
