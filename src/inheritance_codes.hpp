#pragma once

#include <cstdint>

namespace findhap::inheritance {
enum class Code : uint8_t {
    no_parent,
    sire_without_genotype,
    dam_without_genotype,
    unmatched,
    first_match,
    second_match,
    crossover_to_first,
    crossover_to_second,
    first_discordant,
    second_discordant,
    both_match,
    ambiguous,
    resolved_first,
    resolved_second,
    maternal_x,
};

struct Definition {
    char text[2];
    uint8_t from_preceding, from_following;
};

inline constexpr Definition definitions[] = {
    {{'.', ' '}, 0, 0}, // no_parent
    {{'p', ' '}, 0, 0}, // sire_without_genotype
    {{'m', ' '}, 0, 0}, // dam_without_genotype
    {{' ', '0'}, 0, 0}, // unmatched
    {{' ', '1'}, 1, 1}, // first_match
    {{' ', '2'}, 2, 2}, // second_match
    {{'X', '1'}, 1, 2}, // crossover_to_first
    {{'X', '2'}, 2, 1}, // crossover_to_second
    {{'-', '1'}, 1, 1}, // first_discordant
    {{'-', '2'}, 2, 2}, // second_discordant
    {{' ', 'H'}, 0, 0}, // both_match
    {{'?', '?'}, 0, 0}, // ambiguous
    {{'?', '1'}, 0, 0}, // resolved_first
    {{'?', '2'}, 0, 0}, // resolved_second
    {{' ', 'Y'}, 0, 0}, // maternal_x
};
static_assert(sizeof(Code) == 1);
static_assert(sizeof(definitions) / sizeof(Definition) == static_cast<unsigned>(Code::maternal_x) + 1);

struct BytePair {
    Code states[2];

    constexpr Code get(unsigned role) const noexcept { return states[role]; }
    constexpr void set(unsigned role, Code code) noexcept { states[role] = code; }
    static constexpr BytePair both(Code code) noexcept { return {{code, code}}; }
};

struct NibblePair {
    uint8_t states;

    constexpr Code get(unsigned role) const noexcept {
        return static_cast<Code>((states >> (4 * role)) & 0xf);
    }

    constexpr void set(unsigned role, Code code) noexcept {
        const unsigned shift = 4 * role;
        states = static_cast<uint8_t>((states & ~(0xfu << shift)) |
                                      (static_cast<unsigned>(code) << shift));
    }

    static constexpr NibblePair both(Code code) noexcept {
        return {static_cast<uint8_t>(static_cast<unsigned>(code) * 0x11u)};
    }
};

static_assert(static_cast<unsigned>(Code::maternal_x) < 16);
static_assert(sizeof(BytePair) == 2 && sizeof(NibblePair) == 1);

inline constexpr Code match_codes[] = {
    Code::first_match, Code::second_match, Code::crossover_to_second,
    Code::crossover_to_first, Code::first_discordant, Code::second_discordant, Code::both_match,
};

constexpr const Definition& definition(Code code) noexcept {
    return definitions[static_cast<uint8_t>(code)];
}

constexpr int orientation(Code preceding, Code following) noexcept {
    const int left = definition(preceding).from_preceding;
    const int right = definition(following).from_following;
    if (left && right && left != right) return 0;
    return left ? left : right;
}
}
