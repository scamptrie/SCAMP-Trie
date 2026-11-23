#include "scamptrie_dynamic.h"
#include "moo/moo_weights.h"
#include "moo/dyn_annealing.h"

#include <chrono>
#include <cmath>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

// -----------------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------------
static constexpr uint64_t kEvalTotalRam = 10 * KILO_BYTES;

static const std::string kScampEvalOutputDefaultDir = "./results/raw/scamptrie";

// -----------------------------------------------------------------------------
// Path helpers
// -----------------------------------------------------------------------------
static bool ensure_directory_exists(const std::string& path) {
    if (path.empty()) return false;

    std::string normalized = path;
    while (!normalized.empty() && normalized.back() == '/') normalized.pop_back();
    if (normalized.empty()) return false;

    std::string current;
    if (!normalized.empty() && normalized[0] == '/') current = "/";

    std::stringstream ss(normalized);
    std::string part;
    while (std::getline(ss, part, '/')) {
        if (part.empty() || part == ".") continue;
        if (!current.empty() && current.back() != '/') current += "/";
        current += part;

        struct stat st;
        if (stat(current.c_str(), &st) == 0) {
            if (!S_ISDIR(st.st_mode)) {
                std::cerr << "path exists but is not directory: " << current << std::endl;
                return false;
            }
            continue;
        }
#ifdef _WIN32
        if (mkdir(current.c_str()) != 0 && errno != EEXIST) {
#else
        if (mkdir(current.c_str(), 0755) != 0 && errno != EEXIST) {
#endif
            std::cerr << "failed to create directory: " << current << " (errno=" << errno << ")" << std::endl;
            return false;
        }
    }
    return true;
}

static std::string fib_path_to_dataset_stem(const std::string& fib_path) {
    std::string s = fib_path;
    size_t last_sep = s.find_last_of("/\\");
    if (last_sep != std::string::npos) s = s.substr(last_sep + 1);
    const std::string ext = ".fib";
    if (s.size() > ext.size() && s.substr(s.size() - ext.size()) == ext)
        s = s.substr(0, s.size() - ext.size());
    return s;
}

// -----------------------------------------------------------------------------
// FIB & weights I/O (CLI-oriented)
// -----------------------------------------------------------------------------
static ip6_t parse_ipv6(const std::string& ip_str) {
    ip6_t ip = {0, 0};
    size_t dot_pos = ip_str.find('.');
    if (dot_pos != std::string::npos) {
        std::string hi_str = ip_str.substr(0, dot_pos);
        std::string lo_str = ip_str.substr(dot_pos + 1);
        ip.hi = std::strtoull(hi_str.c_str(), nullptr, 16);
        ip.lo = std::strtoull(lo_str.c_str(), nullptr, 16);
    } else {
        ip.hi = std::strtoull(ip_str.c_str(), nullptr, 16);
    }
    return ip;
}

static std::vector<FIBEntry> parse_fib_file(const std::string& filename) {
    std::vector<FIBEntry> entries;
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "cannot open FIB file: " << filename << std::endl;
        return entries;
    }

    std::string line;
    int count = 0;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        size_t slash_pos = line.find('/');
        if (slash_pos == std::string::npos) {
            std::cerr << "invalid prefix format: " << line << std::endl;
            exit(1);
        }
        std::string ip_str = line.substr(0, slash_pos);
        std::string len_str = line.substr(slash_pos + 1);
        ip6_t prefix = parse_ipv6(ip_str);
        uint8_t prefix_len = static_cast<uint8_t>(std::stoi(len_str));
        nexthop_t nexthop = static_cast<nexthop_t>(count + 1);
        entries.emplace_back(prefix, prefix_len, nexthop);
        ++count;
    }
    file.close();
    std::cout << "parsed " << count << " prefixes from " << filename << std::endl;
    return entries;
}

static bool write_weights_to_file(const std::string& filename, const MOOWeights& weights) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "cannot open parameter file for writing: " << filename << std::endl;
        return false;
    }
    file << std::fixed << std::setprecision(6);
    for (int i = 0; i < RAM_NUM; ++i) {
        file << weights[i];
        if (i < RAM_NUM - 1) file << " ";
    }
    file << std::endl;
    file.close();
    std::cout << "weights written to: " << filename << std::endl;
    return true;
}

