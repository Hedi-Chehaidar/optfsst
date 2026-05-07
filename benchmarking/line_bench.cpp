#include <bits/stdc++.h>
#include "../fsst.h"
using namespace std;

struct Config {
    string name;     // configuration name that goes into the CSV
    unsigned opt;     // options of the configuration
};

/// The decode
fsst_decoder_t decoder;
/// The compressed data
vector<unsigned char> compressedData;
/// The offsets
vector<unsigned> offsets;
/// Options of fsst
fsst_options_t opt;

uint64_t compressCorpus(const vector<string>& data, double& compressionTime, bool verbose) {
    compressedData.clear();
    offsets.clear();

    vector<unsigned long> rowLens, compressedRowLens;
    vector<const unsigned char*> rowPtrs;
    vector<unsigned char*> compressedRowPtrs;
    rowLens.reserve(data.size());
    compressedRowLens.resize(data.size());
    rowPtrs.reserve(data.size());
    compressedRowPtrs.resize(data.size() + 1);
    unsigned totalLen = 0;
    for (auto& d : data) {
        totalLen += d.size();
        rowLens.push_back(d.size());
        rowPtrs.push_back(reinterpret_cast<unsigned char*>(const_cast<char*>(d.data())));
    }

    auto startTime = chrono::steady_clock::now();
    auto encoder = Optfsst_create(data.size(), rowLens.data(), rowPtrs.data(), false, &opt);
    auto createTime = chrono::steady_clock::now();
    vector<unsigned char> compressionBuffer;
    compressionBuffer.resize(16 + 2 * totalLen);
    auto compressTime = chrono::steady_clock::now();
    Optfsst_compress(encoder, data.size(), rowLens.data(), rowPtrs.data(), compressionBuffer.size(), compressionBuffer.data(), compressedRowLens.data(), compressedRowPtrs.data(), &opt);
    auto stopTime = chrono::steady_clock::now();
    unsigned long compressedLen = data.empty() ? 0 : (compressedRowPtrs[data.size() - 1] + compressedRowLens[data.size() - 1] - compressionBuffer.data());

    compressedData.resize(compressedLen + 8192);
    memcpy(compressedData.data(), compressionBuffer.data(), compressedLen);
    offsets.reserve(data.size());
    compressedRowPtrs[data.size()] = compressionBuffer.data() + compressedLen;
    for (unsigned index = 0, limit = data.size(); index != limit; ++index)
        offsets.push_back(compressedRowPtrs[index + 1] - compressionBuffer.data());
    uint64_t result = compressedData.size() /*+ (offsets.size() * sizeof(unsigned))*/;
    {
        unsigned char buffer[sizeof(fsst_decoder_t)];
        unsigned dictLen = fsst_export(encoder, buffer);
        fsst_destroy(encoder);
        result += dictLen;

        fsst_import(&decoder, buffer);
    }
    if (verbose) {
        cout << "# symbol table construction time: " << chrono::duration<double>(createTime - startTime).count() << endl;
        cout << "# compress time: " << chrono::duration<double>(stopTime - compressTime).count() << endl;
    }
    compressionTime = chrono::duration<double>(createTime - startTime).count() + chrono::duration<double>(stopTime - compressTime).count();

    return result;
}
/// Decompress a single row. The target buffer is guaranteed to be large enough
uint64_t decompressRow(vector<char>& target, unsigned line) {
    char* writer = target.data();
    auto limit = writer + target.size();

    auto data = compressedData.data();
    auto start = line ? offsets[line - 1] : 0, end = offsets[line];
    unsigned len = fsst_decompress(&decoder, end - start, data + start, limit - writer, reinterpret_cast<unsigned char*>(writer));
    return len;
}

int main() {


    // Input files (one file per run).
    vector<string> files;
    namespace fs = filesystem;
    string path = "../data/refined";
    
    for (const auto& entry : fs::recursive_directory_iterator(path)) {
        if(entry.is_regular_file())
            files.push_back(entry.path().string());
    }
    // Configurations: name + args passed to the binary.
    const vector<Config> configs = {
        {"FSST", 0},
        {"+ dp-train ", FSST_OPT_DP_TRAIN},
        {"+ triples ", FSST_OPT_TRIPLES | FSST_OPT_DP_TRAIN},
        {"+ prune ", FSST_OPT_TRIPLES | FSST_OPT_DP_TRAIN | FSST_OPT_PRUNE},
        {"FSST + dp-encode", FSST_OPT_DP_ENCODE},
        {"+ dp-train", FSST_OPT_DP_TRAIN | FSST_OPT_DP_ENCODE},
        {"+ triples", FSST_OPT_TRIPLES | FSST_OPT_DP_TRAIN | FSST_OPT_DP_ENCODE},
        {"+ prune = OptFSST", FSST_OPT_TRIPLES | FSST_OPT_DP_TRAIN | FSST_OPT_PRUNE | FSST_OPT_DP_ENCODE},
    };

    // Output CSV path


    ofstream out("./csv/decomp_time.csv");
    out << "configuration,Time,file\n";
    ofstream out2("./csv/decomp_speed.csv");
    out2 << "configuration,Time,file\n";
    ofstream out3("./csv/decomp_fsst.csv");
    out3 << "configuration,Time,file\n";
    ofstream out4("./csv/comp_fsst.csv");
    out4 << "configuration,CF,Time,file\n";
    using clock = chrono::steady_clock;

    for (const auto& file : files) {
        vector<double> v;
        vector<double> cf, ct;
        for (const auto& cfg : configs) {
            
            // compress file
            opt.flags = cfg.opt;
            size_t maxLineLen = 0;

            ifstream fin(file);
            string line;
            vector<string> rows;
            while(getline(fin, line)) {
                line += '\n';
                maxLineLen = max(maxLineLen, (size_t)line.length());
                rows.push_back(line);
            }

            double compression_time;

            uint64_t compressed_size = compressCorpus(rows, compression_time, false);

            compression_time = fs::file_size(file) / compression_time / (1 << 20); // speed

            if(cf.size()) {
                out4 << cfg.name << ","
                    << cf[0] / compressed_size << ","
                    << ct[0] / compression_time << ","
                    << file << "\n";
            }

            cf.push_back(compressed_size);
            ct.push_back(compression_time);

            vector<char> targetBuffer;
            targetBuffer.resize(maxLineLen + 128);

            auto t0 = clock::now();
            for(int rep = 0; rep < 100; rep++) {
                for(int i = 0; i < rows.size(); i++) {
                    decompressRow(targetBuffer, i);
                }
            }
            auto t1 = clock::now();

            double t = chrono::duration<double>(t1 - t0).count() * 10; // ms
            
            out << cfg.name << ","
            << t << ","
            << file << "\n";
            
            if(v.size()) {
                out3 << cfg.name << ","
                <<  v[0] / t << ","
                << file << "\n";
            }

            v.push_back(t);

            if(t) {
                t = 1000.0 * compressed_size / (1 << 20) / t ;
            }

            out2 << cfg.name << ","
                << t << ","
                << file << "\n";


            
        }


    }
        
    
    cout << "decomp finished" << endl;
    return 0;
}
