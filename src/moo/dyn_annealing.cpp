#include "dyn_annealing.h"
#include "rand_gen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <omp.h>
#include <fstream>
#include <iomanip>

void DynAnnealing::init(std::vector<std::vector<FIBEntry>> &fib_entries, bool enable_hash, bool enable_free_child) {
    this->enable_hash = enable_hash;
    this->enable_free_child = enable_free_child;
    this->fib_entries = fib_entries;
    fib_num = fib_entries.size();
    round_num = 0;
    f_last = 0;

    for (int i = 0; i < RAM_NUM; ++i)
        weights[i] = 100;

    weights.normalize();
    final_f = -100;
    final_usage = 0;
    iteration_records.clear();
}

double DynAnnealing::func_value(const MOOWeights& weights, const std::vector<double> &usages, const std::vector<uint32_t> &tcam) const {
    double min_usage = 1.0;
    uint32_t max_tcam = 0;
    for (int i = 0; i < fib_num; ++i)
        min_usage = std::min(min_usage, usages[i]);
    double min_weight = 1e9, sum_weights = 0;
    for (int i = 0; i < RAM_NUM - 1; ++i) {
        min_weight = std::min(min_weight, weights[i]);
        sum_weights += weights[i];
    }
    for (int i = 0; i < fib_num; ++i)
        max_tcam = std::max(max_tcam, tcam[i]);
    double min_ratio = min_weight / sum_weights;

    // double target = min_usage + 2 * min_ratio;
    double penalty = 0, beta = 0.0;
    for (int i = 0; i < RAM_NUM - 1; ++ i)
        if (weights[i] < weights[i + 1])
            penalty += weights[i + 1] - weights[i];
    penalty /= sum_weights;

    double target = min_usage - beta * penalty;
    printf("  min_usage: %f%%, max_tcam: %d, penalty: %f, target = %f\n", min_usage * 100, (int)max_tcam, penalty, target);
    return target;
}

bool DynAnnealing::annealing_temperature_check(double delta_f) {
    static constexpr uint64_t rand_max = 1e9 + 7;
    if (delta_f > 0) return true;
    double p = exp(delta_f / annealing_T);
    return rand_check_p(p);
}

void DynAnnealing::get_step_range(double &min_factor, double &max_factor) {
    // Range: [1/a, 1*a], a->1.0 as rounds increase
    static constexpr double INIT_A = 2.0;
    static constexpr int ADJUST_INTERVAL = 20;
    static constexpr double A_REDUCTION_FACTOR = 0.9;
    
    int adjust_stage = round_num / ADJUST_INTERVAL;
    double current_a = 1.0 + (INIT_A - 1.0) * std::pow(A_REDUCTION_FACTOR, adjust_stage);
    
    min_factor = 1.0 / current_a;
    max_factor = 1.0 * current_a;
}

