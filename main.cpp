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
    std::atomic<bool> generating{true};

    explicit SharedState(Config cfg) : config(std::move(cfg)) {}
    std::size_t coresUsedLastTick = 0;
};

void cpuTick(SharedState &s) {
    bool anyCoreBusy = false;
    s.coresUsedLastTick = 0;

    for (auto &c : s.cores)
        if (c.currentProcess)
            anyCoreBusy = true;

    if (s.sleeping.empty() && s.ready.empty() && !anyCoreBusy && !s.generating)
        // TODO (2026-11-04) check if we need to keep it as long as the scheduler is generating
        s.running = false;

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

void insertNewProcess(const std::string &name, SharedState &s) {
    auto p =
        makeProcess(&s.nextProcessId, name, s.config.min_ins, s.config.max_ins);
    s.processes[name] = p;
    s.ready.push_back(p);
}

void schedulerLoop(SharedState &s) {
    while (s.running) {
        {
            std::lock_guard<std::mutex> lock(s.m);
            cpuTick(s);
            if (s.tick > 100) {
                s.generating = false;
            }
            if (s.tick == 32) {
                std::string name = "screen_01";
                insertNewProcess(name, s);
            }
            if (s.tick == 62) {
                writeReport(std::cout, s);
                writeProcessInfo(std::cout, s.processes["screen_01"]);
            }
        }
        //std::this_thread::sleep_for(std::chrono::milliseconds(1));
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

    std::cout << DATE;

    auto config = Config{4, "rr", 10, 10, 10, 30, 2};
    // TODO (2026-11-03) set based on config.txt

    auto state = SharedState{config};
    state.cores.resize(config.num_cpu);
    for (std::size_t i = 0; i < state.cores.size(); i++) {
        state.cores[i].id = static_cast<int>(i);
    }

    schedulerLoop(state);
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