static bool read_weights_from_file(const std::string& filename, MOOWeights& weights) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "cannot open parameter file for reading: " << filename << std::endl;
        return false;
    }
    // Evaluation accepts both the annealer's weight vector and the historical
    // ratio files with a leading stage count.
    std::vector<double> values;
    double value;
    while (file >> value) values.push_back(value);
    if (!file.eof()) return false;
    size_t start = 0;
    if (values.size() == RAM_NUM + 1 && values.front() == RAM_NUM) start = 1;
    if (values.size() != start + RAM_NUM) return false;
    double total = 0;
    for (int i = 0; i < RAM_NUM; ++i) {
        weights[i] = values[start + i];
        if (!std::isfinite(weights[i]) || weights[i] <= 0) return false;
        total += weights[i];
    }
    if (!std::isfinite(total)) return false;
    printf("weights loaded from: %s\n", filename.c_str());
    return true;
}

// -----------------------------------------------------------------------------
struct EvalResult {
    std::string fib_name;
    double usage_rate{};
    uint64_t ram_used{};
    uint64_t ram_allocated{};
    bool success{false};
};

struct EvalOptions {
    bool scamp_eval_output{false};
    std::string scamp_eval_output_dir = kScampEvalOutputDefaultDir;
    bool verify{false};
    bool basic{false};
    std::string stats_ram_path;
    std::string stats_types_path;
    std::string stats_benchmark_path;
    std::string stats_latency_samples_path;
    std::string stats_raise_samples_path;
};

static bool write_optional_scamp_eval_file(ScampTrieDynamic* trie,
                                         const std::string& fib_path,
                                         const std::string& out_dir,
                                         const EvalOptions& eo) {
    if (!ensure_directory_exists(out_dir)) {
        std::cerr << "Failed to prepare scamp-eval output dir: " << out_dir << std::endl;
        trie->print_insert_prefix_timing_stats(
            stdout, eo.stats_latency_samples_path.empty() ? nullptr : eo.stats_latency_samples_path.c_str());
        trie->print_insert_prefix_stage_raise_stats(
            stdout, eo.stats_raise_samples_path.empty() ? nullptr : eo.stats_raise_samples_path.c_str());
        return false;
    }

    uint64_t ram_used_per_stage[RAM_NUM] = {0};
    uint64_t fixed_capacity_bits = 0;
    uint64_t fixed_used_bits = 0;
    uint64_t elastic_used_bits = 0;
    trie->get_storage_usage(ram_used_per_stage, fixed_capacity_bits, fixed_used_bits, elastic_used_bits);

    const std::string dataset_stem = fib_path_to_dataset_stem(fib_path);
    std::string output_path = out_dir + "/" + dataset_stem;
    std::ofstream out(output_path);
    if (!out.is_open()) {
        std::cerr << "Failed to open output file: " << output_path << std::endl;
        trie->print_insert_prefix_timing_stats(
            stdout, eo.stats_latency_samples_path.empty() ? nullptr : eo.stats_latency_samples_path.c_str());
        trie->print_insert_prefix_stage_raise_stats(
            stdout, eo.stats_raise_samples_path.empty() ? nullptr : eo.stats_raise_samples_path.c_str());
        return false;
    }

    const int P = RAM_NUM;
    out << P << "\n";
    for (int i = 0; i < P; ++i) {
        uint64_t used_bits = ram_used_per_stage[i] * 8ULL;
        out << used_bits;
        if (i + 1 < P) out << " ";
    }
    out << "\n";
    out << fixed_capacity_bits << "\n";
    out << fixed_used_bits << "\n";
    out << elastic_used_bits << "\n";
    out.close();
    std::cout << "Evaluation output written to: " << output_path << std::endl;
    return true;
}

