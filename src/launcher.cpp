#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <iostream>
#include <string>
#include <system_error>
#include <unistd.h>
#include "platform_policy.hpp"
#include "thread_request.hpp"
#include "macos_policy.hpp"
#include "cpu_policy.hpp"
#if FINDHAP_PEDIGREE_PLACEMENT
#include "pedigree_placement.hpp"
#endif
#include "row_schedule.hpp"
#include "ancestor_quality_filter.hpp"
#include "processing_scope.hpp"
#include "input_validation.hpp"
#include "progeny_x.hpp"
#include "stdout_policy.hpp"

namespace findhap_standard { int run(int argc, char* argv[]); }
namespace findhap_low_memory { int run(int argc, char* argv[]); }
namespace findhap_dynamic_standard { int run(int argc, char* argv[]); }
namespace findhap_dynamic_low_memory { int run(int argc, char* argv[]); }

#if defined(__linux__) && FINDHAP_TURIN_BINDING
static constexpr char native_binding_restart[] = "TURBOHAP_INTERNAL_OPENMP_RESTART";

static bool resumed_native_binding() {
    const char* marker = std::getenv(native_binding_restart);
    const bool restarted = marker && marker == std::to_string(getpid());
    if (marker && ::unsetenv(native_binding_restart) != 0)
        throw std::system_error(errno, std::generic_category(), "cannot clear OpenMP restart marker");
    if (restarted) {
        const char* bind = std::getenv("OMP_PROC_BIND");
        const char* places = std::getenv("OMP_PLACES");
        if (!bind || std::strcmp(bind, "close") != 0 || !places || std::strcmp(places, "cores") != 0 ||
            std::getenv("GOMP_CPU_AFFINITY"))
            throw std::runtime_error("OpenMP restart did not retain the automatic placement settings");
    }
    return restarted;
}

[[noreturn]] static void restart_with_native_binding(char* const argv[]) {
    const auto pid = std::to_string(getpid());
    if (::setenv("OMP_PROC_BIND", "close", 0) || ::setenv("OMP_PLACES", "cores", 0) ||
        ::setenv(native_binding_restart, pid.c_str(), 1))
        throw std::system_error(errno, std::generic_category(), "cannot configure OpenMP placement");
    ::execv("/proc/self/exe", argv);
    throw std::system_error(errno, std::generic_category(), "cannot restart with OpenMP placement");
}
#endif

