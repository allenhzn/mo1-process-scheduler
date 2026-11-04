/*
â€œinitializeâ€ â€“ initialize the processor configuration of the application. This
must be called before any other command could be recognized, aside from â€œexitâ€.
â€œexitâ€ â€“ terminates the console. â€œscreenâ€ â€“ see additional details.
â€œscheduler-startâ€ (formerly scheduler-test) â€“ continuously generates a batch of
dummy processes for the CPU scheduler. Each process is accessible via the
â€œscreenâ€ command. â€œscheduler-stopâ€ â€“ stops generating dummy processes.
â€œreport-utilâ€ â€“ for generating CPU utilization report. See additional details.
*/

#include "core.h"
#include "factory.h"
#include "instructions.h"
#include "process.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iomanip>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>
#include <fstream>

struct Config {
    int num_cpu;                      // 1 - 128
    std::string scheduler;            // fcfs or rr
    std::uint64_t quantum_cycles;     // 1 - 2^32
    std::uint64_t batch_process_freq; // 1 - 2^32
    std::uint64_t min_ins;            // 1 - 2^32
    std::uint64_t max_ins;            // 1 - 2^32
    std::uint64_t delays_per_exec;    // 0 - 2^32
    bool strict_quantum = false;      
    // ok added this because of the quantum behavior with round robin and the instruction delay
    // strict means round robin can preempt during instruction delay, false means it only preempts after 
    // actually executing an instruction 
};

namespace {

    constexpr std::uint64_t MAX_U32_PLUS = 4294967296ULL; // 2^32

    // digits only so that -1 doesnt wrap. overflow saturates to UINT64_MAX
    bool parseUInt(const std::string &text, std::uint64_t &out) {
        if (text.empty())
            return false;
        for (char ch : text)
            if (!std::isdigit(static_cast<unsigned char>(ch)))
                return false;
        try {
            out = std::stoull(text);
        } catch (const std::out_of_range &) {
            out = UINT64_MAX;
        }
        return true;
    }

    std::uint64_t clampWarn(const std::string &key, std::uint64_t value,
                            std::uint64_t lo, std::uint64_t hi) {
        std::uint64_t clamped = std::min(std::max(value, lo), hi);
        if (clamped != value)
            std::cout << "Warning: " << key << " out of range [" << lo << ", "
                      << hi << "], using " << clamped << "\n";
        return clamped;
    }

}

// reads config.txt, ONLY lines in the form of "<key> <value>". returns nullopt if config is unusable
std::optional<Config> loadConfig(const std::string &path) {
    std::ifstream file(path);
    if (!file) {
        std::cout << "Could not open " << path << "\n";
        return std::nullopt;
    }

    Config cfg{};
    std::unordered_map<std::string, std::string> raw;
    const std::vector<std::string> keys = {
        "num-cpu",   "scheduler", "quantum-cycles", "batch-process-freq",
        "min-ins",   "max-ins",   "delays-per-exec"};

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string key, value, extra;
        if (!(iss >> key >> value)) // blank line 
            continue;
        if (std::find(keys.begin(), keys.end(), key) == keys.end()) // documentation text
            continue;
        if (iss >> extra)
            continue; 
        raw[key] = value;
    }

    for (const auto &k : keys) {
        if (!raw.count(k)) {
            std::cout << "config.txt is missing '" << k << "'\n";
            return std::nullopt;
        }
    }

    std::string sched = raw["scheduler"];
    if (sched.size() >= 2 && sched.front() == '"' && sched.back() == '"')
        sched = sched.substr(1, sched.size() - 2);
    for (char &ch : sched)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (sched != "fcfs" && sched != "rr") {
        std::cout << "config.txt: scheduler must be \"fcfs\" or \"rr\"\n";
        return std::nullopt;
    }
    cfg.scheduler = sched;

    auto number = [&](const std::string &key, std::uint64_t lo,
                      std::uint64_t hi, std::uint64_t &out) {
        std::uint64_t v;
        if (!parseUInt(raw[key], v)) {
            std::cout << "config.txt: '" << key
                      << "' must be a non-negative integer\n";
            return false;
        }
        out = clampWarn(key, v, lo, hi);
        return true;
    };

    std::uint64_t cpus = 0;
    if (!number("num-cpu", 1, 128, cpus) ||
        !number("quantum-cycles", 1, MAX_U32_PLUS, cfg.quantum_cycles) ||
        !number("batch-process-freq", 1, MAX_U32_PLUS,
                cfg.batch_process_freq) ||
        !number("min-ins", 1, MAX_U32_PLUS, cfg.min_ins) ||
        !number("max-ins", 1, MAX_U32_PLUS, cfg.max_ins) ||
        !number("delays-per-exec", 0, MAX_U32_PLUS, cfg.delays_per_exec))
        return std::nullopt;
    cfg.num_cpu = static_cast<int>(cpus);

    if (cfg.min_ins > cfg.max_ins) {
        std::cout << "Warning: min-ins > max-ins, swapping them\n";
        std::swap(cfg.min_ins, cfg.max_ins);
    }
    return cfg;
}

