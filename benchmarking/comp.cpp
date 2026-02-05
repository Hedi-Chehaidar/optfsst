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


    std::ofstream out("./csv/comp.csv");
    out << "configuration,Time,file\n";


    for (const auto& bin : binaries) {
        for (const auto& file : files) {

            for (const auto& cfg : configs) {
                std::ostringstream cmd;
                cmd << bin;
                if (!cfg.args.empty()) cmd << " " << cfg.args;
                cmd << " " << file;
                //output file 
                cmd << " ../build/out";
                
                // compress with configuration
                int rc = 0;//std::system(cmd.str().c_str());

                // decompress and benchmark runtime
                double t = 0;
                for(int i = 0; i < 5; i++) {
                    FILE* pipe = popen(cmd.str().c_str(), "r");
                    std::string output;
                    char buffer[4096];
                    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) output += buffer;
                    rc = pclose(pipe);
                    auto [ok, d_time] = parse_first_number(output);
                    if(!ok) {
                        std::cout << output << std::endl;
                        return -1;
                    }
                    t += d_time;
                }
                
                
                double ms = t / 5;

                out << csv_escape(cfg.name) << ","
                    << ms << ","
                    << file << "\n";

                
            }


        }
        
    }
    
    std::cout << "comp finished" << std::endl;
    return 0;
}