int main(int argc, char* argv[]) {
    using namespace findhap::placement;
    bool low_memory = false, cpu_info = false, policy_seen = false;
    Policy policy = Policy::automatic;
    findhap::rows::Policy row_policy = findhap::rows::Policy::automatic;
    bool row_policy_seen = false;
    bool validation_seen = false;
    bool stdout_seen = false;
    bool x_progeny_seen = false;
    bool processing_scope_seen = false;
    constexpr char x_progeny_prefix[] = "--x-progeny=";
    constexpr auto x_progeny_prefix_length = sizeof(x_progeny_prefix) - 1;
    constexpr char x_progeny_usage[] = "use --x-progeny=legacy or --x-progeny=corrected";
    constexpr char scope_prefix[] = "--processing-scope=";
    constexpr char scope_usage[] = "use --processing-scope=legacy or --processing-scope=chromosome";
    std::vector<char*> original(argv, argv + argc);
    original.push_back(nullptr);
    int count = 1;
    try {
        for (int i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "--low-memory") == 0) low_memory = true;
            else if (std::strcmp(argv[i], "--cpu-info") == 0) cpu_info = true;
            else if (std::strncmp(argv[i], x_progeny_prefix, x_progeny_prefix_length) == 0) {
                const char* value = argv[i] + x_progeny_prefix_length;
                const bool corrected = std::strcmp(value, "corrected") == 0;
                if (!corrected && std::strcmp(value, "legacy") != 0)
                    throw std::runtime_error(x_progeny_usage);
                if (x_progeny_seen && corrected != findhap::progeny::fix_x_progeny)
                    throw std::runtime_error("conflicting --x-progeny values");
                findhap::progeny::fix_x_progeny = corrected;
                x_progeny_seen = true;
            } else if (std::strcmp(argv[i], "--x-progeny") == 0 ||
                       std::strcmp(argv[i], "--fix-x-progeny") == 0 ||
                       std::strncmp(argv[i], "--fix-x-progeny=", sizeof("--fix-x-progeny=") - 1) == 0)
                throw std::runtime_error(x_progeny_usage);
            else if (std::strncmp(argv[i], scope_prefix, sizeof(scope_prefix) - 1) == 0) {
                const char* value = argv[i] + sizeof(scope_prefix) - 1;
                const bool by_chromosome = std::strcmp(value, "chromosome") == 0;
                if (!by_chromosome && std::strcmp(value, "legacy") != 0)
                    throw std::runtime_error(scope_usage);
                if (processing_scope_seen && by_chromosome != findhap::processing::by_chromosome)
                    throw std::runtime_error("conflicting --processing-scope values");
                findhap::processing::by_chromosome = by_chromosome;
                processing_scope_seen = true;
            } else if (std::strcmp(argv[i], "--processing-scope") == 0)
                throw std::runtime_error(scope_usage);
            else if (std::strncmp(argv[i], "--stdout=", 9) == 0) {
                const auto value = turbohap_reporting::parse_stdout_mode(argv[i] + 9);
                if (stdout_seen && value != turbohap_reporting::stdout_mode)
                    throw std::runtime_error("conflicting --stdout values");
                turbohap_reporting::stdout_mode = value;
                stdout_seen = true;
            } else if (std::strncmp(argv[i], "--stdout", 8) == 0)
                throw std::runtime_error("use --stdout=legacy or --stdout=modern");
            else if (std::strncmp(argv[i], "--input-validation=", 19) == 0) {
                const auto value = findhap::input::parse_policy(argv[i] + 19);
                if (validation_seen && value != findhap::input::policy)
                    throw std::runtime_error("conflicting --input-validation values");
                findhap::input::policy = value; validation_seen = true;
            } else if (std::strncmp(argv[i], "--input-validation", 18) == 0)
                throw std::runtime_error("use --input-validation=strict or --input-validation=warn");
            else if (std::strncmp(argv[i], "--row-schedule=", 15) == 0) {
                const auto value = findhap::rows::parse(argv[i] + 15);
                if (row_policy_seen && value != row_policy) throw std::runtime_error("conflicting --row-schedule values");
                row_policy = value; row_policy_seen = true;
            } else if (std::strncmp(argv[i], "--row-schedule", 14) == 0)
                throw std::runtime_error("use --row-schedule=auto, --row-schedule=static or --row-schedule=dynamic");
            else if (std::strncmp(argv[i], "--cpu-policy=", 13) == 0) {
                const Policy value = parse(argv[i] + 13);
                if (policy_seen && value != policy) throw std::runtime_error("conflicting --cpu-policy values");
                policy = value; policy_seen = true;
            } else if (std::strncmp(argv[i], "--cpu-policy", 12) == 0)
                throw std::runtime_error("use --cpu-policy=auto, --cpu-policy=inherit or --cpu-policy=performance");
            else if (std::strncmp(argv[i], "--", 2) == 0 && argv[i][2] != '\0' &&
                     std::strcmp(argv[i], "--help") != 0)
                throw std::runtime_error(std::string("unknown option ") + argv[i] +
                                         ". Run findhap --help for available options.");
            else argv[count++] = argv[i];
        }
        if (findhap::processing::by_chromosome &&
            (findhap::whole_genome_high_density || findhap::defer_ancestor_quality_filter ||
             findhap::defer_genotyped_quality_filter))
            throw std::runtime_error("chromosome processing requires local coverage and active output quality filters");
        // Resolve after parsing so chromosome scope wins in either argument order.
        // Keep the requested value for an explicit, one-time override diagnostic.
        const bool x_progeny_overridden = findhap::processing::by_chromosome &&
            x_progeny_seen && !findhap::progeny::fix_x_progeny;
        if (findhap::processing::by_chromosome)
            findhap::progeny::fix_x_progeny = true;
        argv[count] = nullptr;
        if (count > 1 && std::strcmp(argv[1], "--help") == 0) {
            std::puts(
                "TurboHap\n"
                "Usage: findhap [threads] [--low-memory] [--cpu-policy=auto|inherit|performance]\n"
                "               [--row-schedule=auto|static|dynamic] [--cpu-info]\n"
                "               [--input-validation=strict|warn] [--stdout=legacy|modern]\n"
                "               [--x-progeny=legacy|corrected] [--processing-scope=legacy|chromosome]\n\n"
                "Standard mode prioritizes speed. --low-memory reduces peak memory.\n"
                "Options may appear before or after the thread count.\n"
                "Without a thread count, honour OMP_NUM_THREADS; native Apple Silicon auto uses P-core count.\n"
                "auto: qualified Spark selection; native Apple Silicon uses advisory user-initiated QoS.\n"
                "inherit: leave CPU placement, QoS and default thread count to the caller/OpenMP runtime.\n"
                "performance: require a qualified, sufficient performance-core allocation.\n"
                "Explicit OpenMP placement and inherited CPU restrictions take precedence in auto.\n"
                "--row-schedule=auto|static|dynamic: independent-row work distribution (default auto).\n"
                "auto uses dynamic chunks of four when the request spans reliably ranked core types.\n"
                "Otherwise retain static rows; explicit OpenMP settings and dynamic teams keep static.\n"
                "--cpu-info: show the proposed CPU policy as JSON without applying it or reading inputs.\n"
                "--input-validation=strict|warn: validate genotype layout before inference (default strict).\n"
                "warn permits safe legacy layout ambiguities for diagnostics; results may be misaligned.\n"
                "Unsafe headers, sizes, truncated records and unsupported genotype codes always fail.\n"
                "--x-progeny=legacy|corrected: parental evidence when inferring ungenotyped males from offspring.\n"
                "legacy (default) preserves FindHap v3 scoring. corrected uses segment-specific evidence and can change result files.\n"
                "--processing-scope=legacy|chromosome: use genome-wide rules (default) or score and report each chromosome separately.\n"
                "Chromosome scope always uses corrected X-progeny scoring, overriding an explicit --x-progeny=legacy.\n"
                "Chromosome scope writes one result set per chromosome under chromosome-results/.\n"
                "--stdout=legacy|modern: choose console presentation; result files are unchanged.\n"
#if FINDHAP_MODERN_STDOUT_DEFAULT
                "Default STDOUT: modern (UTC/elapsed progress and live library-capacity summaries).\n"
#else
                "Default STDOUT: legacy (FindHap-style messages and numerical summaries).\n"
#endif
                "Original findhap.options fields and input/output filenames are unchanged.\n"
                "Trailing legacy positional arguments are ignored. Unknown long options are rejected.");




            return 0;
        }
        findhap::macos::configure(policy == Policy::automatic);
        findhap::startup_thread_request = findhap::resolve_thread_request(count, argv);
        if (count == 1)
            findhap::startup_thread_request.effective = findhap::macos::default_threads(
                findhap::startup_thread_request.openmp_limit, false);
        const auto request = findhap::startup_thread_request;
