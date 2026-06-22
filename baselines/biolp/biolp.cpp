#include "biolp.h"
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>

#define EPSILON (1e-9)

#define SPACE (40.6719)

#define TRIE_NODE_BITS (64)

#define IFR 120

using namespace std;
/* need change here and ALL_STAGE */
bool isGreater(double a, double b) { return (a - b) > EPSILON; }

void dump_layer_size(vector<uint32_t> stage, const size_t trie_count, const std::string &filename)
{
    std::ofstream ofs(filename);
    if (!ofs.is_open())
    {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return;
    }

    ofs << stage.size() << endl;

    for (size_t i = 0; i < stage.size(); i++)
    {
        ofs << stage[i] * TRIE_NODE_BITS << " ";
    }

    // The fixed lookup overhead of the BiOLP algorithm comes from locating the root node at the start of the lookup.
    // The array size depends on the initial step size, while the actual utilization depends on the number of trees.

    ofs << endl
        << (1 << DIRECT_LEN) * TRIE_NODE_BITS << endl
        << trie_count * TRIE_NODE_BITS << endl
        << 0 << endl;

    ofs.close();
}

double calc_utilization(vector<uint32_t> stage)
{
    uint64_t all = 0;
    uint32_t max_ = 0;
    size_t layer_cnt = ALL_STAGE;

    for (size_t i = 0; i < layer_cnt; ++i)
    {
        uint32_t cnt = stage[i];
        max_ = std::max(max_, cnt);
        all += cnt;
    }

    if (max_ == 0 || layer_cnt == 0)
        return 0.0;

    return static_cast<double>(all) /
           (static_cast<double>(max_) * layer_cnt);
}

void BiOLP_fixed_capacity(string input_file, string output_file)
{
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dis(0, 999);
    uint16_t rate = 80;
    while (true)
    {
        BiOLP biolp;

        /* Open files */
        ifstream fibfile(input_file);
        string line;
        uint8_t nh = 1;
        uint64_t prefix_count = 0;
        bool get = true;

        if (!fibfile)
            cerr << "Error opening file: " << input_file << endl;

        /* Insert prefix */
        while (getline(fibfile, line))
        {
            int randomNum = dis(gen);
            if (randomNum >= rate)
                continue;

            prefix_count++;
            ip6_t ip;
            if (sscanf(line.c_str(), "%lx.%lx/%hhd", &ip.hi, &ip.lo, &ip.length) != 3)
                cerr << "Invalid format: " << line << endl;
            biolp.Insert_prefix(ip, nh);
            if (nh == 255)
                nh = 1;
            else
                nh++;
        }

        /* Node pushing */
        biolp.Node_pushing(biolp.auxtrie.root, NULL);
        /* Select trie */
        biolp.Selecting_inverted_subtree(0, 10);

        vector<uint32_t> stage(ALL_STAGE, 0);
        if (biolp.Map_stage(stage))
        {
            for (int i = 0; i < ALL_STAGE; i++)
            {
                double temp = stage[i] * 16.0 / 1024;
                if (i != 0 && isGreater(temp, SPACE))
                {
                    printf("Space is out of range at rate %d!\n", rate);
                    get = false;
                    break;
                }
            }
            if (get)
            {
                printf("prefix count : %ld\n", prefix_count);
                break;
            }
        }
        else
        {
            printf("Map false at rate %d!\n", rate);
        }
        rate--;
    }
}

void BiOLP_fixed_prefixes(string input_file, string output_file)
{
    BiOLP biolp;

    /* Open files */
    ifstream fibfile(input_file);
    string line;
    uint8_t nh = 1;
    uint64_t prefix_count = 0;

    if (!fibfile)
        cerr << "Error opening file: " << input_file << endl;

    /* Insert prefix */
    while (getline(fibfile, line))
    {
        prefix_count++;
        ip6_t ip;
        if (sscanf(line.c_str(), "%lx.%lx/%hhd", &ip.hi, &ip.lo, &ip.length) != 3)
            cerr << "Invalid format: " << line << endl;
        biolp.Insert_prefix(ip, nh);
        if (nh == 255)
            nh = 1;
        else
            nh++;
    }

    /* Node pushing */
    biolp.Node_pushing(biolp.auxtrie.root, NULL);
    /* Create trie */
    biolp.Creating_trie(*biolp.auxtrie.root);

    biolp.trie_vector.sortTRIE_VECTOR();

    uint16_t ifr = IFR, tn = 0;
    // if select by tn(trie numbers), set ifr as a big number(such as 120)
    // if select by ifr(inversion factor rate), set tn as biolp.trie_vector.tries.size()
    vector<uint32_t> stage(ALL_STAGE, 0);
    double best_util = 0;

    // biolp.Selecting_inverted_subtree(tn, ifr);

    // biolp.Map_stage(stage);

    while (true)
    {
        /* Select trie */
        uint16_t inverted = biolp.Selecting_inverted_subtree(tn, ifr);

        /* Generate stage allocation */
        vector<uint32_t> temp_stage(ALL_STAGE, 0);

        biolp.Map_stage(temp_stage);

        /* Calculate utilization */
        double util = calc_utilization(temp_stage);

        printf("tree number : %d, util : %lf\n", tn, util);

        if (util > best_util + EPSILON)
        {
            best_util = util;
            stage = temp_stage;
        }

        if (inverted == biolp.trie_vector.tries.size())
            break;

        ++tn;
        ++ifr;
    }
    dump_layer_size(stage, biolp.trie_vector.tries.size(), output_file);
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <dataset_name>" << std::endl;
        return 1;
    }
    std::string dataset_name = argv[1];
    std::string input_file = "data/" + dataset_name + ".fib";
    std::string output_dir = "output/biolp";
    std::string output_file = output_dir + "/" + dataset_name;

    system(("mkdir -p " + output_dir).c_str());

    BiOLP_fixed_prefixes(input_file, output_file);

    return 0;
}