struct SharedState {
    std::mutex m;
    std::unordered_map<std::string, std::shared_ptr<Process>> processes;
    std::deque<std::shared_ptr<Process>> ready;
    std::list<std::shared_ptr<Process>> sleeping;
    std::vector<std::shared_ptr<Process>> finished;
    // linked list for fast removes

    std::vector<Core> cores;
    Config config;
    std::uint64_t tick = 0;
    std::uint64_t nextProcessId = 0;
    std::atomic<bool> running{true};
    std::atomic<bool> generating{false};

    explicit SharedState(Config cfg) : config(std::move(cfg)) {}
    std::size_t coresUsedLastTick = 0;
};

void cpuTick(SharedState &s) {
    s.coresUsedLastTick = 0;

    for (auto it = s.sleeping.begin(); it != s.sleeping.end();) {
        if (--(*it)->sleepTicks == 0) {
            s.ready.push_back(*it);
            it = s.sleeping.erase(it);
        } else {
            it++;
        }
    }
    // wake sleeping processes

    for (auto &core : s.cores) {
        if (!core.currentProcess && !s.ready.empty()) {
            core.currentProcess = std::move(s.ready.front());
            s.ready.pop_front();
            core.currentProcess->executingCore = core.id;
            core.quantumUsed = 0;
            core.executionDelay = 0;
        }
    }
    // assign processes to active cores

    for (auto &core : s.cores) {
        core.lastRan = nullptr;
        if (!core.currentProcess)
            continue;

        bool release = false;
        bool didExecute = false;
        auto &p = core.currentProcess;
        if (core.executionDelay > 0) {
            core.executionDelay--;
            core.quantumUsed++;
            // TODO (2026-11-04) does the instruction delay burn a quantum? probably but check later
        } else {
            didExecute = true;
            core.lastRan = p;
            s.coresUsedLastTick++;
            p->execInstruction();
            core.executionDelay = s.config.delays_per_exec;
            core.quantumUsed++;
            if (p->finished()) {
                s.finished.push_back(p);
                release = true;
            } else if (p->sleepTicks > 0) {
                s.sleeping.push_back(p);
                release = true;
            }
        }

        if (!release && s.config.scheduler == "rr"
            && core.quantumUsed >= s.config.quantum_cycles
            && (s.config.strict_quantum || didExecute)) {
            s.ready.push_back(p);
            release = true;
        }

        if (release)
            core.currentProcess.reset();
    }

    if (s.generating && s.config.batch_process_freq > 0
        && s.tick % s.config.batch_process_freq == 0) {

        while (s.processes.count("p" + std::to_string(s.nextProcessId)))
            s.nextProcessId++;
        // skip ids whose p<id> name was already taken via screen -s
        std::string name = "p" + std::to_string(s.nextProcessId);

        auto p = makeProcess(&s.nextProcessId, name, s.config.min_ins,
                             s.config.max_ins);
        // yes next process id is incremented inside makeProcess instead
        // because you can manually insert processes with specific names using screen -s

        s.processes[name] = p;
        s.ready.push_back(p);
    }
    // process gen comes at the end of the CPU cycle

    s.tick++;
}

void writeProcessInfo(std::ostream &out,
                      const std::shared_ptr<Process> process) {

    std::stringstream logs;

    size_t len = 0;
    for (const auto &s : process->logs) {
        len += s.length();
    }

    std::string logString;
    logString.reserve(len);

    for (const auto &s : process->logs) {
        logString.append(s + "\n");
    }

    out << "Process name: " << process->name << "\n"
        << "ID: " << process->id << "\nLogs:\n"
        << logString << "\n"
        << "Current instruction line: " << process->executedLOC << "\n"
        << "Lines of code: " << process->totalLOC << "\n";

        if (process->finished())
            out << "\nFinished!\n";
}