#if defined(__linux__) && FINDHAP_TURIN_BINDING
        const bool restarted_binding = resumed_native_binding();
        const bool propose_binding = policy == Policy::automatic && !restarted_binding && !external_binding();
        if (propose_binding && !cpu_info) restart_with_native_binding(original.data());
#else
        constexpr bool restarted_binding = false, propose_binding = false;
#endif
        const bool automatic_binding = restarted_binding || propose_binding;
        Topology topology;
        auto decision = start(policy, request.effective, cpu_info, topology);
#if FINDHAP_PEDIGREE_PLACEMENT
        findhap::pedigree_placement::inherit_placement = policy == Policy::inherit;
        findhap::pedigree_placement::allow_default_placement = TURBOHAP_NEON_WORKLOAD_PROFILE &&
            policy == Policy::automatic && topology.qualified && !topology.online.empty() &&
            decision.inherited == topology.online && !external_binding() && !omp_get_dynamic();
#endif
        if (findhap::macos::enabled)
            decision.reason = "advisory user-initiated QoS; OS controls core placement";
        if (propose_binding) decision.reason = "automatic OpenMP binding proposed (close on cores)";
        if (restarted_binding) {
            decision.reason = omp_get_proc_bind() == omp_proc_bind_close && omp_get_num_places() > 0
                ? "automatic OpenMP binding active (close on cores)"
                : "automatic OpenMP binding requested; runtime placement unavailable";
        }
        const findhap::rows::Request row_request{
            request.effective, omp_get_thread_limit(), bool(omp_get_dynamic()),
            external_binding() && !restarted_binding, std::getenv("OMP_SCHEDULE") != nullptr,
            automatic_binding};
        if (cpu_info) {
            const auto rows = findhap::rows::resolve(row_policy, row_request, decision.selected);
            auto details = findhap::rows::json(rows);
            if (propose_binding) {
                details += ",\"openmp_binding\":{\"source\":\"automatic\",\"status\":\"proposed\","
                           "\"proc_bind\":\"close\",\"places\":\"cores\"}";
            }
            information(std::cout, policy, request.effective, request.openmp_limit, topology, decision,
                        details + (findhap::macos::json().empty() ? "" : "," + findhap::macos::json()),
                        propose_binding ? "Linux OpenMP core places" :
                            findhap::macos::enabled ? "Darwin QoS (advisory)" : "");
            return 0;
        }
        findhap::macos::worker_enter();
        diagnostic(std::cerr, policy, request.effective, decision);
        if (findhap::processing::by_chromosome) {
            std::cerr << "TurboHap warning: chromosome processing automatically uses "
                         "corrected X-progeny scoring. ";
            if (x_progeny_overridden)
                std::cerr << "Your --x-progeny=legacy setting has been overridden. ";
            std::cerr << "There is no need to specify --x-progeny separately.\n";
        } else if (findhap::progeny::fix_x_progeny)
            std::cerr << "TurboHap: segment-specific X progeny scoring enabled\n";
        if (!findhap::macos::json().empty())
            std::cerr << "TurboHap Mac scheduling: " << findhap::macos::json() << '\n';
        std::vector<int> allowed;
#if defined(__linux__)
        try { allowed = affinity(); } catch (const std::exception&) {}
#endif
        const auto rows = findhap::rows::resolve(row_policy, row_request, allowed);
        findhap::rows::diagnostic(std::cerr, rows);
        if (rows.dynamic)
            return low_memory ? findhap_dynamic_low_memory::run(count, argv)
                              : findhap_dynamic_standard::run(count, argv);
        return low_memory ? findhap_low_memory::run(count, argv)
                          : findhap_standard::run(count, argv);
    } catch (const std::exception& error) {
        std::cerr << "TurboHap: " << error.what() << '\n';
        return 2;
    }
}
