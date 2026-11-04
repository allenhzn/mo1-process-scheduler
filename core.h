#include <cstdint>
#include <deque>
#include <memory>

struct Process;

struct Core {
    int id;
    std::shared_ptr<Process> currentProcess = nullptr;
    // don't need idle flag cause just check if currentProcess = nullptr

    std::shared_ptr<Process> lastRan;
    std::uint64_t quantumUsed = 0;
    std::uint64_t executionDelay = 0;
};