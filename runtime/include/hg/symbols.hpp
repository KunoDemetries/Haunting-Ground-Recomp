#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace hg {
// Host diagnostics only: known EE function starts (configured roots and JAL
// targets) with evidence-backed names where available (out/function_names.json,
// see docs/PROGRESS.md "function naming"). The generated translation registers the
// table; an empty table simply omits the description. Never affects guest state.
struct SymbolEntry {std::uint32_t start;const char* name;};
struct SymbolTable {const SymbolEntry* entries=nullptr;std::size_t count=0;};
inline SymbolTable ee_symbol_table{};

// " (nearest known function start: NAME+0xOFF)" for a guest pc, or "" when unknown.
inline std::string describe_ee_pc(std::uint32_t pc) {
    const auto& table=ee_symbol_table;
    std::size_t lo=0,hi=table.count;
    while(lo<hi){const auto mid=(lo+hi)/2;if(table.entries[mid].start<=pc)lo=mid+1;else hi=mid;}
    if(!lo)return {};
    const auto& entry=table.entries[lo-1];
    const auto offset=pc-entry.start;
    if(offset>0x10000)return {};
    char text[160];
    if(entry.name)std::snprintf(text,sizeof text," (nearest known function start: %s+0x%x)",entry.name,unsigned(offset));
    else std::snprintf(text,sizeof text," (nearest known function start: sub_%06x+0x%x)",unsigned(entry.start),unsigned(offset));
    return text;
}
}
