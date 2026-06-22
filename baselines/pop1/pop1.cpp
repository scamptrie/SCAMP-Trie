#include "trie.h"
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>

#define EPSILON (1e-9)

using namespace std;

#define TRIE_NODE_BITS (192)

bool isGreater(double a, double b) { return (a - b) > EPSILON; }

void dump_layer_size(vector<uint32_t> stage, int layer16_count, const std::string &filename)
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

    ofs << endl;

    ofs << (1 << 16) * 16 << endl;

    ofs << layer16_count * 16 << endl;

    ofs << 0 << endl;

    ofs.close();
}

void fixed_prefixes(string input_file, string output_file)
{
    TRIE *trie = new TRIE();

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
        trie->Insert_prefix(ip, nh);
        if (nh == 255)
            nh = 1;
        else
            nh++;
    }

    vector<uint32_t> stage(ALL_STAGE, 0);
    int layer16_count = 0;
    trie->Map_stage(stage, layer16_count, *trie->auxtrie.root);

    dump_layer_size(stage, layer16_count, output_file);
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
    std::string output_dir = "output/pop1";
    std::string output_file = output_dir + "/" + dataset_name;

    system(("mkdir -p " + output_dir).c_str());

    fixed_prefixes(input_file, output_file);

    return 0;
}