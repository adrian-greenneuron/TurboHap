#include "../src/row_schedule.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <unistd.h> // mkdtemp on Darwin

using findhap::placement::Topology;
using findhap::rows::Policy;
using findhap::rows::Request;
using findhap::rows::decide;
static const Request normal{1, 1000, false, false, false};
int main() {
    // An unseen 3+2-core topology, arbitrary kinds and sparse/high logical IDs.
    Topology t{{1,4,80,81,8193},{1,4,80,81,8193},
        {{1,997,12345,{1}},{4,1017,12345,{4}},{8193,1024,12345,{8193}},
         {80,718,67890,{80}},{81,731,67890,{81}}},false};
    auto choose = [&](int n) { auto r=normal; r.threads=n; return decide(Policy::automatic,r,t); };
    for (int n : {1,2,3}) assert(!choose(n).dynamic);
    for (int n : {4,5}) {
        auto d=choose(n);
        assert(d.dynamic && d.fastest_physical_cores==3 && d.permitted_physical_cores==5);
    }
    assert(!choose(6).dynamic); // No automatic oversubscription.
    auto capped=normal;capped.threads=5;capped.thread_limit=3;
    assert(!decide(Policy::automatic,capped,t).dynamic);
    for (int which : {0,1,2}) {
        auto r=normal;r.threads=5;
        if(which==0)r.dynamic_teams=true;
        if(which==1)r.explicit_placement=true;
        if(which==2)r.explicit_schedule=true;
        assert(!decide(Policy::automatic,r,t).dynamic);
        assert(decide(Policy::dynamic_four,r,t).dynamic);
    }
    assert(!decide(Policy::fixed_static,Request{5,1000,false,false,false},t).dynamic);
    auto uncertain=t;uncertain.cpus[0].capacity=700;
    assert(!decide(Policy::automatic,Request{4,1000,false,false,false},uncertain).dynamic);
    auto incomplete=t;incomplete.cpus.pop_back();
    assert(!decide(Policy::automatic,Request{4,1000,false,false,false},incomplete).dynamic);
    auto malformed=t;malformed.cpus[0].siblings={4};
    assert(!decide(Policy::automatic,Request{4,1000,false,false,false},malformed).dynamic);
    // Smaller inherited allocations determine the boundary; never widen a mask.
    auto subset=t;subset.allowed={1,80,81};
    assert(decide(Policy::automatic,Request{2,1000,false,false,false},subset).dynamic);
    subset.allowed={1,4,8193};
    assert(!decide(Policy::automatic,Request{3,1000,false,false,false},subset).dynamic);
    // Small capacity variation within ONE architecture is not a new core type.
    auto homogeneous=t;for(auto& c:homogeneous.cpus)c.kind=12345;
    assert(!decide(Policy::automatic,Request{4,1000,false,false,false},homogeneous).dynamic);
    // SMT is not heterogeneous hardware: do not equate a second sibling with
    // another lower-capacity architecture, even beyond the physical-core count.
    Topology smt{{0,1,2,3},{0,1,2,3},
        {{0,1024,7,{0,2}},{2,1024,7,{0,2}},{1,1024,7,{1,3}},{3,1024,7,{1,3}}},false};
    for(int n:{1,2,3,4})assert(!decide(Policy::automatic,Request{n,1000,false,false,false},smt).dynamic);
    // Three unseen core classes: choose threshold from the fastest group.
    auto three=t;three.cpus[0].kind=999;three.cpus[0].capacity=1500;
    assert(decide(Policy::automatic,Request{2,1000,false,false,false},three).dynamic);
    assert(findhap::rows::parse("auto")==Policy::automatic);
    bool failed=false;try{findhap::rows::parse("adaptive");}catch(const std::runtime_error&){failed=true;}
    assert(failed);
    auto missing=findhap::rows::resolve(Policy::automatic,normal,{});
    assert(!missing.dynamic);

    // Exercise the real reader against synthetic sysfs. Identifiers are not
    // Spark part IDs; both MIDR and generic Linux core_type are supported.
    char pattern[]="/tmp/turbohap-row-topology-XXXXXX";
    const char* tmp=mkdtemp(pattern);assert(tmp);
    const std::filesystem::path root(tmp);
    auto put=[&](const std::string& path,const std::string& value){
        auto file=root/path;std::filesystem::create_directories(file.parent_path());
        std::ofstream out(file);out<<value<<'\n';assert(out.good());
    };
    put("cpu7/cpu_capacity","1024");put("cpu7/regs/identification/midr_el1","0x4100abc0");
    put("cpu7/topology/thread_siblings_list","7");
    put("cpu99/cpu_capacity","512");put("cpu99/regs/identification/midr_el1","0x4100def0");
    put("cpu99/topology/thread_siblings_list","99");
    auto read=findhap::rows::discover({7,99},root.string());
    assert(decide(Policy::automatic,Request{2,1000,false,false,false},read).dynamic);
    std::filesystem::remove(root/"cpu7/regs/identification/midr_el1");
    std::filesystem::remove(root/"cpu99/regs/identification/midr_el1");
    put("cpu7/topology/core_type","2");put("cpu99/topology/core_type","1");
    read=findhap::rows::discover({7,99},root.string());
    assert(decide(Policy::automatic,Request{2,1000,false,false,false},read).dynamic);
    std::filesystem::remove_all(root);
    std::cout<<"PASS: unseen 2/3-class topologies, within-class variation, restrictions, SMT, limits, overrides and metadata readers\n";
}