void writeReport(std::ostream &out, const SharedState &s) {

    std::stringstream running;
    std::stringstream finished;

    constexpr int NAME_WIDTH = 15;
    constexpr int TIME_WIDTH = 28;
    constexpr int STATUS_WIDTH = 15;
    constexpr int LOC_WIDTH = 12;

    for (const auto &core : s.cores) {
        if (auto p = core.lastRan) {
            std::stringstream timestamp;
            timestamp << std::put_time(&p->creationTime,
                                       "(%m/%d/%Y) %I:%M:%S %p");

            std::stringstream status;
            status << "Core: " << core.id;

            std::stringstream loc;
            loc << p->executedLOC << " / " << p->totalLOC;

            running << std::left << std::setw(NAME_WIDTH) << p->name
                    << std::left << std::setw(TIME_WIDTH) << timestamp.str()
                    << std::left << std::setw(STATUS_WIDTH) << status.str()
                    << std::left << std::setw(LOC_WIDTH) << loc.str() << "\n";
        }
    }

    for (const auto &p : s.finished) {
        std::stringstream timestamp;
        timestamp << std::put_time(&p->creationTime, "(%m/%d/%Y) %I:%M:%S %p");

        std::stringstream loc;
        loc << p->executedLOC << " / " << p->totalLOC;

        finished << std::left << std::setw(NAME_WIDTH) << p->name << std::left
                 << std::setw(TIME_WIDTH) << timestamp.str() << std::left
                 << std::setw(STATUS_WIDTH) << "Finished" << std::left
                 << std::setw(LOC_WIDTH) << loc.str() << "\n";
    }

    out << "CPU utilization: "
        << (s.coresUsedLastTick / static_cast<float>(s.cores.size())) * 100
        << "%\n"
        << "Cores used: " << s.coresUsedLastTick << "\n"
        << "Cores available: " << s.cores.size() - s.coresUsedLastTick << "\n"
        << "\n------------------------------------------------------------"
           "\n"
        << "Running processes:\n"
        << running.rdbuf() << "\n"
        << "Finished processes:\n"
        << finished.rdbuf()
        << "\n------------------------------------------------------------"
           "\n";
}

std::shared_ptr<Process> insertNewProcess(const std::string &name,SharedState &s) {
    auto p = makeProcess(&s.nextProcessId, name, s.config.min_ins, s.config.max_ins);
    s.processes[name] = p;
    s.ready.push_back(p);
    return p;
}

constexpr auto TICK_DURATION = std::chrono::milliseconds(1);

void schedulerLoop(SharedState &s) {
   while (s.running) {
       {
           std::lock_guard<std::mutex> lock(s.m);
           cpuTick(s);
       }
       // sleep OUTSIDE the lock so the console thread can grab the mutex
       std::this_thread::sleep_for(TICK_DURATION);
   }
}

void setGenerating(SharedState &s, bool on) {
    std::lock_guard<std::mutex> lock(s.m);
    s.generating = on;
}

std::string buildReport(SharedState &s) {
    std::ostringstream out;
    std::lock_guard<std::mutex> lock(s.m);
    writeReport(out, s);
    return out.str();
}

std::vector<std::string> splitTokens(const std::string &line) {
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string t;
    while (iss >> t)
        tokens.push_back(t);
    return tokens;
}

// 1-32 chars of letters, digits, '_' or '-'
bool isValidProcessName(const std::string &name) {
    if (name.empty() || name.size() > 32)
        return false;
    for (char ch : name)
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_' &&ch != '-')
            return false;
    return true;
}

// the per-process console. process-smi and exit
void screenSession(SharedState &s, const std::shared_ptr<Process> &p) {
    std::cout << "Attached to process '" << p->name
        << "'. Type 'process-smi' for info, 'exit' to return.\n";

    std::string line;
    while (true) {
        std::cout << "root:\\" << p->name << "> " << std::flush;
        if (!std::getline(std::cin, line))
            return; // EOF

        auto tokens = splitTokens(line);
        if (tokens.empty())
            continue;

        if (tokens[0] == "exit") {
            return;
        }
        else if (tokens[0] == "process-smi") {
            std::ostringstream out;
            {
                std::lock_guard<std::mutex> lock(s.m);
                writeProcessInfo(out, p);
            }
            std::cout << out.str();
        }
        else {
            std::cout << "Unknown command: " << tokens[0] << "\n";
        }
    }
}

