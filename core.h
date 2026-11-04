#include <cstdint>
#include <deque>
#include <memory>

struct Process;

struct Core {
    int id;    
    std::shared_ptr<Process> currentProcess = nullptr;
    // don't need idle flag cause just check if currentProcess = nullptr I think

    std::uint32_t quantumUsed = 0;
};