static void write_optional_stats_csv(ScampTrieDynamic* trie,
                                   const std::string& fib_path,
                                   const EvalOptions& eo) {
    const std::string ds = fib_path_to_dataset_stem(fib_path);

    if (!eo.stats_ram_path.empty()) {
        uint64_t ram_allocated_per_stage[RAM_NUM] = {0};
        uint64_t ram_used_per_stage[RAM_NUM] = {0};
        trie->get_stage_allocated(ram_allocated_per_stage);
        trie->get_stage_usage(ram_used_per_stage);
        std::ofstream out(eo.stats_ram_path);
        if (out.is_open()) {
            out << ds;
            for (int i = 0; i < RAM_NUM; ++i) out << "," << (ram_allocated_per_stage[i] * 8ULL);
            out << "\n" << ds;
            for (int i = 0; i < RAM_NUM; ++i) out << "," << (ram_used_per_stage[i] * 8ULL);
            out << "\n";
        }
    }
    if (!eo.stats_types_path.empty()) {
        uint32_t counts[RAM_NUM][7] = {{0}};
        trie->get_stage_type_counts(counts);
        std::ofstream out(eo.stats_types_path);
        if (out.is_open()) {
            const char* type_names[] = {"Hybrid Node(Type3)","Decoupled Node(Type2)","HS Decoupled Node(Type1)",
                "Collapsed Node(Type5)","HS Collapsed Node(Type4)","Total","ChildOfHybrid(Type3)"};
            for (int r = 0; r < 7; ++r) {
                out << ds << "," << type_names[r];
                uint32_t row_total = 0;
                for (int s = 0; s < RAM_NUM; ++s) {
                    out << "," << counts[s][r];
                    row_total += counts[s][r];
                }
                out << "," << row_total << "\n";
            }
        }
    }
}

// ---------------------------------------------------------------------------
// LPM Correctness Verification — brute-force reference for sanity checks
// ---------------------------------------------------------------------------

/// Brute-force LPM: linear scan over all FIB entries, pick longest match.
/// On equal-length matches the *last* entry wins, matching SCAMP-Trie's
/// insertion semantics (later insert overwrites).
static nexthop_t brute_force_lpm(const std::vector<FIBEntry>& fib, const ip6_t& ip) {
    nexthop_t best_nh = LPM_INVALID_NH;
    int best_len = -1;
    for (const auto& e : fib) {
        if (e.prefix_len >= best_len && is_prefix_equal(ip, e.prefix, e.prefix_len)) {
            best_len = e.prefix_len;
            best_nh = e.nexthop;
        }
    }
    return best_nh;
}

/// Verify SCAMP-Trie lookup correctness against brute-force ground truth.
/// Uses every FIB prefix's network address as a test IP (guaranteed match).
static bool verify_lookup_correctness(ScampTrieDynamic* trie,
                                       const std::vector<FIBEntry>& fib) {
    int n_errors = 0;
    int n_misses = 0;
    const int n_total = static_cast<int>(fib.size());

    printf("\n=== LPM Correctness Verification ===\n");
    for (int i = 0; i < n_total; ++i) {
        const ip6_t test_ip = fib[i].prefix;          // network address → guaranteed match
        const nexthop_t expected = brute_force_lpm(fib, test_ip);

        uint32_t bmp_idx = 0;
        const nexthop_t actual = trie->lookup(test_ip, bmp_idx);

        if (expected != actual) {
            if (n_errors < 10) {
                printf("  [ERR] #%d (len=%u): expected NH=%llu, got NH=%llu\n",
                       i, fib[i].prefix_len,
                       (unsigned long long)expected, (unsigned long long)actual);
            }
            ++n_errors;
            if (actual == LPM_INVALID_NH) ++n_misses;
        }

        if ((i + 1) % 50000 == 0)
            printf("  … verified %d/%d prefixes\n", i + 1, n_total);
    }

    if (n_errors == 0) {
        printf("  ALL %d LOOKUPS CORRECT\n", n_total);
    } else {
        printf("  %d/%d correct  —  %d ERRORS (%d misses)\n",
               n_total - n_errors, n_total, n_errors, n_misses);
    }
    return n_errors == 0;
}

