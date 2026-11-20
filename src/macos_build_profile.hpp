#pragma once
// The native Apple Silicon engine was exercised on M1 Pro, M4 Pro and M5.
// Every feature remains individually overridable. No core count is compiled in.
#if defined(__APPLE__) && defined(__aarch64__) && defined(__ARM_NEON) && \
    ((defined(FINDHAP_NATIVE_APPLE_M4PRO) && FINDHAP_NATIVE_APPLE_M4PRO) || \
     (defined(FINDHAP_APPLE_SILICON_COHORT) && FINDHAP_APPLE_SILICON_COHORT))
#define TURBOHAP_APPLE_M4_PROFILE 1
// Explicit fixture and caller settings take priority over the cohort default.
#if defined(FINDHAP_M4_COHORT_ORDERED_SCAN_ORDER) && !defined(FINDHAP_ORDERED_SCAN_ORDER)
#define FINDHAP_ORDERED_SCAN_ORDER FINDHAP_M4_COHORT_ORDERED_SCAN_ORDER
#endif
#ifndef FINDHAP_ORDERED_LAYOUT
#define FINDHAP_ORDERED_LAYOUT 1
#endif
#ifndef FINDHAP_POP_ENCODE
#define FINDHAP_POP_ENCODE 5
#endif
#ifndef FINDHAP_POP_FULL
#define FINDHAP_POP_FULL 2
#endif
#ifndef FINDHAP_PEDIGREE_ORDERED_FINAL
#define FINDHAP_PEDIGREE_ORDERED_FINAL 1
#endif
#ifndef FINDHAP_HAPLIST_INIT
#define FINDHAP_HAPLIST_INIT 3
#endif
#ifndef FINDHAP_LIBRARY_FORMAT
#define FINDHAP_LIBRARY_FORMAT 2
#endif
#ifndef FINDHAP_OUTPUT_BATCH_WRITE
#define FINDHAP_OUTPUT_BATCH_WRITE 1
#endif
#ifndef FINDHAP_IO_BUFFER_BYTES
#define FINDHAP_IO_BUFFER_BYTES 65536
#endif
#else
#define TURBOHAP_APPLE_M4_PROFILE 0
#endif
// Waiting and heterogeneous rows are enabled only after their separate trials.
#ifndef FINDHAP_MAC_BLOCKTIME_MS
#define FINDHAP_MAC_BLOCKTIME_MS -1
#endif
#ifndef FINDHAP_MAC_DYNAMIC_ROWS
#define FINDHAP_MAC_DYNAMIC_ROWS 0
#endif
