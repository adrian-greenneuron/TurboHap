#include "../src/pedigree_rematch.hpp"
#include <cassert>
#include <random>
#include <iostream>

int main() {
    // Compare segment reconstruction with the original full-locus traversal,
    // including absent haplotypes, unknown alleles, signed bytes, gaps, tails
    // and overlapping previous segments with last-informative-write semantics.
    std::mt19937 rng(291026);
    for (int trial = 0; trial < 5000; ++trial) {
        const int loci = 1 + rng() % 257, animals = 1 + rng() % 7, haps = 5;
        const int stride = loci, previous_count = 1 + rng() % 12;
        std::vector<int> ps(1), pl(1), starts(1), lengths(1);
        for (int p = 0; p < previous_count; ++p) {
            int begin = 1 + rng() % loci;
            ps.push_back(begin); pl.push_back(1 + rng() % (loci - begin + 1));
        }
        if (trial % 2 == 0) {
            ps.resize(1); pl.resize(1);
            for (int begin = 1; begin <= loci;) {
                int length = 1 + rng() % std::min(40, loci - begin + 1);
                ps.push_back(begin); pl.push_back(length); begin += length;
            }
        }
        const int actual_previous_count = int(ps.size()) - 1;
        for (int begin = 1; begin <= loci;) {
            int length = 1 + rng() % std::min(35, loci - begin + 1);
            starts.push_back(begin); lengths.push_back(length); begin += length;
        }
        auto plan = pedigree_rematch::make_plan(starts.data(), lengths.data(), starts.size()-1,
                                                ps.data(), pl.data(), actual_previous_count);
        assert(plan.disjoint);
        std::vector<int32_t> kh(size_t(2) * animals * actual_previous_count);
        for (auto& v : kh) v = int(rng() % 7) - 1;
        std::vector<size_t> offsets(actual_previous_count);
        std::vector<int8_t> library(size_t(actual_previous_count) * haps * stride);
        for (int p = 0; p < actual_previous_count; ++p) offsets[p] = size_t(p) * haps * stride;
        for (auto& v : library) v = int(rng() % 9) - 3;
        for (int animal = 1; animal <= animals; ++animal) {
            std::vector<int8_t> expected(loci,5);
            for (int p = 1; p <= actual_previous_count; ++p) {
                size_t entry = size_t(p-1)*2*animals + size_t(animal-1)*2;
                int a=kh[entry], b=kh[entry+1];
                if (a > 0 && b > 0) {
                    const auto* one = library.data()+offsets[p-1]+size_t(a-1)*stride;
                    const auto* two = library.data()+offsets[p-1]+size_t(b-1)*stride;
                    for (int l = 0; l < pl[p]; ++l)
                        if (one[l] != 1 && two[l] != 1)
                            expected[ps[p]+l-1] = (one[l]+two[l])/2;
                }
            }
            for (int s = 1; s < int(starts.size()); ++s) {
                std::vector<int8_t> actual(lengths[s]);
                int known = pedigree_rematch::reconstruct(actual.data(), lengths[s], animal, animals,
                    plan.segments[s-1],kh.data(),library.data(),offsets.data(),stride,5);
                int expected_known=0;
                for (int l=0;l<lengths[s];++l) {
                    assert(actual[l] == expected[starts[s]+l-1]);
                    expected_known += actual[l] != 5;
                }
                assert(known == expected_known);
            }
        }
        // The fast sweep over a disjoint previous partition must agree too.
        auto partition = pedigree_rematch::make_plan(starts.data(),lengths.data(),starts.size()-1,
                                                    starts.data(),lengths.data(),starts.size()-1);
        assert(partition.disjoint);
        for (size_t s=0;s<partition.segments.size();++s) {
            assert(partition.segments[s].size()==1);
            const auto& o=partition.segments[s][0];
            assert(o.previous_segment==int(s)+1 && o.previous_offset==0 && o.destination_offset==0);
            assert(o.length==lengths[s+1]);
        }
    }
    int starts[]={0,1,4}, lengths[]={0,5,3};
    assert(!pedigree_rematch::make_plan(starts,lengths,2,starts,lengths,2).disjoint);
    std::cout << "PASS 5000 full-reconstruction differential cases and overlap fallback\n";
}
