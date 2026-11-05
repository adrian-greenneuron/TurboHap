#include "input_audit.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <unistd.h>

namespace {
std::string consume(int& position,int argc,char** argv,const char* option) {
    if(position+1>=argc)throw std::invalid_argument(std::string("missing value for ")+option);
    return argv[++position];
}
// Exclusive creation prevents report files from replacing existing inputs,
// outputs, symlink targets or an earlier report.
void create_report(const std::filesystem::path& path,const std::string& content) {
    int descriptor=::open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
    if(descriptor<0)throw std::runtime_error(path.string()+": cannot create report exclusively: "+std::strerror(errno));
    FILE* file=::fdopen(descriptor,"w");
    if(!file){::close(descriptor);throw std::runtime_error("cannot open report stream");}
    bool good=std::fwrite(content.data(),1,content.size(),file)==content.size();
    if(std::fclose(file)!=0)good=false;
    if(!good)throw std::runtime_error("failed writing report: "+path.string());
}
}
int main(int argc,char** argv) {
    try {
        namespace fs=std::filesystem;
        fs::path directory=".",prefix,issues_path;
        bool json=false;
        turbohap_audit::Report report;
        for(int i=1;i<argc;++i) {
            const std::string option=argv[i];
            if(option=="--directory")directory=consume(i,argc,argv,"--directory");
            else if(option=="--report-prefix")prefix=consume(i,argc,argv,"--report-prefix");
            else if(option=="--issues-tsv")issues_path=consume(i,argc,argv,"--issues-tsv");
            else if(option=="--json")json=true;
            else if(option=="--max-examples") {
                int count=0;
                if(!turbohap_audit::integer(consume(i,argc,argv,"--max-examples"),count)||count<0||count>100)
                    throw std::invalid_argument("--max-examples must be an integer from 0 to 100");
                report.limit=size_t(count);
            } else if(option=="--help") {
                std::puts("Usage: turbohap-validate-inputs [--directory DIR] [--json]\n"
                    "  [--report-prefix /new/path/input_audit_DDMMYY] [--max-examples 5]\n"
                    "  [--issues-tsv /new/path/all_issues.tsv]\n\n"
                    "Checks findhap.options, pedigree.file, chromosome.data, genotypes.txt, and active\n"
                    "hap.listpriors/hap.foundpriors/hap.Xseg, including cross-file references.\n"
                    "Collects errors and warnings with exact counts and bounded location examples.\n"
                    "Default: human-readable stdout; --json selects structured stdout.\n"
                    "--report-prefix writes private .md and .json reports; --issues-tsv streams every\n"
                    "issue event (not every cell) with locations/counts. Existing files are never overwritten.\n"
                    "Exit 0: checks completed, including review warnings; exit 2: errors or incomplete checks.\n"
                    "No inference or automatic repair. No result files are opened. Biological marker order,\n"
                    "allele coding, genome assembly and Mendelian/genomic accuracy are not certified.");
                return 0;
            } else throw std::invalid_argument("unknown argument: "+option+" (use --help)");
        }
        if(!fs::is_directory(directory))throw std::invalid_argument("--directory must name an existing input directory");
        std::vector<fs::path> destinations;
        if(!prefix.empty()){destinations.push_back(prefix.string()+".md");destinations.push_back(prefix.string()+".json");}
        if(!issues_path.empty())destinations.push_back(issues_path);
        std::set<fs::path> resolved;
        for(const auto& path:destinations) {
            auto absolute=fs::weakly_canonical(fs::absolute(path));
            if(!resolved.insert(absolute).second)throw std::invalid_argument("report destinations overlap");
            if(fs::exists(path)||fs::is_symlink(path))throw std::invalid_argument("refusing to overwrite existing report/input/result path: "+path.string());
            const auto parent=fs::weakly_canonical(fs::absolute(path).parent_path());
            if(parent==fs::weakly_canonical(directory)) {
                const auto name=path.filename().string();
                for(const char* reserved:{"findhap.options","pedigree.file","genotypes.txt","chromosome.data","hap.listpriors","hap.foundpriors","hap.Xseg","hap.list","hap.found","hap.filled","hap.inherit","haplotypes.txt","genotypes.filled","cross.overs","allele.frequency","do_not_distrib","fort.26"})
                    if(name==reserved)throw std::invalid_argument("report path is a reserved input/result filename");
            }
        }
        // The TSV can be large, so stream it instead of retaining every issue.
        FILE* tsv_file=nullptr;
        struct Close {FILE*& file;~Close(){if(file)std::fclose(file);}} close{tsv_file};
        if(!issues_path.empty()) {
            int descriptor=::open(issues_path.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
            if(descriptor<0)throw std::runtime_error("cannot create detailed issue TSV exclusively");
            tsv_file=::fdopen(descriptor,"w");
            if(!tsv_file){::close(descriptor);throw std::runtime_error("cannot open detailed issue TSV stream");}
            if(std::fprintf(tsv_file,"severity\tfile\tcode\tline\tcolumn\tfield\taffected_values\tdetail\tsuggested_action\n")<0)
                throw std::runtime_error("cannot write issue TSV header");
            report.tsv=tsv_file;
        }
        turbohap_audit::Audit audit(report,directory);
        audit.run();
        if(report.tsv){
            if(std::fflush(tsv_file)!=0)throw std::runtime_error("failed writing detailed issues TSV");
            if(std::fclose(tsv_file)!=0){tsv_file=nullptr;throw std::runtime_error("failed closing detailed issues TSV");}
            tsv_file=nullptr;report.tsv=nullptr;
        }
        if(json)report.json(std::cout);else report.text(std::cout,false);
        if(!prefix.empty()) {
            std::ostringstream markdown,structured;report.text(markdown,true);report.json(structured);
            create_report(prefix.string()+".md",markdown.str());
            create_report(prefix.string()+".json",structured.str());
        }
        if(report.exit_code())std::cerr<<"Input validation failed: "<<report.errors<<" error occurrences. See the audit report for locations and repair advice.\n";
        return report.exit_code();
    }catch(const std::exception& error){std::cerr<<"Input audit could not complete: "<<error.what()<<'\n';return 2;}
}
