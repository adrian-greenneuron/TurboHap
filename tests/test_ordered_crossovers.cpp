#include "../src/ordered_crossovers.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include <omp.h>

int main() {
    using findhap::OrderedCrossoverBatch;
    assert(OrderedCrossoverBatch::capacity(0) == 0);
    assert(OrderedCrossoverBatch::capacity(size_t(-1)) == 0);
    for (size_t budget : {size_t(804), size_t(4096), size_t(65536), size_t(1024*1024)}) {
        const size_t segments = budget == 804 ? 1 : 3;
        OrderedCrossoverBatch batch(segments, budget);
        assert(batch.resident_bound() <= budget && batch.rows() > 0);
        FILE* output = std::tmpfile(); assert(output);
        std::string expected;
        // Include a partial batch and reuse all slots with changing counts.
        for (int pass = 0; pass < 3; ++pass) {
            const size_t rows = pass == 1 ? 1 : batch.rows();
            std::vector<std::string> oracle(rows * segments);
            #pragma omp parallel for schedule(dynamic,1) num_threads(4)
            for (size_t slot = 0; slot < rows * segments; ++slot) {
                char records[800] = {};
                const int count = int((slot + pass * 3) % 11);
                for (int k = 0; k < count; ++k) {
                    std::snprintf(records + k * 80, 80, "pass=%d animal=%zu segment=%zu record=%d  ", pass,slot/segments,slot%segments,k);
                    oracle[slot] += std::string(records + k * 80) + '\n';
                }
                batch.capture(slot/segments,slot%segments,count,records);
            }
            for (const auto& record : oracle) expected += record;
            assert(batch.emit(output, rows));
        }
        assert(std::fflush(output) == 0); std::rewind(output);
        std::string actual(expected.size(), '!');
        assert(std::fread(actual.data(),1,actual.size(),output) == actual.size());
        assert(actual == expected && std::fgetc(output) == EOF);
        assert(std::fclose(output) == 0);
        FILE* readonly=std::fopen("/dev/null","r");assert(readonly);
        char record[800]={};std::strcpy(record,"record");
        for(size_t segment=0;segment<segments;++segment)batch.capture(0,segment,1,record);
        assert(!batch.emit(readonly,1) && std::ferror(readonly));
        std::fclose(readonly);
    }
}
