// Compare the actual C++ kernel with the pinned Fortran routine, including
// a conflict followed by missing loci (the old two-pass scan rejected it).
#define main findhap_program_main
#include "../src/findhap.cpp"
#undef main
#include <iostream>
#include <random>

extern "C" void reference_listhap_(int8_t*, int8_t*, int*, int*, int*, int*, int*,
                                  int*, int*, int*, int*, int*, int*, float*);

static bool index_matches(const CandidateIndex& index, const std::vector<int8_t>& library,
                          int length, int nhap) {
    for (int h = 1; h <= nhap; ++h) {
        auto expected = index.encode(library.data() + (size_t)(h - 1) * length);
        auto actual = index.entry(h);
        if (actual.zero != expected.zero || actual.two != expected.two) {
            std::cerr << "Stale candidate signature: haplotype=" << h << '\n';
            return false;
        }
    }
    return true;
}

static bool compare_case(int length, int mode, float errate,
                         std::vector<int8_t> geno, std::vector<int8_t> library,
                         std::vector<int> frequencies, std::vector<int> order,
                         int* suggest, int nhap) {
    int maxlen = length, maxhap = frequencies.size(), seg = 1, maxseg = 1;
    int ref_found[2] = {}, ref_nhap = nhap;
    auto ref_geno = geno, ref_library = library;
    auto ref_freq = frequencies, ref_order = order;
    reference_listhap_(ref_geno.data(), ref_library.data(), ref_freq.data(), ref_order.data(),
                       suggest, &length, &maxlen, &ref_nhap, &maxhap, &seg,
                       &maxseg, ref_found, &mode, &errate);
    for (bool indexed : {false, true}) {
        auto actual_geno = geno, actual_library = library;
        auto actual_freq = frequencies, actual_order = order;
        int actual_nhap = nhap, found[2] = {};
        CandidateIndex index(length, actual_library.data(), maxlen, nhap);
        bool genotype_changed;
        if (mode == 0)
            genotype_changed = listhap<0>(actual_geno.data(), actual_library.data(), actual_freq.data(), actual_order.data(), suggest,
                       length, maxlen, &actual_nhap, maxhap, seg, maxseg, found, errate, indexed ? &index : nullptr);
        else
            genotype_changed = listhap<1>(actual_geno.data(), actual_library.data(), actual_freq.data(), actual_order.data(), suggest,
                       length, maxlen, &actual_nhap, maxhap, seg, maxseg, found, errate, indexed ? &index : nullptr);
        if (genotype_changed != (actual_geno != geno) ||
            actual_geno != ref_geno || actual_library != ref_library || actual_nhap != ref_nhap ||
            memcmp(found, ref_found, sizeof(found)) || actual_freq != ref_freq || actual_order != ref_order ||
            (indexed && !index_matches(index, actual_library, length, actual_nhap))) {
            std::cerr << "Mismatch: length=" << length << " mode=" << mode
                      << " errate=" << errate << " indexed=" << indexed << '\n';
            return false;
        }
    }
    return true;
}

static bool compare_trace(int length, int mode) {
    int maxlen = length, maxhap = 256, seg = 1, maxseg = 1;
    int nhap = 0, ref_nhap = 0;
    float errate = 0.05f;
    std::vector<int8_t> library((size_t)length * maxhap), ref_library = library;
    std::vector<int> frequencies(maxhap), ref_freq = frequencies, order(maxhap + 1);
    for (int h = 0; h <= maxhap; ++h) order[h] = h;
    auto ref_order = order;
    auto index = std::make_unique<CandidateIndex>(length, library.data(), length, nhap);
    std::mt19937 rng(20261019 + length + mode);
    for (int query = 0; query < 100; ++query) {
        // Model a later population pass after external pedigree allele fills.
        // Production discards the index at that boundary and rebuilds it.
        if (query && query % 25 == 0) {
            for (int h = 0; h < nhap; ++h) {
                int locus = rng() % length;
                library[(size_t)h * length + locus] = ref_library[(size_t)h * length + locus] = rng() % 3;
            }
            index = std::make_unique<CandidateIndex>(length, library.data(), length, nhap);
        }
        std::vector<int8_t> geno(length);
        for (auto& value : geno) {
            int code = rng() % 4;
            value = code == 3 ? 5 : code;
        }
        auto initial_geno = geno, ref_geno = geno;
        int suggest[16] = {}, found[2] = {}, ref_found[2] = {};
        if (nhap) {
            suggest[0] = 1 + rng() % nhap;
            suggest[8] = 1 + rng() % nhap;
        }
        reference_listhap_(ref_geno.data(), ref_library.data(), ref_freq.data(), ref_order.data(),
                           suggest, &length, &maxlen, &ref_nhap, &maxhap, &seg,
                           &maxseg, ref_found, &mode, &errate);
        bool changed = mode == 0
            ? listhap<0>(geno.data(), library.data(), frequencies.data(), order.data(), suggest,
                        length, maxlen, &nhap, maxhap, seg, maxseg, found, errate, index.get())
            : listhap<1>(geno.data(), library.data(), frequencies.data(), order.data(), suggest,
                        length, maxlen, &nhap, maxhap, seg, maxseg, found, errate, index.get());
        if (changed != (geno != initial_geno) || geno != ref_geno || library != ref_library ||
            frequencies != ref_freq || order != ref_order || nhap != ref_nhap ||
            memcmp(found, ref_found, sizeof(found)) || !index_matches(*index, library, length, nhap)) {
            std::cerr << "Trace mismatch: length=" << length << " mode=" << mode
                      << " query=" << query << '\n';
            return false;
        }
    }
    return true;
}

