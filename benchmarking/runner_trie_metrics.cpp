#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../libfsst.hpp"

namespace fs = std::filesystem;

static std::vector<unsigned char> read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open input file: " + path.string());
    }

    in.seekg(0, std::ios::end);
    std::streamoff size = in.tellg();
    if (size < 0) {
        throw std::runtime_error("cannot determine input file size: " + path.string());
    }
    in.seekg(0, std::ios::beg);

    std::vector<unsigned char> data(static_cast<size_t>(size));
    if (size > 0) {
        in.read(reinterpret_cast<char*>(data.data()), size);
        if (!in) {
            throw std::runtime_error("failed to read input file: " + path.string());
        }
    }
    return data;
}

int main() {
    const fs::path corpus_root = "../fsst-paper/dbtext";
    std::vector<fs::path> files;

    for (const auto& entry : fs::recursive_directory_iterator(corpus_root)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());

    fs::create_directories("./csv");
    std::ofstream out("./csv/trie_training_metrics_dbtext.csv");
    out << "file,total_nodes,level,node_count,internal_node_count,child_edge_count,"
           "avg_children_per_node,avg_children_per_internal_node,min_children,max_children,"
           "child_distance_samples,avg_child_distance,min_child_distance,max_child_distance\n";

    fsst_options_t opt = {0};
    opt.flags = FSST_OPT_DP_TRAIN | FSST_OPT_DP_ENCODE | FSST_OPT_TRIPLES | FSST_OPT_PRUNE;

    for (const auto& file : files) {
        std::vector<unsigned char> data = read_file(file);
        const unsigned char* input = data.empty() ? reinterpret_cast<const unsigned char*>("") : data.data();
        const size_t len = data.size();

        libfsst::TrieMetrics metrics = libfsst::measureLastTrainingTrie(1, &len, &input, false, opt);

        std::cout << "processed " << file.string()
                  << " (" << metrics.nodeCount << " trie nodes, "
                  << metrics.levels.size() << " levels)\n";

        for (const auto& level : metrics.levels) {
            const double avg_children_per_node =
                level.nodes ? static_cast<double>(level.childEdges) / static_cast<double>(level.nodes) : 0.0;
            const double avg_children_per_internal_node =
                level.internalNodes ? static_cast<double>(level.childEdges) / static_cast<double>(level.internalNodes) : 0.0;
            const double avg_child_distance =
                level.childDistanceSamples ? static_cast<double>(level.childDistanceSum) / static_cast<double>(level.childDistanceSamples) : 0.0;

            out << std::quoted(file.string()) << ","
                << metrics.nodeCount << ","
                << level.level << ","
                << level.nodes << ","
                << level.internalNodes << ","
                << level.childEdges << ","
                << avg_children_per_node << ","
                << avg_children_per_internal_node << ","
                << level.minChildren << ","
                << level.maxChildren << ","
                << level.childDistanceSamples << ","
                << avg_child_distance << ","
                << level.minChildDistance << ","
                << level.maxChildDistance << "\n";
        }
    }

    std::cout << "wrote ./csv/trie_training_metrics_dbtext.csv\n";
    return 0;
}
