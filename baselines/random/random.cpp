#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <dataset_name>" << std::endl;
        return 1;
    }

    std::string dataset_name = argv[1];
    std::string input_file = "data/" + dataset_name + ".fib";
    std::string output_dir = "output/random";
    std::string output_file = output_dir + "/" + dataset_name;

    // 读取输入文件并统计行数
    std::ifstream in_file(input_file);
    if (!in_file.is_open())
    {
        std::cerr << "Error: Cannot open input file " << input_file << std::endl;
        return 1;
    }

    int line_count = 0;
    std::string line;
    while (std::getline(in_file, line))
    {
        line_count++;
    }
    in_file.close();

    // 创建输出目录（如果不存在）
    system(("mkdir -p " + output_dir).c_str());

    // 写入输出文件
    std::ofstream out_file(output_file);
    if (!out_file.is_open())
    {
        std::cerr << "Error: Cannot create output file " << output_file << std::endl;
        return 1;
    }

    // 第 1 行：流水级数
    out_file << "11" << std::endl;

    // 第 2 行：每级流水的实际使用空间（位 bits）
    out_file << line_count;
    for (int i = 0; i < 10; i++)
    {
        out_file << " 1024";
    }
    out_file << std::endl;

    // 第 3 行：其他固定存储的存储器大小（位 bits）
    // 假设使用固定大小的数组（与数据集规模无关）
    int fixed_storage_size = 10000;
    out_file << fixed_storage_size << std::endl;

    // 第 4 行：其他固定存储中实际使用空间（位 bits）
    int fixed_storage_used = 8800;
    out_file << fixed_storage_used << std::endl;

    // 第 5 行：其他弹性存储的实际使用空间（位 bits）
    // 与数据集规模强相关的存储
    int elastic_storage_used = line_count * 10;
    out_file << elastic_storage_used << std::endl;

    out_file.close();

    return 0;
}