#ifdef FINDHAP_HIGH_MEMORY
static bool compare_repeated_trace(int length, int mode) {
    int maxlen = length, maxhap = 4096, seg = 1, maxseg = 1, nhap = 1200, ref_nhap = nhap;
    float errate = 0.004f;
    std::mt19937 rng(20261020 + length + mode);
    std::vector<int8_t> library((size_t)length * maxhap, 0);
    std::vector<int> frequencies(maxhap), order(maxhap + 1);
    for (int h = 0; h < nhap; ++h) {
        frequencies[h] = rng() % 5;
        for (int l = 0; l < length; ++l)
            library[(size_t)h * length + l] = l % 11 == 0 ? 1 : 2 * (rng() % 2);
    }
    for (int h = 0; h <= maxhap; ++h) order[h] = h;
    std::shuffle(order.begin() + 1, order.begin() + nhap + 1, rng);
    auto ref_library = library;
    auto ref_freq = frequencies, ref_order = order;
    CandidateIndex index(length, library.data(), maxlen, nhap);
    std::vector<std::vector<int8_t>> queries(16, std::vector<int8_t>(length));
    for (auto& query : queries)
        for (auto& value : query) value = rng() % 3;
    for (int iteration = 0; iteration < 400; ++iteration) {
        // Reuse the index across mode transitions too: a frequency tree
        // disabled in mode 0 must rebuild from current counts in mode 1.
        if (iteration && iteration % 71 == 0) mode = 1 - mode;
        if (iteration && iteration % 29 == 0) {
            const int h = rng() % nhap, locus = rng() % length;
            library[(size_t)h * length + locus] = ref_library[(size_t)h * length + locus] = rng() % 3;
            index.refresh(h + 1, library.data() + (size_t)h * length);
        }
        auto geno = queries[(iteration / 4) % queries.size()];
        auto initial = geno, ref_geno = geno;
        int found[2] = {}, ref_found[2] = {}, suggest[16] = {};
        if (mode == 0 && iteration % 3 == 0) suggest[0] = 1 + rng() % nhap;
        reference_listhap_(ref_geno.data(), ref_library.data(), ref_freq.data(), ref_order.data(),
                          suggest, &length, &maxlen, &ref_nhap, &maxhap, &seg,
                          &maxseg, ref_found, &mode, &errate);
        const bool changed = mode == 0
            ? listhap<0>(geno.data(), library.data(), frequencies.data(), order.data(), suggest,
                         length, maxlen, &nhap, maxhap, seg, maxseg, found, errate, &index)
            : listhap<1>(geno.data(), library.data(), frequencies.data(), order.data(), suggest,
                         length, maxlen, &nhap, maxhap, seg, maxseg, found, errate, &index);
        if (changed != (geno != initial) || geno != ref_geno || library != ref_library ||
            frequencies != ref_freq || order != ref_order || nhap != ref_nhap ||
            memcmp(found, ref_found, sizeof(found)) || !index_matches(index, library, length, nhap)) {
            std::cerr << "Repeated-query mismatch: length=" << length << " mode=" << mode
                      << " iteration=" << iteration << '\n';
            return false;
        }
    }
    return true;
}
#endif

