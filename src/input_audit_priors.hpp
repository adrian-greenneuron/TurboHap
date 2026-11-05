#pragma once
namespace turbohap_audit {
inline void Audit::prior_files() {
    const bool active=regular("hap.listpriors");
    if(!active) {
        if(regular("hap.foundpriors")) {
            report.files["hap.foundpriors"].state="present_but_unused";
            issue("hap.foundpriors","orphan_prior","warning",0,0,"file","Present without hap.listpriors, so inference will not consume it.",
                "A prior import requires hap.listpriors, hap.foundpriors and hap.Xseg together.","Supply a matched prior set or keep this unused file separate from the run inputs.");
        }
        if(regular("hap.Xseg"))report.files["hap.Xseg"].state="present_but_unused";
        return;
    }
    for(const char* name : {"hap.foundpriors","hap.Xseg"})if(!regular(name))issue(name,"missing_prior_companion","error",0,0,"file","Active hap.listpriors has no readable regular companion file.",
        "All three prior files must come from a consistent export.","Restore the matching companion files; do not fabricate their headers.");
    int list_steps=0,found_steps=0,high_density=-1,x_segment=-1;
    auto fixed_int=[&](const std::string& name,const std::string& row,size_t line,size_t offset,size_t width,const std::string& field,int& value)->bool {
        if(row.size()<offset+width){issue(name,"truncated_prior_header","error",line,row.size()+1,field,"Record is shorter than the required fixed-width header.",
            "Prior fields must remain in their original byte columns.","Restore the original complete prior export.");return false;}
        return numeric(name,line,{std::string_view(row).substr(offset,width),offset+1},field,value);
    };
    auto headers=[&](Lines& input,const std::string& name,size_t offset,int& steps) {
        std::string row;
        if(!input.next(row)){issue(name,"missing_prior_headers","error",1,1,"header","Missing step-count and label records.",
            "Both library prior files begin with two header records.","Restore both headers from the matching source export.");return false;}
        bool okay=fixed_int(name,row,input.line,offset,3,"steps",steps);
        if(okay&&steps<1){issue(name,"prior_steps_domain","error",1,offset+1,"steps","Step count must be positive.",
            "The declared number of prior steps is invalid.","Restore the original step-count header.");okay=false;}
        if(!input.next(row)){issue(name,"missing_prior_headers","error",2,1,"header","Missing second header record.",
            "The engine discards two physical records before reading priors.","Restore the label/header record, preserving record order.");okay=false;}
        return okay;
    };
    const std::string list_name="hap.listpriors";
    Lines list(report,directory,list_name,std::max(size_t(1024*1024),map.chromosomes.size()*2+65536));
    bool list_header=list&&headers(list,list_name,31,list_steps);
    std::string row;int previous_step=0,previous_segment=0,sentinel_step=0;
    while(list&&list.next(row)) {
        if(list.oversized)continue;
        if(trim(row).empty()){issue(list_name,"blank_prior_record","error",list.line,1,"record","Blank library-prior record.",
            "The fixed-width prior reader cannot interpret a blank record.","Restore or remove the unintended blank row without dropping a library record.");continue;}
        int hap=0,chr=0,step=0,segment=0,start=0,length=0;
        int* destinations[]={&hap,&chr,&step,&segment,&start,&length};
        const size_t offsets[]={20,30,33,36,43,51},widths[]={10,3,3,7,8,6};
        const char* fields[]={"haplotype","chromosome","step","segment","start_locus","length"};
        bool okay=true;
        for(int f=0;f<6;++f)okay=fixed_int(list_name,row,list.line,offsets[f],widths[f],fields[f],*destinations[f])&&okay;
        if(!okay)continue;
        double frequency=0,cumulative=0;
        bool real_ok=true;
        for(int field=0;field<2;++field) {
            try {
                auto text=trim(std::string_view(row).substr(size_t(field)*10,10));
                double value=findhap::input_real(std::string(text));
                if(!std::isfinite(value)||value<0||value>100.001)throw std::runtime_error("range");
                (field==0?frequency:cumulative)=value;
            }catch(const std::exception&){real_ok=false;issue(list_name,"invalid_prior_frequency","error",list.line,size_t(field)*10+1,field==0?"frequency":"cumulative_frequency",
                "Expected a finite percentage in 0..100 (allowing export rounding).","Frequency fields must retain their original scale and columns.","Restore valid percentages from the same prior export.");}
        }
        if(hap==0&&chr==0&&segment==0&&start==0&&length==0) {
            if(step!=previous_step+1||step<2||frequency!=0||cumulative!=0||sentinel_step||
               (list_header&&step>list_steps))
                issue(list_name,"invalid_prior_sentinel","error",list.line,1,"sentinel","End-of-step sentinel does not identify the next step with zero data fields.",
                    "The engine consumes these sentinels to separate prior steps.","Regenerate all prior files together; do not hand-edit step boundaries.");
            sentinel_step=step;continue;
        }
        if(hap<1||chr<1||step<1||segment<1||start<1||length<1||
           (list_header&&step>list_steps)||(options_ok&&hap>options.controls[5])) {
            issue(list_name,"prior_index_domain","error",list.line,21,"indices","Invalid haplotype, chromosome, step, segment, start or length.",
                "Prior indices must be positive, fit maxhap and remain within the declared steps.","Use a matching export and options; do not renumber haplotypes independently.");continue;
        }
        if(step<previous_step||(step==previous_step&&segment<previous_segment))issue(list_name,"prior_record_order","error",list.line,34,"step/segment",
            "Step or segment order decreases.","The engine loads groups in file order; revisiting a group can overwrite prior slots.","Restore the original step/segment ordering of complete records.");
        if(previous_step&&step>previous_step&&sentinel_step!=step)issue(list_name,"missing_prior_sentinel","error",list.line,34,"step",
            "A new step begins without its end-of-previous-step sentinel.","Without the sentinel the engine may consume and discard the first library record of the new step.","Restore step separators from the original export.");
        if(step!=previous_step)sentinel_step=0;
        previous_step=step;previous_segment=segment;
        if(options_ok&&length>std::max(options.controls[2],options.controls[3]))issue(list_name,"prior_length_limit","error",list.line,52,"length",
            "Prior segment is longer than the configured maximum schedule length.","This prior export does not fit the current library dimensions.","Use the original compatible segment options or regenerate the priors.");
        const int64_t last=int64_t(start)+length-1;
        if(map.usable&&(last>int64_t(map.chromosomes.size())||start>int(map.chromosomes.size())))
            issue(list_name,"prior_map_bounds","error",list.line,44,"start/length","Prior segment extends outside the marker map.",
                "Prior coordinates refer to marker rows, not physical base positions.","Supply the exact marker map used to produce these priors.");
        else if(map.usable&&(map.chromosomes[size_t(start-1)]!=chr||map.chromosomes[size_t(last-1)]!=chr))
            issue(list_name,"prior_chromosome_geometry","error",list.line,31,"chromosome","Prior segment crosses a chromosome boundary or has the wrong chromosome label.",
                "A prior segment must lie entirely on its declared chromosome.","Restore the matched map and segment geometry.");
        auto& group=library[step][segment];
        if(!group.line){group.chromosome=chr;group.start=start;group.length=length;group.line=list.line;}
        else if(group.chromosome!=chr||group.start!=start||group.length!=length)
            issue(list_name,"inconsistent_prior_geometry","error",list.line,37,"segment","Other haplotypes in this segment use different coordinates or lengths; first record is on line "+std::to_string(group.line)+".",
                "All haplotypes in one step/segment must share the same marker interval.","Regenerate the inconsistent prior group from a single source.");
        if(!group.haplotypes.insert(hap).second)issue(list_name,"duplicate_prior_haplotype","error",list.line,21,"haplotype","Repeated haplotype ID within this step/segment.",
            "References to a duplicate original haplotype ID become ambiguous.","Resolve the duplicate through a consistent prior re-export.");
        if(real_ok&&group.cumulative>cumulative+.00002)issue(list_name,"prior_cumulative_order","warning",list.line,11,"cumulative_frequency","Cumulative frequency decreases within the group.",
            "This suggests mixed or reordered library records.","Check that percentages and record order come from one export.");
        group.cumulative=cumulative;
        if(real_ok)group.frequency_sum+=frequency;
        const size_t expected=58+size_t(length);
        if(row.size()<expected)issue(list_name,"truncated_prior_payload","error",list.line,row.size()+1,"haplotypes","Fewer haplotype digits than the declared length.",
            "A truncated prior cannot define the segment's full haplotype.","Restore the complete prior record; do not pad it with invented alleles.");
        if(row.size()>57&&row[57]!=' ')issue(list_name,"prior_separator","error",list.line,58,"separator","Expected the haplotype separator space in column 58.",
            "The first haplotype digit starts in column 59.","Restore the original fixed-width header and separator.");
        uint64_t invalid=0;size_t first=0;
        for(size_t i=58;i<std::min(row.size(),expected);++i)if(row[i]<'0'||row[i]>'2'){if(!invalid)first=i;++invalid;}
        if(invalid)issue(list_name,"invalid_prior_haplotype_codes","error",list.line,first+1,"haplotypes",std::to_string(invalid)+" codes outside 0..2.",
            "Prior haplotypes use 0, 1 and 2, unlike genotype records which also allow 3..5.","Correct the original haplotype export; do not substitute genotype coding.",invalid);
        if(row.size()>expected&&!trim(std::string_view(row).substr(expected)).empty())issue(list_name,"extra_prior_payload","error",list.line,expected+1,"trailing fields","Non-padding data after the declared haplotype length.",
            "Extra calls may indicate shifted prior columns.","Reconcile the declared length and fixed-width payload.");
    }
    if(list&&library.empty())issue(list_name,"empty_prior_library","error",0,0,"file","No usable haplotype library records.","An active prior set requires library entries.","Restore the complete matching library export.");
    for(auto& step_entry:library) {
        int expected_segment=1;int64_t expected_start=1;
        for(const auto& entry:step_entry.second) {
            const auto& segment=entry.second;
            const double tolerance=std::max(.002,double(segment.haplotypes.size())*.000006);
            if(std::abs(segment.frequency_sum-100)>tolerance||std::abs(segment.cumulative-100)>.002)
                issue(list_name,"prior_frequency_total","warning",segment.line,1,"frequency percentages",
                    "Segment frequencies/cumulative total do not sum to 100% within export rounding tolerance.",
                    "This can indicate a filtered, truncated or mixed prior library.","Confirm whether filtering was intentional; otherwise restore the complete segment library.");
            if(entry.first!=expected_segment)issue(list_name,"missing_prior_segment","error",segment.line,37,"segment","Expected segment "+std::to_string(expected_segment)+".",
                "Segment IDs must form a complete consecutive set for each step.","Restore missing segment records from the original export.");
            expected_segment=entry.first+1;
            if(segment.start!=expected_start)issue(list_name,segment.start<expected_start?"overlapping_prior_segments":"uncovered_prior_loci","error",segment.line,44,"start_locus",
                "Expected next start "+std::to_string(expected_start)+", found "+std::to_string(segment.start)+".",
                "Every marker must be covered exactly once; overlapping/gapped priors can leave reconstructed calls undefined.","Regenerate a complete disjoint prior partition with the correct map and settings.");
            expected_start=int64_t(segment.start)+segment.length;
        }
        if(map.usable&&expected_start!=int64_t(map.chromosomes.size())+1)issue(list_name,"incomplete_prior_coverage","error",0,0,"step "+std::to_string(step_entry.first),
            "The segment partition does not end at the final map marker.","Prior steps must cover the complete supplied map.","Restore all segments for this step from a matching export.");
    }
    if(list_header) {
        if(list_steps>100000){issue(list_name,"prior_step_resource_limit","error",1,32,"steps","Step count exceeds the audit's 100,000-step verification limit.",
            "A complete step schedule cannot be certified within the bounded audit.","Verify that this is a valid prior header and use a reasonable inference configuration.");}
        else for(int step=1;step<=list_steps;++step)if(!library.count(step))issue(list_name,"missing_prior_step","error",0,0,"step "+std::to_string(step),"No library records for a declared step.",
            "A last-step-only output is not a complete multi-step prior import.","Export listout=1 for all required steps, using the intended matching options.");
        if(options_ok&&list_steps!=int(report.metrics["effective_steps"]))issue(list_name,"prior_options_steps","error",1,32,"steps","Declared prior steps disagree with the effective option schedule.",
            "The current options and prior export are not consistent.","Restore the original compatible options or regenerate all priors.");
    }
    // Check the actual rounded schedule and balanced chromosome segmentation.
    if(options_ok&&map.usable&&report.metrics["effective_steps"]<=100000) {
        const int steps=int(report.metrics["effective_steps"]);
        const bool consecutive=map.bounds.empty()||int64_t(map.bounds.rbegin()->first)-map.bounds.begin()->first+1==int64_t(map.bounds.size());
        if(!consecutive)issue(list_name,"gapped_chromosome_schedule_review","warning",0,0,"geometry","Map chromosome codes have gaps; option-derived geometry comparison was not performed.",
            "Basic full coverage, chromosome boundaries and prior references are still checked; empty engine chromosome slots need separate review.","Review the source map and the prior export together before relying on a gap-containing schedule.");
        else {
            float ratio=steps>1?std::pow(float(options.controls[2])/options.controls[3],1.0f/(steps-1)):1.0f;
            int target=options.controls[2];
            for(int step=1;step<=steps;++step) {
                if(step>1){float next=std::round(float(target)/ratio);if(!std::isfinite(next)||next<1||next>=2147483648.0f){issue("findhap.options","invalid_rounded_schedule","error",0,0,"segment schedule","Rounded schedule produces an invalid segment length.",
                    "The engine's rounded step lengths must remain finite positive 32-bit integers.","Use a valid maxlen/minlen/steps combination.");break;}target=int(next);}
                auto loaded=library.find(step);if(loaded==library.end())continue;
                int segment_id=0;
                for(const auto& chr:map.bounds) {
                    const int count=chr.second.second-chr.second.first+1,segs=1+(count-1)/target;
                    int start=chr.second.first;
                    for(int s=0;s<segs;++s){const int length=(chr.second.second-start+1)/(segs-s);++segment_id;
                        auto found=loaded->second.find(segment_id);
                        if(found!=loaded->second.end()&&(found->second.start!=start||found->second.length!=length||found->second.chromosome!=chr.first))
                            issue(list_name,"prior_option_geometry","error",found->second.line,37,"segment","Segment differs from the engine's rounded options/map geometry.",
                                "A complete partition can still be incompatible with the configured segment schedule.","Use the source options/map from this prior export or regenerate the complete prior set.");
                        start+=length;}
                }
                if(size_t(segment_id)!=loaded->second.size())issue(list_name,"prior_segment_count_options","error",0,0,"step "+std::to_string(step),"Segment count disagrees with the options/map schedule.",
                    "The engine expects a different number of segments at this step.","Restore matching options, map and priors.");
            }
        }
    } else if(!options_ok)report.blocked.push_back("Prior/options schedule comparison: findhap.options is invalid.");

    if(regular("hap.foundpriors")) {
        const std::string name="hap.foundpriors";Lines found(report,directory,name,64*1024*1024);
        bool found_header=found&&headers(found,name,10,found_steps);
        if(list_header&&found_header&&list_steps!=found_steps)issue(name,"prior_header_steps_mismatch","error",1,11,"steps","The two prior files declare different step counts.",
            "Prior files must originate from the same export.","Restore a matched prior set.");
        int last_step=0,found_sentinel=0;std::set<int> seen_steps;
        struct Seen {unsigned mask=0;size_t line=0;};std::unordered_map<int,Seen> seen;
        auto finish_step=[&](){for(const auto& a:seen)if(a.second.mask!=3)issue(name,"missing_prior_parent_record","error",a.second.line,11,"parent",
            "Only one of the two parental haplotype records is present for this animal/step.","Each represented prior animal needs records for parent 1 and parent 2.","Restore the matching parental record from the prior export.");seen.clear();};
        while(found.next(row)) {
            if(found.oversized)continue;
            auto f=tokens(row);
            if(f.size()<4){issue(name,"truncated_found_prior","error",found.line,row.size()+1,"header","Expected animal, parent, step and segment count.",
                "Prior assignments require all four header values.","Restore the complete assignment record.");continue;}
            int id=0,parent=0,step=0,segments=0;int* out[]={&id,&parent,&step,&segments};bool okay=true;
            const char* names[]={"animal","parent","step","segments"};
            for(int i=0;i<4;++i)okay=numeric(name,found.line,f[i],names[i],*out[i])&&okay;
            if(!okay)continue;
            if(id==0&&parent==0&&segments==0) {
                if(step!=last_step+1||f.size()!=4||found_sentinel||(found_header&&step>found_steps))issue(name,"invalid_found_sentinel","error",found.line,1,"sentinel","Invalid next-step sentinel.",
                    "Sentinels must have zero animal, parent and segment count, and identify the next step.","Restore the original step boundaries.");
                found_sentinel=step;continue;
            }
            if(id<1||parent<1||parent>2||step<1||segments<1||(found_header&&step>found_steps)) {
                issue(name,"found_prior_domain","error",found.line,1,"header","Invalid ID, parental role, step or segment count.",
                    "Assignment indices must fit the declared prior layout.","Use positive IDs, parent 1/2 and valid step/segment counts.");continue;}
            if(step<last_step)issue(name,"found_step_order","error",found.line,f[2].column,"step","Step order decreases.",
                "The engine consumes assignments in step order.","Restore original complete-record ordering.");
            if(last_step&&step>last_step&&found_sentinel!=step)issue(name,"missing_found_sentinel","error",found.line,f[2].column,"step",
                "New assignment step has no preceding sentinel.","The engine can consume the first assignment as a step terminator.","Restore the original assignment step separators.");
            if(step!=last_step){finish_step();last_step=step;found_sentinel=0;}
            seen_steps.insert(step);
            auto& identity=seen[id];
            if(!identity.line)identity.line=found.line;
            if(identity.mask&(1u<<(parent-1)))issue(name,"duplicate_prior_assignment","error",found.line,f[1].column,"parent","Duplicate animal/parent/step assignment.",
                "Later assignments can overwrite earlier haplotype choices.","Resolve duplicate assignments through a consistent source re-export.");
            identity.mask|=1u<<(parent-1);
            if(!animal_index.count(id))issue(name,"prior_animal_outside_pedigree","warning",found.line,f[0].column,"animal","Prior animal is absent from the current pedigree.",
                "The engine ignores this animal's assignments; retaining old reference animals can be intentional.","Confirm this is an intended reference/cohort difference rather than an ID mismatch.");
            auto geometry=library.find(step);
            if(geometry==library.end())issue(name,"unknown_prior_assignment_step","error",found.line,f[2].column,"step","No matching library step.",
                "Assignments must refer to a loaded library step.","Restore matching prior files.");
            else if(size_t(segments)!=geometry->second.size())issue(name,"prior_assignment_segments","error",found.line,f[3].column,"segments","Segment count differs from the library step.",
                "Each assignment vector must have one value per library segment.","Restore the complete matching assignment layout.");
            if(size_t(segments)!=f.size()-4)issue(name,"prior_assignment_width","error",found.line,row.size()+1,"haplotype references","Declared segment count differs from the number of assignment fields.",
                "Missing or extra references can shift all later segment assignments.","Re-export the complete record rather than padding or truncating it.");
            for(size_t i=4;i<f.size();++i) {
                int hap=0;if(!numeric(name,found.line,f[i],"haplotype reference",hap))continue;
                if(hap<0||(options_ok&&hap>options.controls[5]))issue(name,"prior_reference_range","error",found.line,f[i].column,"haplotype reference","Reference is negative or exceeds maxhap.",
                    "Zero is unknown; positive values identify library haplotypes.","Restore the correct assignment and compatible maxhap.");
                else if(hap>0&&geometry!=library.end()) {
                    auto segment=geometry->second.find(int(i-3));
                    if(segment==geometry->second.end()||!segment->second.haplotypes.count(hap))issue(name,"unknown_prior_haplotype","error",found.line,f[i].column,"segment "+std::to_string(i-3),"Referenced positive haplotype is absent from this library step/segment.",
                        "Matching numeric ranges alone do not establish a valid prior reference.","Use the hap.listpriors that belongs to these assignments; do not renumber independently.");
                }
            }
            ++report.metrics["prior_assignment_records"];
        }
        finish_step();
        if(found_header&&found_steps<=100000)for(int step=1;step<=found_steps;++step)if(!seen_steps.count(step))
            issue(name,"missing_found_step","error",0,0,"step "+std::to_string(step),"No animal assignments for a declared prior step.",
                "A complete prior import needs assignments for the declared library steps.","Restore the matching complete assignment export.");
    }
    if(regular("hap.Xseg")) {
        const std::string name="hap.Xseg";Lines xseg(report,directory,name);int values[2]={-1,-1};
        for(int i=0;i<2;++i) {
            if(!xseg.next(row)){issue(name,"missing_xseg_record","error",size_t(i+1),1,"record","Missing one of the two reference summary records.",
                "hap.Xseg contains the high-density reference count and X-segment boundary.","Restore the matching original hap.Xseg file.");break;}
            auto fields=tokens(row);
            if(fields.empty()){issue(name,"blank_xseg_record","error",xseg.line,1,"count/boundary","Expected a leading integer on this summary record.",
                "Both hap.Xseg records contain required numeric values.","Restore the complete matching prior summary.");continue;}
            if(!numeric(name,xseg.line,fields[0],i==0?"high_density_count":"X_segment",values[i]))continue;
            if(values[i]<(i==0?0:1))issue(name,"xseg_domain","error",xseg.line,1,"count/boundary","Negative reference count or nonpositive segment boundary.",
                "The reference count must be nonnegative and X boundary positive.","Restore the matching prior summary.");
        }
        high_density=values[0];x_segment=values[1];
        if(!library.empty()&&x_segment>0) {
            const auto& last=library.rbegin()->second;
            int expected=int(last.size())+1;
            if(options_ok&&options.controls[1]>0)for(const auto& s:last)if(s.second.chromosome==options.controls[1]){expected=s.first;break;}
            if(x_segment!=expected)issue(name,"xseg_library_mismatch","error",2,1,"X_segment","Boundary disagrees with the final prior library/map.",
                "The summary may belong to another reference export.","Restore hap.Xseg from the same prior run and settings.");
        }
        while(xseg.next(row))if(!trim(row).empty())issue(name,"extra_xseg_record","warning",xseg.line,1,"record","Extra nonempty record after the two expected summary records.",
            "The engine does not require this extra record.","Confirm the file is an unmodified prior summary export.");
        if(high_density>=0)report.metrics["prior_high_density_reference_count"]=uint64_t(high_density);
    }
    uint64_t groups=0,haplotypes=0;
    for(const auto& s:library)for(const auto& g:s.second){++groups;haplotypes+=g.second.haplotypes.size();}
    report.metrics["prior_step_segment_groups"]=groups;report.metrics["prior_library_haplotypes"]=haplotypes;
}
} // namespace turbohap_audit
