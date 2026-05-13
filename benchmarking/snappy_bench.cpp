#include <cstddef>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <snappy.h>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: snappy_bench <input> <output>\n";
        return 1;
    }

    std::ifstream in(argv[1], std::ios::binary | std::ios::ate);
    if (!in) {
        std::cerr << "cannot open input: " << argv[1] << "\n";
        return 1;
    }
    const auto size = static_cast<std::size_t>(in.tellg());
    in.seekg(0, std::ios::beg);

    std::vector<char> input(size);
    if (size > 0) {
        in.read(input.data(), static_cast<std::streamsize>(size));
        if (!in) {
            std::cerr << "failed to read input: " << argv[1] << "\n";
            return 1;
        }
    }

    std::string compressed;
    snappy::Compress(input.data(), input.size(), &compressed);

    std::ofstream out(argv[2], std::ios::binary);
    if (!out) {
        std::cerr << "cannot open output: " << argv[2] << "\n";
        return 1;
    }
    out.write(compressed.data(), static_cast<std::streamsize>(compressed.size()));
    return out ? 0 : 1;
}