int main() {
    int cases = 0;
    for (int length : {15, 16, 17, 128, 129, 600, 4095, 4096, 4097, 100000, 524288}) {
        for (int mode : {0, 1}) {
            int maxhap = 8;
            std::vector<int8_t> geno(length, 0), library((size_t)length * maxhap, 0);
            std::fill(library.begin(), library.begin() + length, 1);
            library[0] = 2;
            library[1] = library[2] = library[3] = 0;
            // A second candidate with many conflicts exercises wide SIMD counts.
            std::fill(library.begin() + length, library.begin() + 2 * length, 2);
            std::vector<int> frequencies(8), order = {0,1,2,3,4,5,6,7,8};
            frequencies[0] = 2; frequencies[1] = 1;
            int suggest[16] = {};
            if (!compare_case(length, mode, 0.1f, geno, library, frequencies, order, suggest, 2)) return 1;
            ++cases;
        }
    }
    // Zero conflicts still require missing-locus fills and the no-checkable-
    // loci decision. Also cover an entirely missing query (check == 0).
    for (int length : {33, 100, 129}) {
        for (int mode : {0, 1}) {
            const int maxhap = 8;
            std::vector<int8_t> geno(length), library((size_t)length * maxhap, 1);
            const int8_t codes[] = {0, 2, 1, 5};
            for (int l = 0; l < length; ++l) geno[l] = codes[l % 4];
            library[length] = 0;
            std::vector<int> frequencies(maxhap), order(maxhap + 1);
            frequencies[0] = 3; frequencies[1] = 1;
            for (int h = 0; h <= maxhap; ++h) order[h] = h;
            int suggest[16] = {};
            if (!compare_case(length, mode, 0.05f, geno, library, frequencies, order, suggest, 2)) return 1;
            ++cases;
            std::fill(geno.begin(), geno.end(), 5);
            if (!compare_case(length, mode, 0.0f, geno, library, frequencies, order, suggest, 2)) return 1;
            ++cases;
        }
    }
    // Exercise block edges, missingness, frequency ties, reordered candidates
    // and ancestor suggestions against the independent Fortran routine.
    std::mt19937 rng(20261019);
    for (int length : {1, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129, 257, 600, 4097}) {
        for (int mode : {0, 1}) {
            for (float errate : {0.0f, 0.01f, 0.05f, 0.125f, 0.5f, 1.0f}) {
                const int maxhap = 16, nhap = 6;
                std::vector<int8_t> geno(length), library((size_t)length * maxhap, 0);
                std::vector<int> frequencies(maxhap), order(maxhap + 1);
                for (auto& value : geno) {
                    int code = rng() % 4;
                    value = code == 3 ? 5 : code;
                }
                for (int h = 0; h < nhap; ++h) {
                    frequencies[h] = rng() % 8;
                    for (int l = 0; l < length; ++l) library[(size_t)h * length + l] = rng() % 3;
                }
                for (int h = 0; h <= maxhap; ++h) order[h] = h;
                std::shuffle(order.begin() + 1, order.begin() + nhap + 1, rng);
                int suggest[16] = {};
                suggest[0] = 1 + rng() % nhap;
                suggest[8] = 1 + rng() % nhap;
                if (!compare_case(length, mode, errate, geno, library, frequencies, order, suggest, nhap)) return 1;
                ++cases;
            }
        }
    }
    for (int length : {33, 100, 257})
        for (int mode : {0, 1})
            if (!compare_trace(length, mode)) return 1;
#ifdef FINDHAP_HIGH_MEMORY
    for (int length : {33, 100})
        for (int mode : {0, 1})
            if (!compare_repeated_trace(length, mode)) return 1;
    // Cross bitmap/tree boundaries with frequency ties, shuffled order, an
    // inactive slot and zero/error-tolerant searches. Compare every side effect,
    // including minima belonging to rejected candidates and library growth.
    for (int count : {63, 64, 65, 127, 128, 129, 255, 1023, 1024, 1025, 2047}) {
        for (int length : {33, 100, 257}) {
            for (int mode : {0, 1}) {
                for (float errate : {0.0f, 0.004f, 0.05f}) {
                    const int maxhap = count + 8;
                    std::vector<int8_t> geno(length), library((size_t)length * maxhap, 0);
                    std::vector<int> frequencies(maxhap), order(maxhap + 1);
                    for (auto& value : geno) {
                        const int code = rng() % 4;
                        value = code == 3 ? 5 : code;
                    }
                    for (int h = 0; h < count; ++h) {
                        frequencies[h] = rng() % 5;
                        for (int l = 0; l < length; ++l)
                            library[(size_t)h * length + l] = rng() % 3;
                    }
                    for (int l = 0; l < length; ++l)
                        library[(size_t)(count - 1) * length + l] = geno[l] == 2 ? 2 : 0;
                    for (int h = 0; h <= maxhap; ++h) order[h] = h;
                    std::shuffle(order.begin() + 1, order.begin() + count + 1, rng);
                    order[3] = 0;
                    int suggest[16] = {};
                    if (mode == 0) suggest[0] = 1 + rng() % count;
                    if (!compare_case(length, mode, errate, geno, library, frequencies,
                                      order, suggest, count)) return 1;
                    ++cases;
                }
            }
        }
    }
#endif
    std::cout << "PASS: C++/Fortran listhap state matches in " << cases
              << " cases with/without indexing, both modes and lengths through 524288; "
              << "600 sequential indexed queries with growth, mutation and rebuilds"
#ifdef FINDHAP_HIGH_MEMORY
              << "; 1600 repeated queries with allele mutation and order changes"
#endif
              << '\n';
}