// Single FIB: insert → reshape → optional exports (matches previous behavior).
static int eval_single_dataset(const std::string& fib_path,
                              const MOOWeights& weights,
                              EvalOptions eo) {
    printf("\n=== Parsing FIB File ===\n");
    std::vector<FIBEntry> fib_entries = parse_fib_file(fib_path);
    if (fib_entries.empty()) {
        printf("Failed to parse FIB file: %s\n", fib_path.c_str());
        return 1;
    }
    printf("Loaded %zu prefixes from FIB file\n", fib_entries.size());

    printf("\n=== Creating ScampTrieDynamic ===\n");
    std::unique_ptr<ScampTrieDynamic> trie(new ScampTrieDynamic());
    if (eo.basic) {
        trie->use_basic_mapping();
    } else {
        trie->set_ram_allocation(kEvalTotalRam, weights);
        printf("RAM allocation set to: %8.3lf KB\n", double(kEvalTotalRam) / KILO_BYTES);
    }

    printf("\n=== Inserting Prefixes ===\n");
    int count = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (const auto& entry : fib_entries) {
        uint32_t r = trie->insert_prefix(entry.prefix, entry.prefix_len, entry.nexthop);
        if (r != SCAMPTRIE_OK) {
            printf("Insert failed at prefix #%d\n", count);
            trie->print_insert_prefix_timing_stats(
                stdout, eo.stats_latency_samples_path.empty() ? nullptr : eo.stats_latency_samples_path.c_str());
            trie->print_insert_prefix_stage_raise_stats(
                stdout, eo.stats_raise_samples_path.empty() ? nullptr : eo.stats_raise_samples_path.c_str());
            return 1;
        }
        ++count;
    }
    auto t1 = std::chrono::steady_clock::now();
    double insert_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    printf("Successfully inserted %d prefixes (insert time: %.2f ms, %.0f prefixes/sec)\n",
           count, insert_ms, (insert_ms > 0 ? (count * 1000.0 / insert_ms) : 0.0));

    if (!eo.stats_benchmark_path.empty()) {
        std::ofstream out(eo.stats_benchmark_path);
        if (out.is_open())
            out << std::fixed << insert_ms << "," << count << ","
                << (insert_ms > 0 ? (count * 1000.0 / insert_ms) : 0.0) << "\n";
    }

    printf("\n=== Reshaping ===\n");
    if (trie->reshape() != SCAMPTRIE_OK) {
        printf("Reshape failed\n");
        trie->print_insert_prefix_timing_stats(
            stdout, eo.stats_latency_samples_path.empty() ? nullptr : eo.stats_latency_samples_path.c_str());
        trie->print_insert_prefix_stage_raise_stats(
            stdout, eo.stats_raise_samples_path.empty() ? nullptr : eo.stats_raise_samples_path.c_str());
        return 1;
    }

    if (eo.scamp_eval_output) {
        if (!write_optional_scamp_eval_file(trie.get(), fib_path, eo.scamp_eval_output_dir, eo))
            return 1;
    }

    write_optional_stats_csv(trie.get(), fib_path, eo);

    if (eo.verify && !verify_lookup_correctness(trie.get(), fib_entries)) return 1;

    trie->print_insert_prefix_timing_stats(
        stdout, eo.stats_latency_samples_path.empty() ? nullptr : eo.stats_latency_samples_path.c_str());
    trie->print_insert_prefix_stage_raise_stats(
        stdout, eo.stats_raise_samples_path.empty() ? nullptr : eo.stats_raise_samples_path.c_str());
    printf("=== Dynamic Mode Test Completed ===\n");
    return 0;
}

static std::string fib_path_basename(const std::string& path) {
    size_t last = path.find_last_of("/\\");
    return last == std::string::npos ? path : path.substr(last + 1);
}

static void print_results_table(const std::vector<EvalResult>& results) {
    printf("\n=== Evaluation Results Table ===\n");
    std::string sep = "+" + std::string(50, '-') + "+" + std::string(12, '-') + "+" +
                      std::string(15, '-') + "+" + std::string(15, '-') + "+\n";
    printf("%s", sep.c_str());
    printf("|%-50s|%12s|%15s|%15s|\n", "Dataset", "Usage Rate", "RAM Used (KB)", "RAM Alloc (KB)");
    printf("%s", sep.c_str());
    for (const auto& res : results) {
        if (res.success) {
            std::string display = fib_path_basename(res.fib_name);
            printf("|%-50s|%11.2lf%%|%15.2lf|%15.2lf|\n",
                   display.c_str(),
                   res.usage_rate * 100,
                   double(res.ram_used) / KILO_BYTES,
                   double(res.ram_allocated) / KILO_BYTES);
        } else {
            printf("|%-50s|%12s|%15s|%15s|\n",
                   res.fib_name.c_str(), "FAILED", "N/A", "N/A");
        }
    }
    printf("%s", sep.c_str());
}

