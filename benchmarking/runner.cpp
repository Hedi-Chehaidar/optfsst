#include <bits/stdc++.h>

#if defined(_WIN32)
  #include <windows.h>
  #define popen _popen
  #define pclose _pclose
#endif

struct Config {
    std::string name;     // configuration name that goes into the CSV
    std::string args;     // arguments passed to the binary for this config
};

static std::string csv_escape(const std::string& s) {
    bool need_quotes = false;
    for (char c : s) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') { need_quotes = true; break; }
    }
    if (!need_quotes) return s;

    std::string out = "\"";
    for (char c : s) out += (c == '"') ? "\"\"" : std::string(1, c);
    out += "\"";
    return out;
}

// Extract first numeric value from stdout (CF). If none, parse fails.
static std::pair<bool, double> parse_first_number(const std::string& s) {
    static const std::regex re(R"(([+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?))");
    std::smatch m;
    if (std::regex_search(s, m, re)) {
        try { return {true, std::stod(m.str(1))}; }
        catch (...) { return {false, 0.0}; }
    }
    return {false, 0.0};
}

// Run command, capture stdout, measure wall time ms.
static std::string run_and_capture_stdout(const std::string& cmd, int& exit_code, double& ms) {
    using clock = std::chrono::steady_clock;

    auto t0 = clock::now();
    for(int i = 0; i < 5; i++) {
        FILE* pipe = popen(cmd.c_str(), "r");
        int rc = pclose(pipe);
    }
    auto t1 = clock::now();
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        exit_code = -1;
        ms = 0.0;
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

    
    ms = std::chrono::duration<double>(t1 - t0).count() * 1000 / 5;
    return output;
}

int main() {

    // One or more binaries to benchmark (paths to executables).
    const std::vector<std::string> binaries = {
        "../build/fsst"
        // "/path/to/another_binary"
    };

    // Input files (one file per run).
    std::vector<std::string> files;
    namespace fs = std::filesystem;
    std::string path = "../data/refined";
    
    for (const auto& entry : fs::recursive_directory_iterator(path)) {
        if(entry.is_regular_file())
            files.push_back(entry.path().string());
    }
    // Configurations: name + args passed to the binary.
    const std::vector<Config> configs = {
        {"FSST", ""},
        {"+ dp-train ", "--dp-train"},
        {"+ triples ", "--dp-train --triples --triples"},
        {"+ prune ", "--dp-train --triples --prune"},
        {"FSST + dp-encode", "--dp-encode"},
        {"+ dp-train", "--dp-train --dp-encode"},
        {"+ triples", "--dp-train --triples --triples --dp-encode"},
        {"+ prune = BtrFSST", "--dp-train --triples --prune --dp-encode"},
    };

    // Output CSV path


    std::ofstream out("./csv/results.csv");
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
            double best_cf = 0; std::string best_config;

            for (const auto& cfg : configs) {
                std::ostringstream cmd;
                cmd << bin;
                if (!cfg.args.empty()) cmd << " " << cfg.args;
                cmd << " " << file;
                //output file 
                cmd << " ../build/out";
                int exit_code = 0;
                double ms = 0.0;
                std::string stdout_text = run_and_capture_stdout(cmd.str(), exit_code, ms);

                auto [ok, cf_value] = parse_first_number(stdout_text);
                v.push_back(cf_value);
                // If CF isn't numeric, store raw stdout (escaped) in CF column.
                std::string cf_field = ok ? std::to_string(cf_value)
                                          : csv_escape(stdout_text);

                
                out << csv_escape(cfg.name) << ","
                    << ms << ","
                    << cf_field << ","
                    << file << "\n";

                if(cfg.name == "FSST" || cfg.name == "+ prune = BtrFSST") {
                    out2 << csv_escape(cfg.name == "FSST" ? cfg.name : "BtrFSST") << ","
                    << ms << ","
                    << cf_field << ","
                    << file << "\n";

                    out3 << csv_escape(cfg.name == "FSST" ? cfg.name : "BtrFSST") << ","
                    << ms << ","
                    << cf_field << ","
                    << file << "\n";
                }

                if(cf_value > best_cf) {
                    best_cf = cf_value;
                    best_config = cfg.name;
                }
                

                // Progress / debugging
                /*std::cerr << "[bin=" << bin << " cfg=" << cfg.name << "] file=" << file
                          << " time=" << ms << "s exit=" << exit_code << "\n";*/
            }
            comps.push_back({v.back() / v[0], file});
            if(v.back() < v[0]) cnt_worse++;


            // check best configuration
            best[best_config]++;
            out2 << csv_escape("Best") << ","
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
            unsigned unique_nb = unique.size(), col_size = std::ceil(std::log2(unique_nb)) * total_rows / 8;
            unsigned unique_size = 0;
            for(auto& r: unique) unique_size += r.length();

            for (const auto& cfg : configs) {
                if(cfg.name != "FSST" && cfg.name != "+ prune = BtrFSST") continue;
                std::ostringstream cmd;
                cmd << "../build/fsst";
                if (!cfg.args.empty()) cmd << " " << cfg.args;
                cmd << " in.txt ../build/out";
                int exit_code = 0;
                double ms = 0.0;
                std::string stdout_text = run_and_capture_stdout(cmd.str(), exit_code, ms);
                auto [ok, cf] = parse_first_number(stdout_text);
                unsigned compressed = fs::file_size("../build/out");
                double dict_fsst_size = col_size + compressed, dict_fsst_cf = fs::file_size(file) / dict_fsst_size;
                out3 << "Dict " << csv_escape(cfg.name == "FSST" ? cfg.name : "BtrFSST")
                << ", " << ms << ", " << dict_fsst_cf << ", " << file << "\n";
            }

        }
        
    }
    std::sort(comps.begin(), comps.end());
    stat << "worse on " << cnt_worse << " files from " << files.size() << " (" << (cnt_worse * 100.0) / files.size() << "%) :\n";
    for(int i = 0; i < cnt_worse; i++) stat << comps[i].first << " " << comps[i].second << "\n";
    stat << "--------\nbetter on files:\n";
    for(int i = files.size() - 1; i >= cnt_worse; --i) stat << comps[i].first << " " << comps[i].second << "\n";

    /*std::ofstream out2("./csv/results2.csv");
    out2 << "configuration,file,CF\n";
    for(int i = 0; i < 3; i++) {
        for(auto& cfg: configs) {
            std::ostringstream cmd;
            cmd << "../build/fsst";
            if (!cfg.args.empty()) cmd << " " << cfg.args;
            cmd << " " << comps[i].second;
            cmd << " ../build/out";
            int exit_code = 0;
            double ms = 0.0;
            std::string stdout_text = run_and_capture_stdout(cmd.str(), exit_code, ms);
            auto [ok, cf_value] = parse_first_number(stdout_text);
            std::string cf_field = ok ? std::to_string(cf_value)
                                          : csv_escape(stdout_text);
            std::string file_name = "";
            for(int j = comps[i].second.length() - 5; comps[i].second[j] != '/'; j--) file_name += comps[i].second[j];
            reverse(file_name.begin(), file_name.end());
            out2 << csv_escape(cfg.name) << ","
                    << file_name << ","
                    << cf_field << "\n"; 
        }
    }*/

    for(auto& c : best) std::cout << c.first << ": " << c.second << "\n";
    
    std::cout << "runner finished" << std::endl;
    return 0;
}
