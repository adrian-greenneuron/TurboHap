#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace turbohap_audit {
inline std::string json_string(const std::string& text) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
        else if (c < 32 || c >= 127) {
            out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15];
        } else out += char(c);
    }
    return out + '"';
}
inline std::string flat(std::string text) {
    for (char& c : text) if (c == '\t' || c == '\r' || c == '\n' || c == '|') c = ' ';
    return text;
}
struct Example { size_t line, column; std::string field, detail; };
struct Issue {
    std::string file, code, severity, explanation, repair;
    uint64_t occurrences = 0, affected_values = 0;
    std::vector<Example> examples;
};
struct FileSummary {
    std::string role, state = "not_checked";
    uint64_t records = 0, bytes = 0;
};
class Report {
public:
    size_t limit = 5;
    FILE* tsv = nullptr;
    std::map<std::string, Issue> issues;
    std::map<std::string, FileSummary> files;
    std::map<std::string, uint64_t> metrics;
    std::map<std::string, std::string> configuration;
    std::vector<std::string> blocked;
    uint64_t errors = 0, warnings = 0, information = 0;

    void add(const std::string& file, const std::string& code, const std::string& severity,
             size_t line, size_t column, const std::string& field, const std::string& detail,
             const std::string& explanation, const std::string& repair, uint64_t values = 1) {
        auto& item = issues[file + "/" + code];
        if (item.occurrences == 0) {
            item.file = file; item.code = code; item.severity = severity;
            item.explanation = explanation; item.repair = repair;
        }
        ++item.occurrences; item.affected_values += values;
        if (item.examples.size() < limit) item.examples.push_back({line, column, field, detail});
        if (severity == "error") ++errors;
        else if (severity == "warning") ++warnings;
        else ++information;
        if (tsv && std::fprintf(tsv,"%s\t%s\t%s\t%zu\t%zu\t%s\t%llu\t%s\t%s\n",
            severity.c_str(),flat(file).c_str(),code.c_str(),line,column,flat(field).c_str(),
            static_cast<unsigned long long>(values),flat(detail).c_str(),flat(repair).c_str())<0)
            throw std::runtime_error("failed writing detailed issue TSV");
    }
    std::string status() const { return errors ? "fail" : !blocked.empty() ? "incomplete" : warnings ? "warning" : "pass"; }
    int exit_code() const { return errors || !blocked.empty() ? 2 : 0; }
    void json(std::ostream& out) const {
        out << "{\"schema_version\":2,\"status\":" << json_string(status())
            << ",\"scope\":\"all supported input files: structural and cross-file audit\""
            << ",\"result_files_modified\":false,\"error_count\":" << errors
            << ",\"warning_count\":" << warnings << ",\"information_count\":" << information
            << ",\"counting\":\"issue occurrences; a record may violate multiple rules; affected_values counts fields or cells\""
            << ",\"examples_per_issue_limit\":" << limit << ",\"files\":{";
        bool first = true;
        for (const auto& entry : files) {
            if (!first) out << ','; first = false;
            const auto& f = entry.second;
            out << json_string(entry.first) << ":{\"role\":" << json_string(f.role)
                << ",\"state\":" << json_string(f.state) << ",\"records\":" << f.records << ",\"bytes\":" << f.bytes << '}';
        }
        out << "},\"metrics\":{"; first = true;
        for (const auto& entry : metrics) {
            if (!first) out << ','; first = false; out << json_string(entry.first) << ':' << entry.second;
        }
        out << "},\"configuration\":{"; first = true;
        for (const auto& entry : configuration) {
            if (!first) out << ','; first = false; out << json_string(entry.first) << ':' << json_string(entry.second);
        }
        out << "},\"blocked_checks\":["; first = true;
        for (const auto& reason : blocked) { if (!first) out << ','; first = false; out << json_string(reason); }
        out << "],\"limitations\":[\"No inference or automatic repairs\","
            "\"Does not establish genome assembly, biological marker order or allele coding\","
            "\"Does not perform Mendelian/genomic accuracy tests\","
            "\"Counts and locations describe the supplied files; revalidate after editing\"],\"issues\":[";
        first = true;
        for (const auto& entry : issues) {
            if (!first) out << ','; first = false;
            const auto& item = entry.second;
            out << "{\"file\":" << json_string(item.file) << ",\"code\":" << json_string(item.code)
                << ",\"severity\":" << json_string(item.severity) << ",\"occurrences\":" << item.occurrences
                << ",\"affected_values\":" << item.affected_values << ",\"explanation\":" << json_string(item.explanation)
                << ",\"suggested_action\":" << json_string(item.repair) << ",\"examples\":[";
            bool exfirst = true;
            for (const auto& ex : item.examples) {
                if (!exfirst) out << ','; exfirst = false;
                out << "{\"line\":" << ex.line << ",\"column\":" << ex.column << ",\"field\":" << json_string(ex.field)
                    << ",\"detail\":" << json_string(ex.detail) << '}';
            }
            out << "],\"examples_omitted\":" << item.occurrences - item.examples.size() << '}';
        }
        out << "]}\n";
    }
    void text(std::ostream& out, bool markdown) const {
        if (markdown) out << "# TurboHap exhaustive input audit\n\nGreen Neuron AI\n\n";
        else out << "TurboHap exhaustive input audit - Green Neuron AI\n";
        out << "Status: " << status() << ". " << errors << " error occurrences; " << warnings
            << " warnings; " << information << " informational notices.\n\n"
            << "Errors require correction. Warnings require review but do not by themselves fail the audit.\n"
            << "Counts are issue occurrences, not unique animals: one record can violate several rules.\n"
            << "Locations use physical line numbers and one-based byte columns. Column 0 means the whole record/file.\n\n";
        if (markdown) out << "## Files checked\n\n| File | Role | State | Records |\n| --- | --- | --- | ---: |\n";
        for (const auto& entry : files) {
            if (markdown) out << "| " << entry.first << " | " << entry.second.role << " | " << entry.second.state << " | " << entry.second.records << " |\n";
            else out << entry.first << ": " << entry.second.state << "; " << entry.second.records << " records\n";
        }
        out << '\n';
        if (markdown) out << "## Issue summary\n\n| Severity | File | Check | Occurrences | Affected fields/cells |\n| --- | --- | --- | ---: | ---: |\n";
        for (const auto& entry : issues) {
            const auto& i = entry.second;
            if (markdown) out << "| " << i.severity << " | " << i.file << " | " << i.code << " | " << i.occurrences << " | " << i.affected_values << " |\n";
            else out << i.severity << " " << i.file << " / " << i.code << ": " << i.occurrences << " occurrences, " << i.affected_values << " affected fields/cells\n";
        }
        if (issues.empty()) out << "No issues found by the implemented checks.\n";
        out << '\n';
        for (const auto& entry : issues) {
            const auto& i = entry.second;
            out << (markdown ? "### " : "") << i.severity << ": " << i.file << " / " << i.code << '\n'
                << (markdown ? "\n" : "") << i.explanation << "\n\nSuggested action: " << i.repair << "\n\n";
            for (const auto& e : i.examples)
                out << "- " << i.file << " line " << e.line << ", column " << e.column << " [" << flat(e.field) << "]: " << flat(e.detail) << '\n';
            if (i.occurrences > i.examples.size()) out << "  " << i.occurrences - i.examples.size() << " further examples omitted from this summary.\n";
            out << '\n';
        }
        if (!blocked.empty()) {
            out << (markdown ? "## Checks blocked by invalid dependencies\n\n" : "Blocked checks:\n");
            for (const auto& reason : blocked) out << "- " << reason << '\n';
        }
        if (markdown) out << "\n## Dataset counts\n\n| Metric | Count |\n| --- | ---: |\n";
        for (const auto& metric : metrics)
            out << (markdown ? "| " : "") << metric.first << (markdown ? " | " : ": ") << metric.second << (markdown ? " |\n" : "\n");
        if (!configuration.empty()) {
            out << (markdown ? "\n## Parsed run settings\n\n| Setting | Value |\n| --- | --- |\n" : "\nParsed run settings:\n");
            for (const auto& entry : configuration)
                out << (markdown ? "| " : "") << entry.first << (markdown ? " | " : ": ") << flat(entry.second) << (markdown ? " |\n" : "\n");
        }
        out << "\nThis is a structural and cross-file audit, not a biological accuracy certificate.\n"
            << "Genome assembly, true marker order, allele coding and Mendelian/genomic consistency are not established.\n"
            << "No inputs or inference result files were changed. Reports can reveal private cohort properties; keep them private.\n";
    }
};
} // namespace turbohap_audit
