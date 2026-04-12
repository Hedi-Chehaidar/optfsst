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
static std::string run_and_capture_stdout(const std::string& cmd, int& exit_code) {
    using clock = std::chrono::steady_clock;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        exit_code = -1;
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
    return output;
}

int main() {

    // One or more binaries to benchmark (paths to executables).
    const std::vector<std::string> binaries = {
        "../build12/fsst12"
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


    std::ofstream out("./csv/improvement12.csv");
    out << "configuration,CF,file\n";

    std::ofstream stat("./prv/stats12.txt");
    std::vector<std::pair<double, std::string>> comps;
    int cnt_worse = 0;
    std::map<std::string, int> best;

    for (const auto& bin : binaries) { // only one binary for now
        for (const auto& file : files) {
            std::vector<double> v;
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
                cmd << " ../build12/out";
                int exit_code = 0;
                std::string stdout_text = run_and_capture_stdout(cmd.str(), exit_code);
                
                bool ok = true;
                double cf_value = 1.0 * fs::file_size(file) / fs::file_size("../build12/out");

                v.push_back(cf_value);
                if(cfg.name != "FSST") {
                    out << cfg.name << ","
                        << cf_value / v[0] << ","
                        << file << "\n";
                }

                if(cf_value >= best_cf) {
                    best_cf = cf_value;
                    best_config = cfg.name;
                }
                
            }
            comps.push_back({v.back() / v[0], file});
            if(v.back() < v[0]) cnt_worse++;


            // check best configuration
            best[best_config]++;




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
    
    std::cout << "cf_bench12 finished" << std::endl;
    return 0;
}