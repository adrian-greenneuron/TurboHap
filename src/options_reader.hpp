#pragma once

#include <array>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <istream>
#include <stdexcept>
#include <string>

namespace findhap {

inline bool input_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

inline int input_integer(const std::string& token) {
    size_t i = (!token.empty() && (token[0] == '+' || token[0] == '-')) ? 1 : 0;
    if (i == token.size()) throw std::runtime_error("missing integer");
    for (size_t j = i; j < token.size(); ++j)
        if (token[j] < '0' || token[j] > '9') throw std::runtime_error("invalid integer: " + token);
    char* end;
    errno = 0;
    long value = std::strtol(token.c_str(), &end, 10);
    if (errno == ERANGE || value < INT_MIN || value > INT_MAX)
        throw std::runtime_error("integer out of range: " + token);
    return static_cast<int>(value);
}

inline float input_real(std::string token) {
    // List-directed REAL accepts D exponents and exponents without a letter.
    for (char& c : token) if (c == 'D' || c == 'd' || c == 'E') c = 'e';
    if (token.find('e') == std::string::npos) {
        size_t sign = token.find_first_of("+-", 1);
        if (sign != std::string::npos) token.insert(sign, "e");
    }
    size_t i = (!token.empty() && (token[0] == '+' || token[0] == '-')) ? 1 : 0;
    std::string special = token.substr(i);
    for (char& c : special) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (special != "inf" && special != "infinity" && special != "nan") {
        size_t digits = 0;
        while (i < token.size() && token[i] >= '0' && token[i] <= '9') { ++i; ++digits; }
        if (i < token.size() && token[i] == '.') {
            ++i;
            while (i < token.size() && token[i] >= '0' && token[i] <= '9') { ++i; ++digits; }
        }
        if (!digits) throw std::runtime_error("invalid real: " + token);
        if (i < token.size() && token[i] == 'e') {
            ++i;
            if (i < token.size() && (token[i] == '+' || token[i] == '-')) ++i;
            size_t start = i;
            while (i < token.size() && token[i] >= '0' && token[i] <= '9') ++i;
            if (i == start) throw std::runtime_error("missing real exponent: " + token);
        }
        if (i != token.size()) throw std::runtime_error("invalid real: " + token);
    }
    char* end;
    float value = std::strtof(token.c_str(), &end);
    if (end != token.c_str() + token.size()) throw std::runtime_error("invalid real: " + token);
    return value;
}

// One Fortran list-directed READ. getline consumes each physical record once;
// destroying the reader discards unused values on its last record, as READ does.
class NumericRead {
    std::istream& input;
    std::string record, repeated;
    size_t position = 0;
    int remaining = 0;
    bool after_value = false;

    void whitespace() {
        for (;;) {
            while (position < record.size() && input_space(record[position])) ++position;
            if (position < record.size()) return;
            if (!std::getline(input, record)) throw std::runtime_error("unexpected end of options record");
            position = 0;
        }
    }

public:
    explicit NumericRead(std::istream& stream) : input(stream) {}

    std::string next() {
        if (remaining) { --remaining; return repeated; }
        whitespace();
        if (after_value && record[position] == ',') { ++position; whitespace(); }
        // Null values and premature '/' leave mandatory variables undefined in
        // the original; reject them rather than manufacturing defaults.
        if (record[position] == ',' || record[position] == '/')
            throw std::runtime_error("missing mandatory option value");
        size_t start = position;
        while (position < record.size() && !input_space(record[position]) &&
               record[position] != ',' && record[position] != '/') ++position;
        std::string token = record.substr(start, position - start);
        size_t star = token.find('*');
        if (star != std::string::npos) {
            std::string count = token.substr(0, star);
            if (count.empty() || count[0] < '0' || count[0] > '9')
                throw std::runtime_error("invalid repetition count");
            remaining = input_integer(count);
            if (remaining < 1) throw std::runtime_error("invalid repetition count");
            --remaining;
            token = token.substr(star + 1);
            if (token.empty()) throw std::runtime_error("null repeated mandatory option");
            repeated = token;
        }
        after_value = true;
        return token;
    }
};

struct Options {
    std::array<int, 10> controls;
    float errate;
};

inline Options read_options(std::istream& input) {
    Options options;
    {
        NumericRead values(input);
        for (int& value : options.controls) value = input_integer(values.next());
    }
    std::string ignored;
    if (!std::getline(input, ignored) || !std::getline(input, ignored))
        throw std::runtime_error("missing skipped option records");
    NumericRead error_rate(input);
    options.errate = input_real(error_rate.next());
    return options;
}

inline int thread_argument(const char* argument) {
    // The baseline GET_COMMAND_ARGUMENT destination is CHARACTER*2.
    std::string text(argument, std::char_traits<char>::length(argument) < 2 ?
                     std::char_traits<char>::length(argument) : 2);
    text.resize(2, ' ');
    if (text[1] == ' ') text = "0" + text.substr(0, 1);
    size_t first = text.find_first_not_of(" \t");
    text = first == std::string::npos ? "" : text.substr(first);
    size_t end = text.find_first_of(" ,\t/");
    if (end != std::string::npos) text.resize(end);
    int count = input_integer(text);
    if (count < 1) throw std::runtime_error("thread count must be positive");
    return count;
}

} // namespace findhap
