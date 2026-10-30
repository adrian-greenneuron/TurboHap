#include "../src/output_streams.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <random>
#include <vector>

int main() {
    const int table[6][6] = {{0,3,1,5,5,5},{3,5,4,5,5,5},{1,4,2,5,5,5},
                              {5,5,5,5,5,5},{5,5,5,5,5,5},{5,5,5,5,5,5}};
    std::array<uint8_t,64> bytes{};
    for (int a=0;a<6;++a) for(int b=0;b<6;++b) bytes[6*a+b]=table[a][b];
    std::mt19937 random(291026);
    for (int length=0;length<=257;++length) {
        std::vector<int8_t> first(length),second(length),actual(length+2, -99);
        std::vector<char> hap(2*length+2,'!');
        for(int i=0;i<length;++i){first[i]=random()%6;second[i]=random()%6;}
        for(int parents=0;parents<4;++parents) {
            const int8_t* a=parents&1 ? first.data():nullptr;
            const int8_t* b=parents&2 ? second.data():nullptr;
            for(bool male_x : {false,true}) {
                turbohap_output::reconstruct(a,b,actual.data()+1,length,table,bytes.data(),male_x);
                for(int i=0;i<length;++i) {
                    int expected=table[a?a[i]:5][b?b[i]:5];
                    if(male_x && (expected==1 || expected>2)) expected=5;
                    assert(actual[i+1]==expected);
                }
                assert(actual.front()==-99 && actual.back()==-99);
            }
            for(int domain=0;domain<2;++domain) {
                if(domain) for(int i=0;i<length;++i){first[i]=static_cast<int8_t>(random());second[i]=static_cast<int8_t>(random());}
                turbohap_output::encode_haplotypes(a,b,hap.data()+1,length);
                for(int i=0;i<length;++i) for(int parent=0;parent<2;++parent) {
                    const int8_t* row=parent?b:a;int value=row?row[i]:1;
                    value=value==1 ? parent+1 : value/2+1;
                    assert(hap[1+2*i+parent]==static_cast<char>(value+'0'));
                }
                assert(hap.front()=='!' && hap.back()=='!');
            }
            for(int i=0;i<length;++i){first[i]=random()%6;second[i]=random()%6;}
        }
    }
    std::puts("Output streams match scalar tables, signed haplotype bytes, null parents, X rules and every vector tail");
}
