#include "../src/map_validation.hpp"

#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

using findhap::input::MapReader;
using findhap::input::MapRecord;

struct File {
    FILE* value = std::tmpfile();
    explicit File(const std::string& contents) {
        assert(value);
        assert(std::fwrite(contents.data(), 1, contents.size(), value) == contents.size());
        std::rewind(value);
    }
    ~File() { std::fclose(value); }
};

void rejected(const std::string& rows, const std::string& field,
              const std::string& line = "line 2") {
    File file("header\n" + rows);
    bool failed = false;
    try {
        MapReader reader(file.value);
        MapRecord record;
        while (reader.read(record)) {}
    } catch (const std::runtime_error& error) {
        const std::string message(error.what());
        assert(message.find("chromosome.data") != std::string::npos);
        assert(message.find(", column ") != std::string::npos);
        assert(message.find(field) != std::string::npos);
        assert(message.find(line) != std::string::npos);
        failed = true;
    }
    assert(failed);
}

int main(int argc, char** argv) {
    {
        File file("\r\nA 1 1 1 100 1 1\n\r\n  \t\n");
        MapReader reader(file.value);
        MapRecord record;
        assert(reader.read(record));
        assert(!reader.read(record));
    }
    {
        // Shifted chromosome numbers, gaps, duplicate positions, missing chip
        // membership, CRLF, a leading plus and EOF without LF are all valid.
        File file("header\r\nA 5 1 1 0 2 1 0\r\nB 5 2 2 0 2 2 1\r\n"
                  "C 5 3 3 100 2 0 2\r\nD 9 +1 4 2147483647 2 3 0");
        MapReader reader(file.value);
        MapRecord record;
        std::vector<int> chroms;
        while (reader.read(record)) chroms.push_back(record.chromosome);
        assert((chroms == std::vector<int>{5, 5, 5, 9}));
        assert(reader.record_count() == 4);
        assert(reader.line_number() == 5);
        assert(reader.chip_count() == 2);
        assert((reader.chip_counts() == std::vector<int>{3, 2}));
        assert(record.marker == "D" && record.position == INT_MAX);
        assert(!reader.read(record));
    }
    rejected("", "no markers", "line 1");
    rejected("\n", "blank records");
    rejected("A 1 1 1 100 1 1\n\n\nB 1 2 2 200 1 2\n", "blank records", "line 3");
    rejected("A 1 1", "overall");
    rejected("A 1 1 1 100 2 1\nB 1 2 2 200 2 2 1\n", "chip columns");
    rejected("A 1 1 1 100 1 1 9\n", "chip columns");
    rejected("A 1 1 1 100 2147483647 1\n", "chip columns");
    rejected("A 1 1 1 100 0\n", "n_chips");
    rejected("A 1 1 1 100 1 1\nB 1 2 2 200 2 2 1\n", "n_chips", "line 3");
    rejected("A 1 1 1 100 1 2147483647\n", "chip 1");
    rejected("A 1 1 1 100 1 -1\n", "chip 1");
    rejected("A 1 1 1 100 1 1\nB 1 2 2 200 1 1\n", "chip 1", "line 3");
    rejected("A 1 1 1 100 1 0\nB 1 2 2 200 1 2\n", "chip 1", "line 3");
    rejected("A 0 1 1 100 1 1\n", "chromosome");
    rejected("A -1 1 1 100 1 1\n", "chromosome");
    rejected("A 2147483647 1 1 100 1 1\n", "chromosome");
    rejected("A 1 0 1 100 1 1\n", "within");
    rejected("A 1 1 0 100 1 1\n", "overall");
    rejected("A 1 1 2 100 1 1\n", "overall");
    rejected("A 1 1 1 -1 1 1\n", "position");
    rejected("A 1 1 1 2147483648 1 1\n", "position");
    rejected("A 1 1 1 1x 1 1\n", "position");
    rejected("A 1 1 1 + 1 1\n", "position");
    rejected("A 1 1 1 +-1 1 1\n", "position");
    rejected("A 1 1 1 1e3 1 1\n", "position");
    rejected("A 1 1 1 100 1 1\nB 1 2 2 99 1 2\n", "position", "line 3");
    rejected("A 2 1 1 100 1 1\nB 1 1 2 200 1 2\n", "chromosome", "line 3");
    rejected("A 1 1 1 100 1 1\nB 1 3 2 200 1 2\n", "within", "line 3");
    rejected("A 1 1 1 100 1 1\nB 2 2 2 200 1 2\n", "within", "line 3");
    rejected(std::string(51, 'A') + " 1 1 1 100 1 1\n", "marker");
    rejected(std::string("A 1 1 1 100 1 1") + '\0' + "junk\n", "NUL");
    rejected(std::string(MapReader::max_record_bytes + 1, 'A') + "\n", "1 MiB");
    {
        File file("header\n  A 1 1 1  100x  1 1\n");
        try {
            MapReader reader(file.value);
            MapRecord record;
            reader.read(record);
            assert(false);
        } catch (const std::runtime_error& e) {
            assert(std::string(e.what()).find("line 2, column 12, position:") != std::string::npos);
        }
    }
    {
        File empty("");
        try { MapReader reader(empty.value); assert(false); }
        catch (const std::runtime_error& e) { assert(std::string(e.what()).find("header") != std::string::npos); }
    }
    {
        // A failed row must not expose invalid chip indices to the caller.
        File file("header\nA 1 1 1 100 1 1\nB 1 2 2 200 1 99999\n");
        MapReader reader(file.value);
        MapRecord record;
        assert(reader.read(record));
        try { reader.read(record); assert(false); }
        catch (const std::runtime_error&) {}
        assert(record.marker == "A" && reader.record_count() == 1);
        assert(reader.chip_counts()[0] == 1);
    }
    if (argc > 1) {
        FILE* example = std::fopen(argv[1], "rb");
        assert(example);
        MapReader reader(example);
        MapRecord record;
        while (reader.read(record)) {}
        assert(reader.record_count() > 0);
        std::fclose(example);
    }
    std::cout << "map validation tests passed\n";
}