// Multiple FIB: per-dataset trie, no reshape (consistent with DynAnnealing eval).
static int eval_multi_dataset_table(const std::vector<std::string>& fib_paths,
                                   const MOOWeights& weights) {
    std::vector<EvalResult> results(fib_paths.size());

    for (size_t i = 0; i < fib_paths.size(); ++i) {
        const std::string& fib_file = fib_paths[i];
        results[i].fib_name = fib_file;

        std::vector<FIBEntry> fib_entries = parse_fib_file(fib_file);
        if (fib_entries.empty()) {
            printf("Failed to parse FIB file: %s\n", fib_file.c_str());
            continue;
        }

        std::unique_ptr<ScampTrieDynamic> trie(new ScampTrieDynamic());
        trie->set_ram_allocation(kEvalTotalRam, weights);

        bool ok = true;
        for (const auto& entry : fib_entries) {
            if (trie->insert_prefix(entry.prefix, entry.prefix_len, entry.nexthop) != SCAMPTRIE_OK) {
                ok = false;
                break;
            }
        }
        if (!ok) {
            printf("Insert failed for FIB file: %s\n", fib_file.c_str());
            trie->print_insert_prefix_timing_stats(stdout);
            trie->print_insert_prefix_stage_raise_stats(stdout);
            continue;
        }

        uint64_t ram_used, ram_allocated;
        uint32_t tcam_unused;
        trie->get_ram_info(ram_used, ram_allocated, tcam_unused);
        (void)tcam_unused;
        results[i].usage_rate = 1.0 * ram_used / ram_allocated;
        results[i].ram_used = ram_used;
        results[i].ram_allocated = ram_allocated;
        results[i].success = true;
        trie->print_insert_prefix_timing_stats(stdout);
        trie->print_insert_prefix_stage_raise_stats(stdout);
    }

    print_results_table(results);
    printf("\n=== Dynamic Mode Test Completed ===\n");
    return 0;
}

static int run_dynamic_eval(const std::vector<std::string>& fib_paths,
                           const std::string& param_file,
                           EvalOptions eo) {
    printf("=== Running Dynamic Mode Test ===\n");
    printf("\n=== Loading Weights for Evaluation ===\n");

    MOOWeights loaded_weights{};
    if (!eo.basic && !read_weights_from_file(param_file, loaded_weights)) {
        std::cerr << "Failed to load weights from parameter file" << std::endl;
        return 1;
    }

    if (fib_paths.size() == 1)
        return eval_single_dataset(fib_paths[0], loaded_weights, eo);
    return eval_multi_dataset_table(fib_paths, loaded_weights);
}

static int run_dynamic_gen(std::vector<std::vector<FIBEntry>>& fib_entries_all,
                           const std::string& output_file) {
    printf("=== Running Dynamic Mode Test ===\n");
    DynAnnealing dyn_annealing;
    dyn_annealing.init(fib_entries_all, true, true);
    dyn_annealing.iteration();
    MOOWeights final_weights = dyn_annealing.get_final_weights();
    if (!write_weights_to_file(output_file, final_weights)) {
        std::cerr << "Failed to write weights to parameter file" << std::endl;
        return 1;
    }
    printf("Weights written to: %s\n", output_file.c_str());
    return 0;
}

static void print_usage(const char* program_name) {
    std::cout << "usage: " << program_name << " [options]\n\n";
    std::cout << "Required: at least one -F <fib_file>; -W <weights_file> except with --basic\n";
    std::cout << "Modes:\n";
    std::cout << "  --eval   Evaluate (load weights, insert prefixes)\n";
    std::cout << "  --gen    Generation / annealing (write optimized weights to -W)\n";
    std::cout << "\n";
    std::cout << "-F <fib_file>                   FIB path (repeatable)\n";
    std::cout << "-W <param_file>                  MOO weights file\n";
    std::cout << "--scamp-eval-output               5-line eval file (requires --eval, single -F)\n";
    std::cout << "--scamp-eval-output-dir <path>   base dir for 5-line file (default: " << kScampEvalOutputDefaultDir << ")\n";
    std::cout << "--basic                          Section 3.2 mapping (single FIB evaluation)\n";
    std::cout << "--verify                         verify LPM correctness after insertion (requires --eval)\n";
    std::cout << "--stats-ram <path>\n";
    std::cout << "--stats-types <path>\n";
    std::cout << "--stats-benchmark <path>\n";
    std::cout << "--stats-latency-samples <path>\n";
    std::cout << "--stats-raise-samples <path>\n";
    std::cout << "(the five --stats-* options require exactly one -F)\n";
}

