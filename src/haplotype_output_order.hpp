#pragma once

namespace findhap_haplotype_metadata {
// Demand-zero metadata does not initialize the unpublished order tail. It may
// contain zeros or old original-ID -> slot mappings from prior loading. At
// library output this tail has the identity of the eagerly initialized array.
// Keep the published permutation intact. Do not use this while loading priors:
// zero there means an unmapped prior ID, and the tail is still a valid remap.
inline constexpr int output_order_id(int stored_order, int position, int published) noexcept {
    return position > published ? position : stored_order;
}
}