// runs on its own thread. reads commands until "exit" or EOF
void consoleLoop() {
    std::unique_ptr<SharedState> state;
    std::thread scheduler;

    std::string line;
    while (true) {
        std::cout << "root:\\> " << std::flush;
        if (!std::getline(std::cin, line)) {
            std::cout << "\n"; // ctrl-d: treat as exit
            break;
        }

        auto tokens = splitTokens(line);
        if (tokens.empty())
            continue;
        const std::string &cmd = tokens[0];

        static const std::vector<std::string> noArgCommands = {
            "exit", "initialize", "scheduler-start", "scheduler-stop",
            "report-util"};
        if (tokens.size() > 1 &&
            std::find(noArgCommands.begin(), noArgCommands.end(), cmd) != noArgCommands.end()) {
            std::cout << "'" << cmd << "' takes no arguments.\n";
            continue;
        }

        if (cmd == "exit") {
            break;
        }

        if (cmd == "initialize") {
            if (state) {
                std::cout << "Already initialized.\n";
                continue;
            }
            auto cfg = loadConfig("config.txt");
            if (!cfg)
                continue;
            state = std::make_unique<SharedState>(*cfg);
            state->cores.resize(static_cast<std::size_t>(cfg->num_cpu));
            for (std::size_t i = 0; i < state->cores.size(); i++)
                state->cores[i].id = static_cast<int>(i);
            scheduler = std::thread(schedulerLoop, std::ref(*state));
            std::cout << "Initialized with " << cfg->num_cpu << " CPUs ("
                      << cfg->scheduler << ").\n";
            continue;
        }

        if (!state) {
            std::cout << "Please run 'initialize' first.\n";
            continue;
        }

        if (cmd == "scheduler-start") {
            setGenerating(*state, true);
            std::cout << "Scheduler started: generating processes.\n";
            continue;
        }

        if (cmd == "scheduler-stop") {
            setGenerating(*state, false);
            std::cout << "Scheduler stopped: no longer generating processes.\n";
            continue;
        }

        if (cmd == "report-util") {
            std::string report = buildReport(*state);
            std::ofstream file("csopesy-log.txt"); // overwrites each time
            if (!file) {
                std::cout << "Could not write csopesy-log.txt\n";
                continue;
            }
            file << report;
            std::cout << "Report generated at csopesy-log.txt!\n";
            continue;
        }

        if (cmd == "screen") {
            bool lsForm = tokens.size() == 2 && tokens[1] == "-ls";
            bool nameForm = tokens.size() == 3 && (tokens[1] == "-s" || tokens[1] == "-r");
            if (!lsForm && !nameForm) {
                std::cout << "Usage: screen -ls | screen -s <name> | screen -r <name>\n";
                continue;
            }
            if (tokens[1] == "-s" && !isValidProcessName(tokens[2])) {
                std::cout << "Invalid process name. Use 1-32 letters, digits, '_' or '-'.\n";
                continue;
            }
        }

        if (cmd == "screen" && tokens.size() >= 2 && tokens[1] == "-ls") {
            std::cout << buildReport(*state);
            continue;
        }

        if (cmd == "screen" && tokens.size() >= 3 && tokens[1] == "-s") {
            const std::string &name = tokens[2];
            std::shared_ptr<Process> p;
            bool created = false;
            {
                std::lock_guard<std::mutex> lock(state->m);
                auto it = state->processes.find(name);
                if (it != state->processes.end()) {
                    p = it->second;
                }
                else {
                    p = insertNewProcess(name, *state);
                    created = true;
                }
            }
            if (created)
            std::cout << "Created new process '" << name << "'.\n";
            screenSession(*state, p);
            continue;
        }

        if (cmd == "screen" && tokens.size() >= 3 && tokens[1] == "-r") {
            const std::string &name = tokens[2];
            std::shared_ptr<Process> p;
            {
                std::lock_guard<std::mutex> lock(state->m);
                auto it = state->processes.find(name);
                if (it != state->processes.end())
                p = it->second;
            }
            if (!p) {
                std::cout << "Process '" << name << "' not found.\n";
                continue;
            }
            screenSession(*state, p);
            continue;
        }

        std::cout << "Unknown command: " << cmd << "\n";
    }

    if (state) {
        state->running = false;
        if (scheduler.joinable())
            scheduler.join();
    }
}

int main() {
    std::vector<std::string> developers = {"Hizon, Allen", "Infante, Charles",
                                           "Luna, Jacoba", "Marquez, Miguel"};
    const ::std::string DATE = "\nVersion date: 2026-11-05\n";

    std::cout << "Welcome to CSOPESY!" << "\n\n";
    std::cout << "Group developers:" << '\n';
    for (std::string dev : developers) {
        std::cout << dev << '\n';
    }

    std::cout << DATE << "\n";

    std::thread io(consoleLoop);
    io.join();
    return 0;
}

/* CPU scheduling design
    every tick {
        - check sleeping process list, decrement sleeps 
        - move non sleeping processes to ready queue
        - assign processes from ready queue to idle cores

        for (Core : Cores) {
            execute process
            ok also for print logs it should show the core # used to execute it 
        }
        no need for multithreading or anything since it's simulated CPU ticks
        edit 11/04 ok we need multithreading but like for a different thing ok 
    }
    */