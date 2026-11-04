#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct Instruction;
using VarMap = std::unordered_map<std::string, std::uint16_t>;

struct Frame {
    std::vector<std::unique_ptr<Instruction>> *body; // for loop instructions
    std::size_t next_instruction = 0; // current index in the for loop
    std::uint16_t remaining;          // remaining loops
};

struct Process {
    std::string name;
    std::uint64_t id;
    std::uint16_t sleepTicks = 0;
    std::uint64_t totalLOC = 0;
    std::uint64_t executedLOC = 0;
    std::tm creationTime;
    int executingCore = -1;

    VarMap vars;
    std::vector<std::unique_ptr<Instruction>> program;
    std::vector<Frame> frames;
    std::vector<std::string> logs;

    explicit Process(std::string name);
    ~Process() = default;

    void cleanupFrames();
    void pushFrame(std::vector<std::unique_ptr<Instruction>> &body,
                   std::uint16_t repeats);
    void addInstruction(std::unique_ptr<Instruction> instruction);
    void execInstruction();

    bool finished() const { return frames.empty(); }

    // TODO (2026-11-03) debug stuff below, remove from final
    void printVars();
    void printLogs();
};