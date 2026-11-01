#pragma once
#include "hg/native_host.hpp"
#include <string>
#include <string_view>

namespace hg {
// HG-DIAG-006: strict decimal controller snapshots for logged slice replay.
// Failure leaves the caller's state untouched; no runtime instruction decoding.
inline bool parse_diagnostic_pad(std::string_view text,HostInput& output) {
    if(text.substr(0,4)!="pad:")return false;
    text.remove_prefix(4);
    HostInput candidate;
    for(unsigned field=0;field<17;++field) {
        const auto end=text.find(',');
        const auto token=text.substr(0,end);
        const unsigned limit=field?255:65535;
        unsigned value=0;
        if(token.empty())return false;
        for(const char c:token) {
            if(c<'0'||c>'9')return false;
            const unsigned digit=unsigned(c-'0');
            if(value>(limit-digit)/10)return false;
            value=value*10+digit;
        }
        if(!field)candidate.buttons=std::uint16_t(value);
        else if(field<5)candidate.axes[field-1]=std::uint8_t(value);
        else candidate.pressures[field-5]=std::uint8_t(value);
        if(field==16) {if(end!=std::string_view::npos)return false;}
        else {if(end==std::string_view::npos)return false;text.remove_prefix(end+1);}
    }
    output=candidate;return true;
}
inline std::string format_diagnostic_pad(const HostInput& input) {
    std::string text="pad:"+std::to_string(input.buttons);
    for(const auto value:input.axes)text+=","+std::to_string(unsigned(value));
    for(const auto value:input.pressures)text+=","+std::to_string(unsigned(value));
    return text;
}
}
