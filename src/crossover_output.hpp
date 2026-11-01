#pragma once
#include "integer_output.hpp"
#include <algorithm>
namespace turbohap_output {
// Exactly the first 79 bytes and terminating NUL of snprintf(...,80,...).
inline void crossover_line(char* out, int animal, int parent, int segment,
                           int first, int last, int hap, int first_parent, int second_parent) {
    char text[96];char* end=text;
    end=append_integer(end,animal,9);end=append_integer(end,parent,9);
    for(int value : {segment,first,last,hap,first_parent,second_parent})end=append_integer(end,value,7);
    const size_t bytes=std::min(size_t(79),size_t(end-text));
    std::memcpy(out,text,bytes);out[bytes]=0;
}
}
