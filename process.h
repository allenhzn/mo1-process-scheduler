#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

struct Instruction;
using VarMap = std::unordered_map<std::string, std::uint16_t>;

struct Frame {
    std::vector<std::unique_ptr<Instruction>> *body; // for loop instructions
    std::size_t next_instruction = 0;                // current index in the for loop
    std::uint16_t remaining;                         // remaining loops
};

struct Process {
    std::string name;
    std::uint16_t sleepTicks = 0;
    int executingCore = -1;

    VarMap vars;
    std::vector<std::unique_ptr<Instruction>> program;
    std::vector<Frame> frames;
    std::vector<std::string> logs;

    explicit Process(std::string name);
    ~Process() = default;

    void cleanupFrames();
    void pushFrame(std::vector<std::unique_ptr<Instruction>> &body, std::uint16_t repeats);
    void addInstruction(std::unique_ptr<Instruction> instruction);
    void execInstruction();
    void printLogs();
    bool finished() const { return frames.empty(); }

    // TODO (2026-11-03) debug stuff below, remove from final
    void printVars();
};