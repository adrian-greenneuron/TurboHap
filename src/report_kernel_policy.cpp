// Built only into turbohap-build-info, with the corresponding engine target's definitions.
#include "ordered_scan.hpp"
#include <ostream>

void TURBOHAP_REPORT_POLICY(std::ostream& out) {
#if TURBOHAP_RUNTIME_SCAN_LAYOUT
    const bool blocked = findhap_ordered_scan::runtime_blocked_layout();
#else
    constexpr bool blocked = TURBOHAP_TILED_SCAN;
#endif
    out << "{\"policy\":\"" << TURBOHAP_PLATFORM_POLICY << "\""
        << ",\"ordered_scan_candidates\":" << findhap_ordered_scan::batch_candidates
        << ",\"ordered_scan_backend\":\"" << findhap_ordered_scan::backend() << "\""
        << ",\"ordered_scan_layout\":\"" << (blocked ? "blocked" :
            FINDHAP_ORDERED_LAYOUT ? "word-major" : "locus-major") << "\""
        << ",\"population_encoder\":" << FINDHAP_POP_ENCODE
        << ",\"population_conflict_backend\":" << FINDHAP_POP_CONFLICT_BACKEND
        << ",\"pedigree_conflict_backend\":" << FINDHAP_PEDIGREE_CONFLICT_BACKEND
        << ",\"pedigree_rematch\":" << FINDHAP_PEDIGREE_REMATCH
        << ",\"final_scan_neon\":" << FINDHAP_POP_FINAL_SCAN_NEON
        << ",\"final_scan_x86\":" << FINDHAP_POP_FINAL_SCAN_X86
        << ",\"library_metadata\":" << FINDHAP_LIBRARY_METADATA
        << ",\"lm_writeback_batch_bytes\":" << FINDHAP_LM_WRITEBACK_BATCH_BYTES
        << ",\"output_streams\":" << FINDHAP_OUTPUT_STREAMS << "}";
}
