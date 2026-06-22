#ifndef __DYN_ANNEALING_H__
#define __DYN_ANNEALING_H__

#include "moo_weights.h"
#include "../scamptrie_dynamic.h"
#include <vector>
#include <string>

class DynAnnealing {
public:
    void init(std::vector<std::vector<FIBEntry>> &fib_entries, bool enable_hash, bool enable_free_child);
    void iteration();
    MOOWeights get_final_weights();
    void output_csv(const std::string& filename);

private:
    double func_value(const MOOWeights& weights, const std::vector<double> &usages, const std::vector<uint32_t> &tcam) const;

    bool annealing_temperature_check(double delta_f);
    void get_step_range(double &min_factor, double &max_factor);

private:
#ifndef DYN_ANNEAL_MAX_ROUND
#define DYN_ANNEAL_MAX_ROUND 300
#endif
    static constexpr int MAX_ROUND = DYN_ANNEAL_MAX_ROUND;
    static constexpr double T_INIT = 0.20;
    static constexpr double DELTA = 0.99;
    static constexpr double MIN_RATIO = 0.02;
    
    double annealing_T;
    bool enable_hash;
    bool enable_free_child;

    std::vector<std::vector<FIBEntry>> fib_entries;

    MOOWeights weights, weights_last;
    double f_last;

    MOOWeights final_weights;
    double final_usage;
    double final_f;

    int round_num;
    int fib_num;

    // 迭代记录数据结构
    struct IterationRecord {
        MOOWeights weights;
        std::vector<double> usage;
        double final_usage;
    };
    std::vector<IterationRecord> iteration_records;
};

#endif // __DYN_ANNEALING_H__