#pragma once
namespace turbohap_audit {
inline bool calendar_date(int date) {
    const int year=date/10000,month=(date/100)%100,day=date%100;
    if(year<1||year>9999||month<1||month>12||day<1)return false;
    constexpr int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    int maximum=days[month-1]+(month==2&&year%4==0&&(year%100!=0||year%400==0));
    return day<=maximum;
}
inline void Audit::pedigree_file() {
    const std::string file="pedigree.file";
    if(report.files[file].state!="missing"&&!regular(file))return;
    const auto before=report.errors;
    Lines reader(report,directory,file);std::string row;
    if(!reader)return;
    while(reader.next(row)) {
        if(reader.oversized)continue;
        auto f=tokens(row);
        if(f.empty()){issue(file,"blank_pedigree_record","warning",reader.line,1,"record","Blank record is skipped by the engine.",
            "Blank rows can conceal a missing animal export.","Confirm the row is intentionally empty; remove it or restore the missing record.");continue;}
        if(row.size()>254)issue(file,"pedigree_record_width","error",reader.line,255,"record","Record exceeds the engine's 255-byte input buffer.",
            "A long row may be split into multiple engine reads.","Shorten unused trailing fields or re-export the five required fields on a single short line.");
        if(f.size()<5){issue(file,"pedigree_missing_fields","error",reader.line,row.size()+1,"record","Fewer than five fields: sex, animal, sire, dam, birth date.",
            "The legacy reader can silently skip this animal.","Restore every required field; use nonpositive parent codes for unknown parents.");continue;}
        Animal animal;animal.line=reader.line;
        bool good=true;
        int* values[]={&animal.id,&animal.sire,&animal.dam,&animal.birth};
        const char* names[]={"animal","sire","dam","birth_date"};
        for(int i=0;i<4;++i)good=numeric(file,reader.line,f[i+1],names[i],*values[i])&&good;
        if(f[0].text.size()!=1){issue(file,"pedigree_sex_width","error",reader.line,f[0].column,"sex","Sex field must contain one character.",
            "The engine reads a single character before the integer fields; longer words can disrupt parsing.","Use M, F or the documented single-character unknown code.");good=false;}
        if(!good)continue;
        animal.sex=f[0].text[0];
        if(animal.id<=0){issue(file,"pedigree_nonpositive_id","error",reader.line,f[1].column,"animal","Animal ID is not positive.",
            "Zero and negative values are reserved for unknown parent links.","Assign a unique positive animal ID and update references consistently.");continue;}
        auto inserted=animal_index.emplace(animal.id,animals.size());
        if(!inserted.second){issue(file,"duplicate_pedigree_id","error",reader.line,f[1].column,"animal","ID already occurs on line "+std::to_string(animals[inserted.first->second].line)+".",
            "Two pedigree records cannot represent the same animal unambiguously.","Resolve the duplicate in the source data; do not arbitrarily keep one row.");continue;}
        if(animal.sex!='M'&&animal.sex!='F')issue(file,"unknown_sex","warning",reader.line,f[0].column,"sex","Sex is neither M nor F.",
            "Unknown/unsupported sex needs review, especially for X-specific handling and parental roles.","Confirm the recorded sex or retain an intentional unknown code with that limitation understood.");
        animal.full_date=calendar_date(animal.birth);
        if(animal.birth!=0&&!animal.full_date)issue(file,"birth_date_review","warning",reader.line,f[4].column,"birth_date","Date is partial or not a valid YYYYMMDD calendar date.",
            "The engine compares birth dates numerically; date errors can affect age-based summaries.","Check the source date. Use a documented unknown value; do not invent missing dates.");
        if(f.size()>5)issue(file,"unused_pedigree_fields","information",reader.line,f[5].column,"trailing fields","Additional fields after the five required values are ignored by inference.",
            "The standard example also includes extra pedigree metadata; it does not participate in inference.","Keep required values in the first five fields and confirm the trailing columns are only metadata.",f.size()-5);
        animals.push_back(animal);
    }
    if(animals.empty())issue(file,"no_pedigree_animals","error",0,0,"file","No usable pedigree animals were read.","At least one valid pedigree animal is required.","Supply the complete pedigree export.");
    pedigree_ok=report.errors==before;
    report.metrics["pedigree_unique_animals"]=animals.size();
    if(!pedigree_ok)report.blocked.push_back("Some ID joins may be incomplete because pedigree.file contains invalid or duplicate records.");
    pedigree_links();
}
inline void Audit::pedigree_links() {
    const std::string file="pedigree.file";
    constexpr uint32_t absent=std::numeric_limits<uint32_t>::max();
    if(animals.size()>size_t(INT_MAX/2)){issue(file,"pedigree_capacity","error",0,0,"file","Pedigree exceeds the engine's signed 2*maxped index capacity.",
        "The engine cannot form its parent/hash dimensions safely for this many records.","Use an appropriately sized validated implementation.");return;}
    std::vector<std::array<uint32_t,2>> parents(animals.size(),{absent,absent});
    for(size_t i=0;i<animals.size();++i) {
        const auto& child=animals[i];
        const int ids[]={child.sire,child.dam};
        if(child.sire>0&&child.sire==child.dam)issue(file,"same_sire_and_dam","error",child.line,0,"parents","Both parental roles point to the same positive ID.",
            "The same animal cannot supply both parental roles in this pedigree model.","Verify both parent identifiers in the source records.");
        for(int role=0;role<2;++role) {
            const std::string field=role==0?"sire":"dam";
            if(ids[role]<=0){++report.metrics["unknown_parent_links"];continue;}
            auto found=animal_index.find(ids[role]);
            if(found==animal_index.end()){issue(file,"unrepresented_parent","warning",child.line,0,field,"Positive parent ID has no pedigree record.",
                "The engine treats an unrepresented parent as unknown; this may be an intentional pedigree subset.","Add the missing ancestor if available, or confirm this parent is intentionally outside the supplied pedigree.");continue;}
            parents[i][role]=uint32_t(found->second);
            const auto& parent=animals[found->second];
            if(found->second==i)issue(file,"self_parent","error",child.line,0,field,"Animal is its own parent.",
                "Self-parentage creates a directed ancestry cycle.","Correct the parental ID using the original pedigree source.");
            if((role==0&&parent.sex=='F')||(role==1&&parent.sex=='M'))issue(file,"parent_sex_conflict","warning",child.line,0,field,"Parent's recorded sex conflicts with this role; parent record is on line "+std::to_string(parent.line)+".",
                "This can indicate swapped parents, an incorrect sex or an incorrect ID.","Review the child and parent records together before changing either.");
            if(child.full_date&&parent.full_date&&parent.birth>=child.birth)issue(file,"parent_birth_order","warning",child.line,0,field,"Parent date is not earlier than the offspring date; parent record is on line "+std::to_string(parent.line)+".",
                "A parent should be born before its offspring when full dates are known.","Check date format, source dates and the parent-child link.");
        }
    }
    // Iterative Tarjan SCC: O(animals + parent links), with no recursion depth
    // limit and no confusion between cycle members and their descendants.
    std::vector<int> index(animals.size(),-1),low(animals.size());
    std::vector<uint8_t> active(animals.size(),0);
    std::vector<uint32_t> stack;
    struct Frame { uint32_t vertex; unsigned next; };
    std::vector<Frame> frames;
    int counter=0;
    auto enter=[&](uint32_t v){index[v]=low[v]=counter++;active[v]=1;stack.push_back(v);frames.push_back({v,0});};
    for(uint32_t root=0;root<animals.size();++root) {
        if(index[root]>=0)continue;
        enter(root);
        while(!frames.empty()) {
            auto& frame=frames.back();const auto v=frame.vertex;
            if(frame.next<2) {
                auto parent=parents[v][frame.next++];
                if(parent==absent)continue;
                if(index[parent]<0){enter(parent);continue;}
                if(active[parent])low[v]=std::min(low[v],index[parent]);
                continue;
            }
            frames.pop_back();
            if(!frames.empty())low[frames.back().vertex]=std::min(low[frames.back().vertex],low[v]);
            if(low[v]==index[v]) {
                std::vector<uint32_t> component;
                for(;;){auto member=stack.back();stack.pop_back();active[member]=0;component.push_back(member);if(member==v)break;}
                if(component.size()>1||parents[v][0]==v||parents[v][1]==v) {
                    ++report.metrics["ancestry_cycle_components"];
                    report.metrics["ancestry_cycle_animals"]+=component.size();
                    for(auto member:component)issue(file,"ancestry_cycle","error",animals[member].line,0,"parents",
                        "Animal belongs to a directed ancestry cycle involving "+std::to_string(component.size())+" records.",
                        "An animal is its own ancestor through parent links. Ordinary inbreeding/repeated ancestors without a directed cycle are allowed.",
                        "Trace and verify parental links in this component; do not delete arbitrary animals to break the cycle.");
                }
            }
        }
    }
}

inline void Audit::genotype_file() {
    const std::string file="genotypes.txt";
    if(report.files[file].state!="missing"&&!regular(file))return;
    size_t limit=64*1024*1024;
    if(map.usable&&!map.chip_lengths.empty())limit=size_t(*std::max_element(map.chip_lengths.begin(),map.chip_lengths.end()))*2+65536;
    Lines reader(report,directory,file,limit);std::string row;
    int previous_id=0;size_t records=0;
    std::array<uint64_t,6> code_counts{};
    while(reader.next(row)) {
        ++records;
        if(reader.oversized)continue;
        if(row.size()<31){issue(file,"truncated_genotype_header","error",reader.line,row.size()+1,"header","Expected three 10-byte integers and one separator space.",
            "A short or blank record cannot identify the animal, chip and declared payload length.","Restore the complete fixed-width record from the export.");continue;}
        int values[3]{};bool valid[3]={true,true,true};const char* names[]={"animal","chip","locus_count"};
        for(int h=0;h<3;++h) {
            auto field=std::string_view(row).substr(size_t(h)*10,10);
            bool ascii=true;
            for(char c:field)if(c!=' '&&c!='+'&&(c<'0'||c>'9'))ascii=false;
            valid[h]=ascii&&integer(field,values[h])&&values[h]>0;
            if(!valid[h])issue(file,"invalid_fixed_width_header","error",reader.line,size_t(h)*10+1,names[h],"Expected a positive 32-bit integer with ASCII-space padding in exactly ten bytes.",
                "Tabs, negative IDs, embedded spaces and overflow can shift or invalidate the record header.","Re-export the three fixed-width integer fields; do not use tab-separated headers.");
        }
        if(row[30]!=' ')issue(file,"genotype_separator","error",reader.line,31,"separator","Column 31 is not the required ASCII space.",
            "The genotype payload must begin in column 32.","Use exactly three ten-byte header fields, one space, then the genotype digits.");
        if(valid[0]) {
            if(previous_id&&values[0]<previous_id)issue(file,"genotype_id_order","warning",reader.line,1,"animal","Animal ID decreases relative to the preceding record.",
                "Some legacy output paths expect genotype records sorted by animal ID.","Confirm the intended record order; if sorting is required, move complete records without changing genotype columns.");
            previous_id=values[0];
            auto animal=animal_index.find(values[0]);
            if(animal==animal_index.end())issue(file,"genotype_without_pedigree","error",reader.line,1,"animal","No matching usable pedigree record.",
                "This animal cannot be joined reliably to the supplied pedigree.","Add/correct the pedigree record or correct the genotype ID. Fix malformed pedigree rows before interpreting missing joins.");
            else {
                auto& known=animals[animal->second];
                if(known.genotype_line)issue(file,"duplicate_genotype_id","warning",reader.line,1,"animal","Another genotype record occurs on line "+std::to_string(known.genotype_line)+".",
                    "Multiple records for one animal can make chip selection and legacy cache mappings ambiguous.","Verify whether records are duplicate exports or multiple chips; resolve them with an explicit source-data policy.");
                else known.genotype_line=reader.line;
            }
        }
        if(map.usable&&valid[1]) {
            if(values[1]>int(map.chip_lengths.size()))issue(file,"unknown_genotype_chip","error",reader.line,11,"chip","Chip is absent from chromosome.data.",
                "Genotype chip numbering must match the map columns.","Use the matching map/chip export.");
            else if(valid[2]&&values[2]!=map.chip_lengths[values[1]-1])issue(file,"genotype_map_length","error",reader.line,21,"locus_count",
                "Declared "+std::to_string(values[2])+" loci; map chip requires "+std::to_string(map.chip_lengths[values[1]-1])+".",
                "A payload/map length disagreement risks assigning calls to the wrong markers.","Reconcile chip membership and marker order at the source; do not pad or trim blindly.");
        }
        if(!valid[2])continue;
        const size_t expected=31+size_t(values[2]),end=std::min(expected,row.size());
        if(row.size()<expected)issue(file,"truncated_genotype_payload","error",reader.line,row.size()+1,"genotypes",
            "Expected "+std::to_string(values[2])+" calls, found "+std::to_string(row.size()-31)+" bytes.",
            "The declared genotype payload is incomplete.","Restore the complete record; do not fill missing bytes with spaces.");
        uint64_t blanks=0,bad=0;size_t first_blank=0,first_bad=0;
        for(size_t i=31;i<end;++i) {
            const unsigned char c=static_cast<unsigned char>(row[i]);
            if(c==' '){if(!blanks)first_blank=i;++blanks;}
            else if(c<'0'||c>'5'){if(!bad)first_bad=i;++bad;}
            else ++code_counts[c-'0'];
        }
        report.metrics["genotype_payload_cells_seen"]+=end-31;
        if(blanks)issue(file,"blank_genotype_fields","error",reader.line,first_blank+1,"genotypes",
            std::to_string(blanks)+" blanks in the declared payload."+(first_blank==31?" The first call is blank: likely an extra separator.":""),
            "Legacy reading treats each blank as code 0; an extra separator can shift every marker.",
            "Check column 31/32 alignment and re-export. Use explicit code 5 for missing calls; never silently delete internal blanks.",blanks);
        if(bad)issue(file,"invalid_genotype_codes","error",reader.line,first_bad+1,"genotypes",std::to_string(bad)+" unsupported bytes in the payload.",
            "Only ASCII genotype codes 0 through 5 are supported, including partial codes 3 and 4.","Correct the source encoding; tabs, punctuation and other digits are not genotype calls.",bad);
        uint64_t extra=0,tailbad=0;size_t firstextra=0,firsttail=0;
        for(size_t i=expected;i<row.size();++i){unsigned char c=static_cast<unsigned char>(row[i]);if(c==' ')continue;
            if(c>='0'&&c<='5'){if(!extra)firstextra=i;++extra;}else{if(!tailbad)firsttail=i;++tailbad;}}
        if(extra)issue(file,"extra_genotype_digits","error",reader.line,firstextra+1,"trailing payload",std::to_string(extra)+" extra genotype digits after the declared calls.",
            "The legacy reader ignores these digits; this can conceal a shifted or wrong-length export.","Reconcile declared length, separator width and the original marker list. Do not trim until alignment is verified.",extra);
        if(tailbad)issue(file,"invalid_genotype_tail","error",reader.line,firsttail+1,"trailing payload",std::to_string(tailbad)+" unsupported trailing bytes.",
            "Only harmless ASCII-space padding may follow the complete payload.","Remove unintended trailing fields or re-export with correct record endings.",tailbad);
    }
    report.metrics["genotype_records"]=records;
    for(size_t i=0;i<6;++i)report.metrics["genotype_code_"+std::to_string(i)]=code_counts[i];
    if(!records&&reader)issue(file,"empty_genotypes","error",1,1,"file","No genotype records.","At least one complete genotype record is required.","Supply the intended genotype export.");
    size_t typed=0;for(const auto& a:animals)typed+=a.genotype_line!=0;
    report.metrics["pedigree_animals_with_genotypes"]=typed;
    report.metrics["pedigree_animals_without_genotypes"]=animals.size()-typed;
}
} // namespace turbohap_audit