void DynAnnealing::iteration() {
    printf("DynAnnealing::iteration(): MAX_ROUND = %d\n", MAX_ROUND);
    /* 验证阶段暂时强制单线程：保证整段退火在代码层面不会因 parallel for 并行建树而爆内存；
     * 验证结束后可删除下面两行，改由 OMP_NUM_THREADS 与环境决定并行度。 */
    omp_set_dynamic(0);
    omp_set_num_threads(1);

    while (true) {
        round_num++;
        if (round_num > MAX_ROUND) {
            printf("DynAnnealing: finished all %d rounds (MAX_ROUND=%d).\n", MAX_ROUND, MAX_ROUND);
            fflush(stdout);
            return;
        }

        std::vector<double> usage(fib_num);
        std::vector<uint32_t> tcam(fib_num);
        double min_usage = 1.0;
        /* 外层 iteration() 已 omp_set_num_threads(1)，此处等价于串行循环；保留 parallel for 便于恢复多线程后同一套 reduction 逻辑。 */
        #pragma omp parallel for schedule(static) reduction(min:min_usage)
        for (int t = 0; t < fib_num; ++t) {
            ScampTrieDynamic *scamptrie_dynamic = new ScampTrieDynamic();
            scamptrie_dynamic->set_ram_allocation(10*KILO_BYTES, weights);
            bool runerror = false;
            for (auto &fib_entry : fib_entries[t]) {
                if (scamptrie_dynamic->insert_prefix(fib_entry.prefix, fib_entry.prefix_len, fib_entry.nexthop) != SCAMPTRIE_OK) {
                    runerror = true;
                    break;
                }
            }
            if (runerror) {
                usage[t] = 0;
                min_usage = std::min(min_usage, 0.0);
                tcam[t] = 1001;
            } else {
                uint64_t ram_used, ram_allocated;
                uint32_t tcam_entries;
                scamptrie_dynamic->get_ram_info(ram_used, ram_allocated, tcam_entries);
                usage[t] = 1.0 * ram_used / ram_allocated;
                tcam[t] = tcam_entries;
                min_usage = std::min(min_usage, usage[t]);
            }
            delete scamptrie_dynamic;
        }

        double min_factor, max_factor;
        get_step_range(min_factor, max_factor);

        printf("Round %d (rand_range: %f-%fx): ", round_num, min_factor, max_factor);
        printf("  Usage: ");
        for (int t = 0; t < fib_num; ++t) printf("(%5.2lf%%,%5d)%c", usage[t] * 100, tcam[t], t < fib_num - 1 ? ' ' : '\n');
        printf("  Weights: ");
        for (int i = 0; i < RAM_NUM; ++i) printf("%8.3f%c", weights[i], i < RAM_NUM - 1 ? ' ' : '\n');

        double new_f = func_value(weights, usage, tcam);
        if (new_f > final_f) {
            final_weights = weights;
            final_f = new_f;
            final_usage = min_usage;
        }

        // 在判定是否接受新解前记录当前迭代信息（此时 final_usage 已更新）
        IterationRecord record;
        for (int i = 0; i < RAM_NUM; ++i)
            record.weights[i] = weights[i];
        record.usage = usage;
        record.final_usage = final_usage;
        iteration_records.push_back(record);

        double delta_f = new_f - f_last;
        printf("  delta_f: %f, f_new: %f, f_last: %f, final_f: %f ----- ", delta_f, new_f, f_last, final_f);
        if (annealing_temperature_check(delta_f)) {
            printf("Accept\n");
            for (int i = 0; i < RAM_NUM; ++i)
                weights_last[i] = weights[i];
            f_last = new_f;
        } else {
            printf("Reject\n");
            for (int i = 0; i < RAM_NUM; ++i)
                weights[i] = weights_last[i];
        }

        for (int i = 0; i < RAM_NUM; ++i) {
            if (rand_check_p(0.4)) {
                double range = max_factor - min_factor;
                double rand_val = (double)(rand() % RAND_MAX) / RAND_MAX; // 0.0 ~ 1.0
                weights[i] = weights[i] * (min_factor + rand_val * range);
            }
            if (weights[i] < 1) weights[i] = 1;
        }
        // 保证每位带权占比 >= MIN_RATIO：不足的抬到 MIN_RATIO，从最高权处扣减以保持总权重不变，循环直到全部 >= MIN_RATIO
        while (1) {
            double total = 0.0;
            for (int i = 0; i < RAM_NUM; ++i) total += weights[i];
            double threshold = MIN_RATIO * total;
            double add_total = 0.0;
            for (int i = 0; i < RAM_NUM; ++i) {
                if (weights[i] < threshold) {
                    add_total += threshold - weights[i];
                    weights[i] = threshold;
                }
            }
            if (add_total <= 1e-12) break;
            double to_sub = add_total;
            while (to_sub > 1e-12) {
                int j_max = -1;
                for (int j = 0; j < RAM_NUM; ++j) {
                    if (weights[j] > threshold && (j_max < 0 || weights[j] > weights[j_max]))
                        j_max = j;
                }
                if (j_max < 0) break;
                double can_sub = weights[j_max] - threshold;
                double sub = std::min(to_sub, can_sub);
                weights[j_max] -= sub;
                to_sub -= sub;
            }
        }
        weights.normalize();
        if (rand_check_p(0.2))
            annealing_T = annealing_T * DELTA;

        fflush(stdout); /* 每轮刷出日志，便于长耗时时用 tail -f 观察是否持续推进 */
    }
}

MOOWeights DynAnnealing::get_final_weights() {
    printf("\n=============== Final Results ===============\n");
    printf("Final Min Usage: %8.2lf%%\n", final_usage * 100);
    for (int t = 0; t < fib_num; ++t) {
        ScampTrieDynamic *scamptrie_dynamic = new ScampTrieDynamic();
        scamptrie_dynamic->set_ram_allocation(10*KILO_BYTES, final_weights);
        for (auto &fib_entry : fib_entries[t]) {
            scamptrie_dynamic->insert_prefix(fib_entry.prefix, fib_entry.prefix_len, fib_entry.nexthop);
        }
        uint64_t ram_used, ram_allocated;
        uint32_t tcam_entries;
        scamptrie_dynamic->get_ram_info(ram_used, ram_allocated, tcam_entries);
        printf("FIB %d Usage: %8.2lf%%, TCAM: %6d\n", t, 1.0 * ram_used / ram_allocated * 100, tcam_entries);
        delete scamptrie_dynamic;
    }

    double sum_weights = 0;
    for (int i = 0; i < RAM_NUM; ++i) sum_weights += final_weights[i];
    printf("Stage Weights:\n");
    for (int i = 0; i < RAM_NUM; ++i) {
        printf("  S%02d:%8.3f (%5.2lf%%)\n", i, final_weights[i], 100.0 * final_weights[i] / sum_weights);
    }
    printf("\n");
    
    // 输出 CSV 文件
    output_csv("./results/summary/dyn_annealing_iterations.csv");
    
    return final_weights;
}

void DynAnnealing::output_csv(const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        printf("Error: Cannot open file %s for writing\n", filename.c_str());
        return;
    }
    
    // 设置精度
    file << std::fixed << std::setprecision(6);
    
    // 输出每一轮迭代的数据
    for (const auto& record : iteration_records) {
        // 输出 weights (B列)
        for (int i = 0; i < RAM_NUM; ++i) {
            file << record.weights[i];
            if (i < RAM_NUM - 1) file << ",";
        }
        
        // 空列分隔
        file << ",";
        
        // 输出 usage (C列)
        for (int t = 0; t < fib_num; ++t) {
            file << record.usage[t];
            if (t < fib_num - 1) file << ",";
        }
        
        // 空列分隔
        file << ",";
        
        // 输出 final_usage (1列)
        file << record.final_usage;
        
        file << "\n";
    }
    
    file.close();
    printf("Iteration records saved to %s\n", filename.c_str());
}