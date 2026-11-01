# The buffer policy uses portable POSIX mappings, not M4-specific instructions.
# Select it for native Apple Silicon independently of the measured M4 CPU
# optimisations, compiler or OpenMP runtime. Performance evidence remains M4 Pro.
set(FINDHAP_NATIVE_APPLE_SILICON_MEMORY FALSE)
if(APPLE AND NOT CMAKE_CROSSCOMPILING AND
        CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$" AND
        (NOT CMAKE_OSX_ARCHITECTURES OR "${CMAKE_OSX_ARCHITECTURES}" STREQUAL "arm64"))
    set(FINDHAP_NATIVE_APPLE_SILICON_MEMORY TRUE)
endif()
option(FINDHAP_MMAP_PHASE_BUFFERS "Use explicit mappings for large temporary phase buffers"
    ${FINDHAP_NATIVE_APPLE_SILICON_MEMORY})

# A cached/explicit OFF keeps the original policy, including GNU LTO. Never
# FORCE an old build directory onto new defaults or override caller choices.
set(FINDHAP_MAC_MEMORY_DEFAULTS FALSE)
set(FINDHAP_DEFAULT_GNU_LTO_PARTITION "1to1")
if(FINDHAP_NATIVE_APPLE_SILICON_MEMORY AND FINDHAP_MMAP_PHASE_BUFFERS)
    set(FINDHAP_MAC_MEMORY_DEFAULTS TRUE)
    set(FINDHAP_DEFAULT_GNU_LTO_PARTITION "one")
endif()
set(FINDHAP_GNU_LTO_PARTITION "${FINDHAP_DEFAULT_GNU_LTO_PARTITION}"
    CACHE STRING "GNU executable LTO partition policy")