struct AppArgv {
    std::vector<std::string> fib_paths;
    std::string weights_path;
    bool gen_mode{false};
    bool eval_mode{false};
    EvalOptions eval{};
};

static int parse_argv(int argc, char** argv, AppArgv& out, const char*& err) {
    err = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-F") == 0 && i + 1 < argc) {
            out.fib_paths.push_back(argv[++i]);
        } else if (strcmp(argv[i], "-W") == 0 && i + 1 < argc) {
            out.weights_path = argv[++i];
        } else if (strcmp(argv[i], "--gen") == 0) {
            out.gen_mode = true;
        } else if (strcmp(argv[i], "--eval") == 0) {
            out.eval_mode = true;
        } else if (strcmp(argv[i], "--scamp-eval-output") == 0) {
            out.eval.scamp_eval_output = true;
        } else if (strcmp(argv[i], "--scamp-eval-output-dir") == 0 && i + 1 < argc) {
            out.eval.scamp_eval_output_dir = argv[++i];
        } else if (strcmp(argv[i], "--basic") == 0) {
            out.eval.basic = true;
        } else if (strcmp(argv[i], "--verify") == 0) {
            out.eval.verify = true;
        } else if (strcmp(argv[i], "--stats-ram") == 0 && i + 1 < argc) {
            out.eval.stats_ram_path = argv[++i];
        } else if (strcmp(argv[i], "--stats-types") == 0 && i + 1 < argc) {
            out.eval.stats_types_path = argv[++i];
        } else if (strcmp(argv[i], "--stats-benchmark") == 0 && i + 1 < argc) {
            out.eval.stats_benchmark_path = argv[++i];
        } else if (strcmp(argv[i], "--stats-latency-samples") == 0 && i + 1 < argc) {
            out.eval.stats_latency_samples_path = argv[++i];
        } else if (strcmp(argv[i], "--stats-raise-samples") == 0 && i + 1 < argc) {
            out.eval.stats_raise_samples_path = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return -1;
        }
        /* else: ignore unrecognized argv (legacy) */
    }

    if (out.fib_paths.empty()) {
        err = "at least one FIB file (-F) is required";
        return 1;
    }
    if (out.eval.basic && (!out.eval_mode || out.gen_mode || out.fib_paths.size() != 1 || !out.weights_path.empty())) {
        err = "--basic requires --eval, one -F, and no mapping weights (-W)";
        return 1;
    }
    if (!out.eval.basic && out.weights_path.empty()) {
        err = "parameter file (-W) is required";
        return 1;
    }
    const bool any_stats =
        !out.eval.stats_ram_path.empty() || !out.eval.stats_types_path.empty()
        || !out.eval.stats_benchmark_path.empty()
        || !out.eval.stats_latency_samples_path.empty()
        || !out.eval.stats_raise_samples_path.empty();
    if (any_stats && out.fib_paths.size() != 1) {
        err = "--stats-* require exactly one -F";
        return 1;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    srand(static_cast<unsigned>(time(nullptr)));

    AppArgv cfg;
    const char* perror_msg = nullptr;
    int pr = parse_argv(argc, argv, cfg, perror_msg);
    if (pr == -1) return 0;
    if (pr != 0) {
        std::cerr << "error: " << (perror_msg ? perror_msg : "arguments") << std::endl;
        print_usage(argc > 0 ? argv[0] : "main_alloc.app");
        return 1;
    }

    if (cfg.eval_mode) {
        return run_dynamic_eval(cfg.fib_paths, cfg.weights_path, cfg.eval);
    }

    std::vector<std::vector<FIBEntry>> all(cfg.fib_paths.size());
    for (size_t t = 0; t < cfg.fib_paths.size(); ++t) {
        std::cout << "FIB[" << t << "] Parsing: " << cfg.fib_paths[t] << std::endl;
        all[t] = parse_fib_file(cfg.fib_paths[t]);
        if (all[t].empty()) {
            std::cerr << "parse FIB file " << (t + 1) << " failed" << std::endl;
            return 1;
        }
    }

    if (cfg.gen_mode) return run_dynamic_gen(all, cfg.weights_path);

    return 0;
}
