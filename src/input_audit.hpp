#pragma once
#include "input_audit_report.hpp"
#include "options_reader.hpp"
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace turbohap_audit {
namespace fs = std::filesystem;
inline bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f'; }
inline std::string_view trim(std::string_view text) {
    while (!text.empty() && space(text.front())) text.remove_prefix(1);
    while (!text.empty() && space(text.back())) text.remove_suffix(1);
    return text;
}
struct Token { std::string_view text; size_t column = 0; };
inline std::vector<Token> tokens(const std::string& line) {
    std::vector<Token> fields;
    for (size_t i = 0; i < line.size();) {
        while (i < line.size() && space(line[i])) ++i;
        size_t begin = i;
        while (i < line.size() && !space(line[i])) ++i;
        if (i > begin) fields.push_back({std::string_view(line).substr(begin, i - begin), begin + 1});
    }
    return fields;
}
inline bool integer(std::string_view text, int& value) {
    text = trim(text);
    if (!text.empty() && text.front() == '+') { text.remove_prefix(1); if (text.empty() || text.front() < '0' || text.front() > '9') return false; }
    auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return !text.empty() && result.ec == std::errc{} && result.ptr == text.data() + text.size();
}
class Lines {
    FILE* file_ = nullptr;
    Report& report_;
    std::string name_;
    size_t limit_, begin_ = 0, end_ = 0;
    std::array<char,65536> buffer_{};
    bool failed_ = false;
public:
    size_t line = 0;
    bool oversized = false;
    Lines(Report& report, const fs::path& directory, std::string name, size_t limit = 1024*1024)
        : report_(report), name_(std::move(name)), limit_(limit) {
        file_ = std::fopen((directory / name_).c_str(), "rb");
        if (!file_) report_.add(name_, "unreadable", "error", 0, 0, "file", "File cannot be opened for reading.",
            "The required input or active prior file is missing or unreadable.", "Check the filename, permissions and supplied directory.");
        report_.files[name_].state = file_ ? "checked" : "unreadable";
    }
    ~Lines() { if (file_) std::fclose(file_); }
    explicit operator bool() const { return file_ != nullptr; }
    bool next(std::string& text) {
        text.clear(); oversized = false;
        if (!file_ || failed_) return false;
        bool newline = false, any = false;
        while (true) {
            if (begin_ == end_) {
                end_ = std::fread(buffer_.data(), 1, buffer_.size(), file_); begin_ = 0;
                report_.files[name_].bytes += end_;
                if (std::ferror(file_)) {
                    report_.add(name_, "read_error", "error", line + 1, 0, "file", "I/O error while reading input.",
                        "The complete file could not be examined.", "Check storage and permissions, then rerun the audit.");
                    report_.files[name_].state = "incomplete"; failed_ = true; return false;
                }
                if (!end_) break;
            }
            const char* start = buffer_.data() + begin_;
            const char* stop = static_cast<const char*>(std::memchr(start, '\n', end_ - begin_));
            size_t count = stop ? size_t(stop-start) : end_-begin_;
            any = true;
            if (count <= limit_ - text.size() && !oversized) text.append(start,count);
            else oversized = true;
            begin_ += count;
            if (stop) { ++begin_; newline = true; break; }
        }
        if (!any) return false;
        ++line; ++report_.files[name_].records;
        if (newline && !text.empty() && text.back() == '\r') text.pop_back();
        if (oversized) report_.add(name_, "record_too_long", "error", line, limit_+1, "record",
            "Record exceeds the bounded parser limit of " + std::to_string(limit_) + " bytes.",
            "A missing newline or unexpectedly long record prevents safe parsing.", "Restore record separators and check declared lengths. The remainder of this row was consumed without storing it.");
        size_t nul_count = std::count(text.begin(),text.end(),'\0');
        if (nul_count) report_.add(name_, "nul_byte", "error", line, text.find('\0')+1, "record",
            std::to_string(nul_count) + " embedded NUL bytes.", "Input files must be plain text; NUL bytes can conceal fields.",
            "Re-export as plain ASCII text; do not remove bytes without checking the export.", nul_count);
        return true;
    }
};
struct Animal {
    int id = 0, sire = 0, dam = 0, birth = 0;
    size_t line = 0, genotype_line = 0;
    char sex = '?';
    bool full_date = false;
};
struct MapState {
    bool usable = false;
    std::vector<int> chromosomes, chip_lengths;
    std::map<int, std::pair<int,int>> bounds;
};
struct Segment {
    int chromosome = 0, start = 0, length = 0;
    size_t line = 0;
    double cumulative = -1;
    double frequency_sum = 0;
    std::unordered_set<int> haplotypes;
};
class Audit {
public:
    Report& report;
    fs::path directory;
    findhap::Options options{};
    bool options_ok = false, pedigree_ok = false;
    MapState map;
    std::vector<Animal> animals;
    std::unordered_map<int,size_t> animal_index;
    std::map<int,std::map<int,Segment>> library;
    std::map<std::string,std::pair<uintmax_t,fs::file_time_type>> stamps;
    explicit Audit(Report& r, fs::path dir) : report(r), directory(std::move(dir)) {}
    void options_file();
    void map_file();
    void pedigree_file();
    void pedigree_links();
    void genotype_file();
    void prior_files();
    void issue(const std::string& file,const std::string& code,const std::string& severity,size_t line,size_t column,
               const std::string& field,const std::string& detail,const std::string& why,const std::string& action,uint64_t count=1) {
        report.add(file,code,severity,line,column,field,detail,why,action,count);
    }
    bool numeric(const std::string& file,size_t line,Token token,const std::string& field,int& value) {
        if (integer(token.text,value)) return true;
        issue(file,"invalid_integer","error",line,token.column,field,"Expected a complete signed 32-bit decimal integer.",
              "A missing, malformed or overflowing numeric field cannot be read reliably.","Correct this field in the source export; do not insert or delete fixed-width columns.");
        return false;
    }
    void inventory() {
        for (const char* name : {"findhap.options","pedigree.file","chromosome.data","genotypes.txt","hap.listpriors","hap.foundpriors","hap.Xseg"}) {
            auto& file = report.files[name];
            file.role = (std::string(name).rfind("hap.",0)==0) ? "optional prior" : "required input";
            std::error_code ec;
            auto path = directory/name;
            if (fs::exists(path,ec)) {
                if (!fs::is_regular_file(path,ec)) issue(name,"not_regular_file","error",0,0,"file","Input path is not a regular file.",
                    "The audit requires stable seekable files, including symlinks to regular files.","Supply an ordinary file rather than a directory, FIFO or device.");
                else stamps[name] = {fs::file_size(path),fs::last_write_time(path)};
            } else file.state = file.role == "optional prior" ? "absent_optional" : "missing";
        }
    }
    bool regular(const std::string& name) const { return stamps.count(name) != 0; }
    void run() {
        inventory(); options_file(); map_file(); pedigree_file(); genotype_file(); prior_files();
        for (const auto& entry : stamps) {
            std::error_code ec;
            const auto path = directory/entry.first;
            const auto size = fs::file_size(path,ec);
            const auto time = fs::last_write_time(path,ec);
            if (ec || size != entry.second.first || time != entry.second.second)
                issue(entry.first,"input_changed","error",0,0,"file","Input size or modification time changed during the audit.",
                    "The report may combine different input versions.","Stop writers and rerun on a stable input copy.");
        }
        for(const auto& item:report.issues) {
            auto& file=report.files[item.second.file];
            if(file.state=="checked"&&(item.second.severity=="error"||item.second.severity=="warning"))
                file.state=item.second.severity=="error"?"checked_with_errors":"checked_with_warnings";
            else if(file.state=="checked_with_warnings"&&item.second.severity=="error")file.state="checked_with_errors";
        }
    }
};

inline void Audit::options_file() {
    const std::string file="findhap.options";
    if (report.files[file].state != "missing" && !regular(file)) return;
    Lines reader(report,directory,file);
    std::vector<std::string> rows; std::string row,whole;
    bool too_large=false;
    while (reader.next(row)) {
        if(whole.size()+row.size()+1>16*1024*1024)too_large=true;
        if(!too_large){rows.push_back(row);whole+=row+'\n';}
    }
    if (!reader) return;
    if (too_large) { issue(file,"options_too_large","error",0,0,"file","Options text exceeds 16 MiB.",
        "This is unlikely to be the options file and retaining it would waste memory.","Check the selected file and remove unrelated data.");return; }
    size_t line=0,pos=0,last_line=0; int repeats=0; std::string_view repeated;
    auto next=[&](Token& result,size_t& physical)->bool {
        if (repeats) { --repeats; result.text=repeated; physical=last_line+1;return true; }
        for (;;) {
            if (line>=rows.size()) return false;
            while (pos<rows[line].size() && space(rows[line][pos])) ++pos;
            if (pos==rows[line].size()) {++line;pos=0;continue;}
            if (rows[line][pos]==',') {++pos;continue;}
            break;
        }
        size_t start=pos;
        while(pos<rows[line].size()&&!space(rows[line][pos])&&rows[line][pos]!=','&&rows[line][pos]!='/')++pos;
        result={std::string_view(rows[line]).substr(start,pos-start),start+1};physical=line+1;last_line=line;
        if (pos==start) ++pos;
        size_t star=result.text.find('*');
        if(star!=std::string_view::npos) {
            int count=0;
            if(integer(result.text.substr(0,star),count)&&count>0) {
                repeats=count-1; repeated=result.text.substr(star+1);result.text=repeated;result.column+=star+1;
            }
        }
        return true;
    };
    constexpr const char* names[]={"iters","XChrom","maxlen","minlen","steps","maxhap","hapout","genout","damout","listout"};
    const auto before=report.errors;
    std::array<bool,10> valid{};
    std::array<Example,10> locations{};
    bool missing=false;
    for(size_t i=0;i<10;++i) {
        Token token;size_t physical=0;
        if(!next(token,physical)) { missing=true;issue(file,"missing_controls","error",rows.size()+1,1,names[i],"Fewer than ten mandatory integer controls.",
            "The first list-directed read requires ten control values.","Supply all ten controls in the documented order.",10-i);break; }
        locations[i]={physical,token.column,names[i],""};valid[i]=numeric(file,physical,token,names[i],options.controls[i]);
    }
    auto bad=[&](size_t i,const std::string& detail,const std::string& action,const std::string& severity="error") {
        issue(file,"option_"+std::string(names[i]),severity,locations[i].line,locations[i].column,names[i],detail,
              "This option value is outside its documented domain or needs review.",action);
    };
    for(size_t i : {size_t(0),size_t(2),size_t(3),size_t(4),size_t(5)})
        if(valid[i]&&options.controls[i]<1) bad(i,"Value must be positive.","Use a positive integer.");
    if(valid[1]&&options.controls[1]<0)bad(1,"X chromosome cannot be negative.","Use 0 for no X chromosome, or its map chromosome code.");
    for(size_t i : {size_t(6),size_t(8)}) if(valid[i]&&(options.controls[i]<0||options.controls[i]>2))bad(i,"Allowed values are 0, 1 and 2.","Use a documented output/imputation selector.");
    if(valid[7]&&options.controls[7]!=0&&options.controls[7]!=1&&options.controls[7]!=2&&options.controls[7]!=5)bad(7,"Allowed values are 0, 1, 2 and 5.","Select the intended genotype output coding explicitly.");
    if(valid[9]&&(options.controls[9]<-1||options.controls[9]>1))bad(9,"Allowed values are -1, 0 and 1.","Use -1 for the last step only, 0 for no library outputs, or 1 for all steps.");
    if(valid[2]&&valid[3]&&options.controls[2]<options.controls[3])bad(2,"The schedule increases from maxlen to minlen; the engine supports this but the naming is unusual.","Confirm that an increasing segment schedule is intentional.","warning");
    if(!missing) {
        line=last_line+3;pos=0;repeats=0;
        Token token;size_t physical=0;
        if(!next(token,physical))issue(file,"missing_error_rate","error",rows.size()+1,1,"errate","Missing error rate after the two skipped records.",
            "The engine skips two physical records after the controls before reading errate.","Restore the labels/blank records and supply a finite error rate between 0 and 1.");
        else {
            try {
                options.errate=findhap::input_real(std::string(token.text));
                if(!std::isfinite(options.errate)||options.errate<0||options.errate>1)throw std::runtime_error("range");
                if(options.errate>.1f)issue(file,"large_error_rate","warning",physical,token.column,"errate","More than 10% mismatches permitted.",
                    "An unusually high mismatch fraction may reduce matching specificity.","Check that a fraction, rather than a percentage, was supplied.");
            }catch(const std::exception&) {issue(file,"invalid_error_rate","error",physical,token.column,"errate","Expected a finite error-rate fraction between 0 and 1.",
                "NaN, infinity, malformed values and fractions outside 0..1 cannot be accepted as a valid configuration.","Correct the error rate; for example .004 means 0.4%.");}
        }
    }
    if(report.errors==before) {
        try {
            std::istringstream stream(whole);options=findhap::read_options(stream);options_ok=true;
            for(size_t i=0;i<10;++i)report.configuration[names[i]]=std::to_string(options.controls[i]);
            std::ostringstream rate;rate<<options.errate;report.configuration["errate"]=rate.str();
        }
        catch(const std::exception& error) {issue(file,"engine_options_parse","error",0,0,"record",error.what(),
            "The options do not match the engine's exact list-directed read semantics.","Check commas, null fields, repetitions and the two skipped physical records.");}
    }
    if(options_ok) {
        const int steps=options.controls[2]==options.controls[3]?1:options.controls[4];
        report.metrics["effective_steps"]=steps;
        if(options.controls[5]==INT_MAX) {
            bad(5,"maxhap leaves no room for the engine's terminal index.","Use a positive maxhap below INT_MAX.");options_ok=false;
        }
        if(steps>100000) {
            bad(4,"More than 100,000 effective steps cannot be exhaustively certified within the bounded audit.","Check that the intended step count was supplied.");options_ok=false;
        } else {
            const float ratio=steps>1?std::pow(float(options.controls[2])/options.controls[3],1.0f/(steps-1)):1.0f;
            int length=options.controls[2],shortest=length,longest=length;
            for(int step=1;step<steps;++step) {
                const float next=std::round(float(length)/ratio);
                if(!std::isfinite(next)||next<1||next>=2147483648.0f) {
                    issue(file,"invalid_rounded_schedule","error",0,0,"segment schedule","Rounded segment length becomes invalid at step "+std::to_string(step+1)+".",
                        "The engine's actual rounded schedule must contain finite positive 32-bit lengths.","Correct maxlen, minlen and steps together.");options_ok=false;break;
                }
                length=int(next);shortest=std::min(shortest,length);longest=std::max(longest,length);
            }
            if(options_ok){report.metrics["shortest_segment_target"]=shortest;report.metrics["largest_segment_target"]=longest;}
        }
    }
}

inline void Audit::map_file() {
    const std::string file="chromosome.data";
    if(report.files[file].state!="missing"&&!regular(file))return;
    Lines reader(report,directory,file);std::string row;
    auto before=report.errors;
    if(!reader)return;
    if(!reader.next(row)){issue(file,"empty_map","error",1,1,"header","Map file is empty.","A header followed by marker records is required.","Supply the complete chromosome.data file.");return;}
    auto head=tokens(row);int possible;
    if(head.size()>=6&&integer(head[1].text,possible))issue(file,"missing_map_header","error",1,1,"header","First row resembles a marker record and would be discarded as the header.",
        "The engine always ignores the first physical map record.","Add the intended header without discarding the first marker.");
    int last_chr=0,last_within=0,last_position=0,chips=0;
    size_t first_blank=0,blanks=0,count=0;
    std::unordered_map<std::string,size_t> labels;
    while(reader.next(row)) {
        if(reader.oversized)continue;
        auto f=tokens(row);
        if(f.empty()){if(!first_blank)first_blank=reader.line;++blanks;continue;}
        if(first_blank){issue(file,"internal_blank_map_record","error",first_blank,1,"record",std::to_string(blanks)+" blank records before another marker.",
            "Only trailing blank map records are accepted by the engine.","Remove internal blank records and preserve marker order.",blanks);first_blank=0;blanks=0;}
        ++count;
        if(f.size()<6){issue(file,"truncated_map_record","error",reader.line,row.size()+1,"record","Fewer than six mandatory fields.",
            "Each map row needs marker, chromosome, within, overall, position, chip count and chip ordinals.","Re-export the complete marker record.");continue;}
        std::string marker(f[0].text);
        if(marker.size()>50)issue(file,"marker_name_length","error",reader.line,f[0].column,"marker","Marker name exceeds 50 bytes.",
            "The engine's documented marker-name field is limited to 50 bytes.","Use unique marker names within the supported width.");
        auto inserted=labels.emplace(marker,reader.line);
        if(!inserted.second)issue(file,"duplicate_marker_name","warning",reader.line,f[0].column,"marker","Label also appears on line "+std::to_string(inserted.first->second)+".",
            "Duplicate labels make marker identification ambiguous even though the engine uses row positions.","Confirm whether these are distinct markers and provide unique labels where appropriate.");
        int values[5]{};bool good=true;
        const char* names[]={"chromosome","within","overall","position","n_chips"};
        for(int i=0;i<5;++i)good=numeric(file,reader.line,f[i+1],names[i],values[i])&&good;
        if(!good)continue;
        int chr=values[0],within=values[1],overall=values[2],position=values[3],nchips=values[4];
        if(chr<1||chr==INT_MAX||within<1||overall<1||position<0||nchips<1){issue(file,"map_numeric_domain","error",reader.line,1,"record","Nonpositive index/chip count, negative position or unsupported chromosome index.",
            "Map indices must fit the engine's positive indexing domain; positions may be zero.","Correct numeric fields using the original map export.");continue;}
        if(size_t(nchips)!=f.size()-6){issue(file,"map_chip_columns","error",reader.line,1,"n_chips","Declared "+std::to_string(nchips)+" chips but found "+std::to_string(f.size()-6)+" chip columns.",
            "Every chip ordinal must occur on this physical row.","Restore all chip columns and the correct chip count.");continue;}
        if(!chips){chips=nchips;map.chip_lengths.assign(size_t(chips),0);}
        if(nchips!=chips){issue(file,"map_chip_count_changed","error",reader.line,f[5].column,"n_chips","Chip count differs from earlier records.",
            "All marker rows must use the same chip-column schema.","Use one consistent chip layout throughout the map.");continue;}
        if(overall!=int(count))issue(file,"map_overall_order","error",reader.line,f[3].column,"overall","Expected "+std::to_string(count)+".",
            "Overall marker ordinals must be consecutive from 1.","Restore the original order and renumber consistently with genotype payloads; never reorder only one file.");
        if(chr<last_chr||within!=(chr==last_chr?last_within+1:1))issue(file,"map_chromosome_order","error",reader.line,f[2].column,"within","Chromosome order decreases or within-chromosome ordinal is not the next value.",
            "Chromosomes must be ordered, with marker ordinals restarting at 1. Gaps in chromosome codes are permitted.","Check chromosome grouping and marker ordinals against the genotype export.");
        if(chr==last_chr&&position<last_position)issue(file,"map_position_order","error",reader.line,f[4].column,"position","Position decreases within this chromosome.",
            "Physical positions must be nondecreasing; tied positions are accepted.","Verify assembly and marker order; synchronize any correction with genotype columns.");
        for(int chip=0;chip<chips;++chip){int ordinal=0;if(!numeric(file,reader.line,f[6+chip],"chip ordinal",ordinal))continue;
            if(ordinal<0||(ordinal!=0&&ordinal!=map.chip_lengths[chip]+1))issue(file,"map_chip_ordinal","error",reader.line,f[6+chip].column,"chip "+std::to_string(chip+1),"Expected 0 or "+std::to_string(map.chip_lengths[chip]+1)+".",
                "Zero means marker absent from the chip; nonzero ordinals must advance consecutively.","Correct chip ordinals without shifting the corresponding genotype columns.");
            if(ordinal!=0)++map.chip_lengths[chip];}
        map.chromosomes.push_back(chr);
        auto bound=map.bounds.emplace(chr,std::make_pair(int(count),int(count)));bound.first->second.second=int(count);
        last_chr=chr;last_within=within;last_position=position;
    }
    if(!count)issue(file,"no_markers","error",2,1,"record","No marker records after the header.","At least one marker is required.","Supply the complete map.");
    map.usable=report.errors==before&&count==map.chromosomes.size();
    report.metrics["map_markers"]=count;report.metrics["map_chips"]=map.chip_lengths.size();report.metrics["map_chromosomes"]=map.bounds.size();
    if(!map.usable)report.blocked.push_back("Genotype chip/map agreement and prior/map geometry: chromosome.data contains errors.");
    else if(options_ok&&options.controls[1]>0&&options.controls[1]<map.bounds.rbegin()->first)
        issue("findhap.options","x_chromosome_not_last","error",1,0,"XChrom","XChrom is less than the largest map chromosome code.",
            "The engine requires X to be the final chromosome and can stop without a useful failure status otherwise.","Use 0 for no X chromosome, or a compatible final chromosome code with the correct map.");
    else if(options_ok&&options.controls[1]&&map.bounds.count(options.controls[1])==0)
        issue("findhap.options","x_chromosome_absent","information",1,0,"XChrom","Configured X chromosome is absent from the supplied map.",
            "No supplied chromosome will use X-specific handling. This can be intentional for an autosomal subset.","Confirm the map includes exactly the intended chromosomes.");
}
} // namespace turbohap_audit

#include "input_audit_pedigree.hpp"
#include "input_audit_priors.hpp"
