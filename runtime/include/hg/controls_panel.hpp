#pragma once
#include "hg/keyboard_input.hpp"
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace hg::host_controls {
// Host UI only. These helpers never touch guest state, clocks or GS memory.
struct PanelState {
    bool open=false;
    bool previous_help=false,previous_escape=false;
    // Returns a host-close request. Holding Escape after dismissing help cannot quit.
    bool update(bool help,bool escape) noexcept {
        const bool help_pressed=help&&!previous_help;
        const bool escape_pressed=escape&&!previous_escape;
        previous_help=help;previous_escape=escape;
        if(escape_pressed) {
            if(open){open=false;return false;}
            return true;
        }
        if(help_pressed)open=!open;
        return false;
    }
};

inline HostInput keyboard_with_select_alias(KeyboardKeys keys,bool tab,bool focused=true) {
    keys[static_cast<std::size_t>(KeyboardKey::backspace)]|=tab;
    return keyboard_input(keys,focused);
}

struct BindingRow {std::string_view control,keys;};
inline constexpr std::array<BindingRow,15> bindings{{
    {"MOVE - LEFT STICK","W / A / S / D"},
    {"CAMERA - RIGHT STICK","ARROW KEYS"},
    {"D-PAD","I / J / K / L"},
    {"CROSS","SPACE"},
    {"CIRCLE","X"},
    {"SQUARE","C"},
    {"TRIANGLE","V"},
    {"L1","Q"},
    {"L2","1 (TOP ROW)"},
    {"R1","E"},
    {"R2","3 (TOP ROW)"},
    {"L3 - LEFT STICK CLICK","LEFT SHIFT"},
    {"R3 - RIGHT STICK CLICK","RIGHT SHIFT"},
    {"START / PAUSE","ENTER"},
    {"SELECT","TAB / BACKSPACE"}
}};

// Independently drawn 5-by-7 host UI lettering; no font file or game asset.
inline std::array<unsigned char,7> glyph(char c) {
    switch(c) {
    case 'A':return {14,17,17,31,17,17,17};
    case 'B':return {30,17,17,30,17,17,30};
    case 'C':return {14,17,16,16,16,17,14};
    case 'D':return {30,17,17,17,17,17,30};
    case 'E':return {31,16,16,30,16,16,31};
    case 'F':return {31,16,16,30,16,16,16};
    case 'G':return {14,17,16,23,17,17,15};
    case 'H':return {17,17,17,31,17,17,17};
    case 'I':return {31,4,4,4,4,4,31};
    case 'J':return {7,2,2,2,2,18,12};
    case 'K':return {17,18,20,24,20,18,17};
    case 'L':return {16,16,16,16,16,16,31};
    case 'M':return {17,27,21,21,17,17,17};
    case 'N':return {17,25,21,19,17,17,17};
    case 'O':return {14,17,17,17,17,17,14};
    case 'P':return {30,17,17,30,16,16,16};
    case 'Q':return {14,17,17,17,21,18,13};
    case 'R':return {30,17,17,30,20,18,17};
    case 'S':return {15,16,16,14,1,1,30};
    case 'T':return {31,4,4,4,4,4,4};
    case 'U':return {17,17,17,17,17,17,14};
    case 'V':return {17,17,17,17,17,10,4};
    case 'W':return {17,17,17,21,21,21,10};
    case 'X':return {17,17,10,4,10,17,17};
    case 'Y':return {17,17,10,4,4,4,4};
    case 'Z':return {31,1,2,4,8,16,31};
    case '0':return {14,17,19,21,25,17,14};
    case '1':return {4,12,4,4,4,4,14};
    case '2':return {14,17,1,2,4,8,31};
    case '3':return {30,1,1,14,1,1,30};
    case '4':return {2,6,10,18,31,2,2};
    case '5':return {31,16,16,30,1,1,30};
    case '6':return {14,16,16,30,17,17,14};
    case '7':return {31,1,2,4,8,8,8};
    case '8':return {14,17,17,14,17,17,14};
    case '9':return {14,17,17,15,1,1,14};
    case ' ':return {};
    case '-':return {0,0,0,31,0,0,0};
    case '/':return {1,1,2,4,8,16,16};
    case ':':return {0,4,4,0,4,4,0};
    case '(':return {2,4,8,8,8,4,2};
    case ')':return {8,4,2,2,2,4,8};
    case '.':return {0,0,0,0,0,4,4};
    default:throw std::invalid_argument("unsupported controls-panel glyph");
    }
}

inline constexpr std::uint32_t rgba(unsigned r,unsigned g,unsigned b) {
    return 0xff000000u|(b<<16)|(g<<8)|r;
}
struct PanelImage {
    static constexpr unsigned width=740,height=490;
    std::vector<std::uint32_t> pixels;
    PanelImage():pixels(width*height,rgba(22,27,35)){}
    void fill(unsigned x,unsigned y,unsigned w,unsigned h,std::uint32_t color) {
        if(x>width||y>height||w>width-x||h>height-y)
            throw std::out_of_range("controls-panel rectangle outside image");
        for(unsigned row=y;row<y+h;++row)
            for(unsigned column=x;column<x+w;++column)pixels[row*width+column]=color;
    }
    void text(unsigned x,unsigned y,std::string_view value,unsigned scale,std::uint32_t color) {
        if(!scale||scale>4||x>width||y>height||7*scale>height-y||
           value.size()>(width-x)/(6*scale))
            throw std::out_of_range("controls-panel text outside image");
        for(char c:value) {
            const auto rows=glyph(c);
            for(unsigned row=0;row<7;++row)for(unsigned column=0;column<5;++column)
                if(rows[row]&(1u<<(4-column)))fill(x+column*scale,y+row*scale,scale,scale,color);
            x+=6*scale;
        }
    }
};

inline PanelImage make_panel() {
    PanelImage image;
    const auto bright=rgba(239,242,246),muted=rgba(179,189,202),accent=rgba(149,202,222);
    image.fill(0,0,PanelImage::width,3,accent);
    image.text(24,21,"KEYBOARD CONTROLS",3,bright);
    image.text(494,25,"F1 / ESC: CLOSE",2,accent);
    image.text(28,61,"PS2 CONTROL",2,muted);
    image.text(366,61,"KEYBOARD",2,muted);
    for(std::size_t n=0;n<bindings.size();++n) {
        const unsigned y=86+unsigned(n)*22;
        if(n%2==0)image.fill(16,y-4,PanelImage::width-32,22,rgba(32,39,49));
        image.text(28,y,bindings[n].control,2,bright);
        image.text(366,y,bindings[n].keys,2,accent);
    }
    image.fill(24,420,PanelImage::width-48,1,rgba(78,92,110));
    image.text(28,435,"GAME KEEPS RUNNING WHILE THIS PANEL IS OPEN.",2,muted);
    image.text(28,458,"PAUSE WITH ENTER FIRST. F1 RETURNS TO THE GAME.",2,muted);
    image.text(28,475,"F2 TOGGLES THE HOST 30 FPS LOCK.",2,accent);
    return image;
}
} // namespace hg::host_controls
