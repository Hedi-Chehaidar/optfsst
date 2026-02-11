#include <bits/stdc++.h>

#if defined(_WIN32)
  #include <windows.h>
  #define popen _popen
  #define pclose _pclose
#endif

namespace fs = std::filesystem;

struct Config {
    std::string name;     // configuration name that goes into the CSV
    std::string args;     // arguments passed to the binary for this config
};


// Run command, capture stdout, measure wall time ms.
static std::string run_and_capture_stdout(const std::string& cmd, int& exit_code, double& s) {
    using clock = std::chrono::steady_clock;
    double d_time = 0;
    for(int i = 0; i < 5; i++) {
        auto t0 = clock::now();
        int rc = std::system(cmd.c_str());
        auto t1 = clock::now();
        d_time += std::chrono::duration<double>(t1 - t0).count() * 1000;
        fs::remove("../build/out");
    }
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        exit_code = -1;
        s = 0.0;
        return "";
    }
    std::string output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) output += buffer;
    int rc = pclose(pipe);
    #if defined(_WIN32)
    exit_code = rc;
    #else
    if (WIFEXITED(rc)) exit_code = WEXITSTATUS(rc);
    else exit_code = rc;
    #endif
    assert(rc == 0);

    
    s = d_time / 5;
    return output;
}

int main() {

    // One or more binaries to benchmark (paths to executables).
    const std::vector<std::string> binaries = {
        "../build/fsst"
        //""
    };

    // Input files (one file per run).
    std::vector<std::string> files;
    
    std::string path = "../data/refined";
    
    for (const auto& entry : fs::recursive_directory_iterator(path)) {
        if(entry.is_regular_file())
            files.push_back(entry.path().string());
    }
    // Configurations: name + args passed to the binary.
    const std::vector<Config> configs = {
        {"FSST", ""},
        {"FSST + dp-train ", "--dp-train"},
        {"+ triples ", "--dp-train --triples"},
        {"+ prune ", "--dp-train --triples --prune"},
        {"FSST + dp-encode", "--dp-encode"},
        {"+ dp-train", "--dp-train --dp-encode"},
        {"+ triples", "--dp-train --triples --dp-encode"},
        //{"LZ4", "../build/lz4"},
        //{"ZSTD", "../build/zstd"},
        {"+ prune = BtrFSST", "--dp-train --triples --prune --dp-encode"},
    };

    // Output CSV path


    std::ofstream out("./csv/improvement.csv");
    std::ofstream out2("./csv/best_config.csv");
    std::ofstream out3("./csv/dict.csv");
    out << "configuration,Time,CF,file\n";
    out2 << "configuration,Time,CF,file\n";
    out3 << "configuration,Time,CF,file\n";

    std::ofstream stat("stats.txt");
    std::vector<std::pair<double, std::string>> comps;
    int cnt_worse = 0;
    std::map<std::string, int> best;

    for (const auto& bin : binaries) { // only one binary for now
        for (const auto& file : files) {
            std::vector<double> v;
            std::vector<double> times;
            double best_cf = 0; std::string best_config;

            for (const auto& cfg : configs) {
                std::ostringstream cmd;
                cmd << bin;
                if (!cfg.args.empty()){
                    if(bin != "") cmd << " ";
                    cmd << cfg.args;
                }
                cmd << " " << file;
                //output file 
                if(cfg.name == "ZSTD") cmd << " -o";
                cmd << " ../build/out";
                int exit_code = 0;
                double s = 0.0;
                std::string stdout_text = run_and_capture_stdout(cmd.str(), exit_code, s);
                
                bool ok = true;
                double cf_value = 1.0 * fs::file_size(file) / fs::file_size("../build/out");
                s /= 1000;
                s = 1.0 * fs::file_size(file) / (1 << 20) / s;
                fs::remove("../build/out");

                v.push_back(cf_value);
                times.push_back(s);
                if(cfg.name != "FSST") {
                    out << cfg.name << ","
                        << times[0] / s << ","
                        << cf_value / v[0] << ","
                        << file << "\n";
                }

                if(cfg.name == "FSST" || cfg.name == "+ prune = BtrFSST") {
                    out2 << (cfg.name == "FSST" ? cfg.name : "BtrFSST") << ","
                    << s << ","
                    << cf_value << ","
                    << file << "\n";

                    out3 << (cfg.name == "FSST" ? cfg.name : "BtrFSST") << ","
                    << s << ","
                    << cf_value << ","
                    << file << "\n";
                }

                if(cf_value > best_cf) {
                    best_cf = cf_value;
                    best_config = cfg.name;
                }
                
            }
            comps.push_back({v.back() / v[0], file});
            if(v.back() < v[0]) cnt_worse++;


            // check best configuration
            best[best_config]++;
            out2 << "Best" << ","
                << "0" << ","
                << best_cf << ","
                << file << "\n";



            //--------- Dict FSST ----------

            std::unordered_set<std::string> unique;
            std::ofstream fout("in.txt");
            std::ifstream fin(file);
            std::string row;
            unsigned total_rows = 0;
            while(std::getline(fin, row)) {
                total_rows++;
                row += '\n';
                if(!unique.count(row)) {
                    unique.insert(row);
                    fout << row;
                }
            }
            fout.flush();
            unsigned unique_nb = unique.size();
            double col_size = std::ceil(std::log2(unique_nb)) * total_rows / 8.0;
            unsigned unique_size = 0;
            for(auto& r: unique) unique_size += r.length();

            for (const auto& cfg : configs) {
                std::ostringstream cmd;
                cmd << "../build/fsst";
                if (!cfg.args.empty()) cmd << " " << cfg.args;
                cmd << " in.txt ../build/out";
                int exit_code = 0;
                double ms = 0.0;
                std::string stdout_text = run_and_capture_stdout(cmd.str(), exit_code, ms);
                unsigned compressed = fs::file_size("../build/out");
                double dict_fsst_size = col_size + compressed, dict_fsst_cf = fs::file_size(file) / dict_fsst_size;
                out << "DICT " << (cfg.name == "FSST" ? cfg.name : "BtrFSST")
                << ", " << ms << ", " << dict_fsst_cf << ", " << file << "\n";
            }

        }
        
    }

    // print stats
    std::sort(comps.begin(), comps.end());
    stat << "worse on " << cnt_worse << " files from " << files.size() << " (" << (cnt_worse * 100.0) / files.size() << "%) :\n";
    for(int i = 0; i < cnt_worse; i++) stat << comps[i].first << " " << comps[i].second << "\n";
    stat << "--------\nbetter on files:\n";
    for(int i = files.size() - 1; i >= cnt_worse; --i) stat << comps[i].first << " " << comps[i].second << "\n";

    // print for each configuration, on how many files it was best 
    for(auto& c : best) std::cout << c.first << ": " << c.second << "\n";
    
    std::cout << "runner finished" << std::endl;
    return 0;
}
