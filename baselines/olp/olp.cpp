#include "olp.h"
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>

#define EPSILON (1e-9)

#define SPACE (40.6719)

#define MAP_CORRECTION (false)

#define CAPACITY_EPSILON 1000

using namespace std;

#define TRIE_NODE_BITS (64)

bool isGreater(double a, double b) { return (a - b) > EPSILON; }

void dump_layer_size(vector<uint32_t> stage, const std::string &filename)
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

    // The OLP algorithm incurs no additional overhead
    // because the nodes corresponding to its initial step size are placed in the first pipeline stage.
    ofs << endl
        << 0 << endl
        << 0 << endl
        << 0 << endl;

    ofs.close();
}

vector<uint32_t> find_min_capacity(OLP *olp, bool correction, int start_cap = 20000)
{
    uint32_t low = start_cap;
    uint32_t high = start_cap;

    vector<uint32_t> stage(ALL_STAGE, 0);
    stage[0] = 1ULL << olp->direct_len;

    vector<uint32_t> temp_stage = stage;

    if (olp->Map_stage(correction, high, temp_stage))
        low = 0;
    else
    {
        low = high;
        high *= 2;
        temp_stage = stage;

        while (!olp->Map_stage(correction, high, temp_stage))
        {
            low = high;
            high *= 2;

            if (high > 1e9)
            {
                printf("Capacity too large, allocation failed\n");
                return vector<uint32_t>(0);
            }

            temp_stage = stage;
        }
    }

    while ((high - low) > CAPACITY_EPSILON)
    {
        temp_stage = stage;

        int mid = low + (high - low) / 2;
        if (olp->Map_stage(correction, mid, temp_stage))
            high = mid;
        else
            low = mid;
    }
    return temp_stage;
}

void fixed_capacity(string input_file, string output_file)
{
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dis(0, 999);
    uint16_t rate = 80;
    while (true)
    {
        OLP *olp = new OLP();

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
            olp->Insert_prefix(ip, nh);
            if (nh == 255)
                nh = 1;
            else
                nh++;
        }

        /* Node pushing */
        olp->Node_pushing(olp->auxtrie.root, NULL);
        /* Trie partiton */
        olp->Partition();
        /* Make queue */
        olp->Make_queue(*olp->auxtrie.root);

        vector<uint32_t> stage(ALL_STAGE, 0);
        stage[0] = 1ULL << olp->direct_len;

        if (olp->Map_stage(MAP_CORRECTION, 36000, stage))
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

void fixed_prefixes(string input_file, string output_file)
{
    OLP *olp = new OLP();

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
        olp->Insert_prefix(ip, nh);
        if (nh == 255)
            nh = 1;
        else
            nh++;
    }

    /* Node pushing */
    olp->Node_pushing(olp->auxtrie.root, NULL);
    /* Trie partiton */
    olp->Partition();
    /* Make queue */
    olp->Make_queue(*olp->auxtrie.root);

    vector<uint32_t> stage(ALL_STAGE, 0);
    stage[0] = 1ULL << olp->direct_len;

    if (MAP_CORRECTION)
        stage = find_min_capacity(olp, MAP_CORRECTION);
    else
        olp->Map_stage(MAP_CORRECTION, 0, stage);

    dump_layer_size(stage, output_file);
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
    std::string output_dir = "output/olp";
    if (MAP_CORRECTION)
        output_dir = "output/olp_fc";
    std::string output_file = output_dir + "/" + dataset_name;

    system(("mkdir -p " + output_dir).c_str());

    fixed_prefixes(input_file, output_file);

    return 0;
}