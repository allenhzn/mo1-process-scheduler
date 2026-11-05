#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

struct Process;

using Variable = std::pair<std::string, std::uint16_t>;
// variable typedef for a pair of the string (name) and the uint16 value

using VarChoice = std::variant<std::string, std::uint16_t>;
// for add and sub instructions, they take either a variable name or immediate value
// if the name hasn't already been used to declare a variable, auto-declare it with a value of 0

// the various instructions a process can hold
// they will inherit from instruction and have a shared execute abstract method
struct Instruction {
  protected:
    std::weak_ptr<Process> process;

  public:
    Instruction(std::weak_ptr<Process> process) : process(process) {}
    virtual ~Instruction() = default;
    virtual void execute() = 0;
    virtual std::uint64_t getLOC() = 0;
    virtual bool isControl() const { return false; }
    // control = start of for loop instead of an instruction that takes a tick
    // this way it instantly moves to the inside of the for loop and executes the first instruction in the same tick
};

struct PrintInstruction : Instruction {
    std::string msg;
    std::optional<std::string> varName;
    PrintInstruction(std::weak_ptr<Process> process, std::string msg,
                     std::optional<std::string> varName = std::nullopt);
    void execute() override;
    std::uint64_t getLOC() override;
};

struct DeclareInstruction : Instruction {
    Variable var;
    DeclareInstruction(std::weak_ptr<Process> process, Variable var);
    void execute() override;
    std::uint64_t getLOC() override;
};

struct AddInstruction : Instruction {
    std::string var1;
    VarChoice val2;
    VarChoice val3;
    AddInstruction(std::weak_ptr<Process> process, std::string var1,
                   VarChoice var2, VarChoice var3);
    void execute() override;
    std::uint64_t getLOC() override;
};

struct SubInstruction : Instruction {
    std::string var1;
    VarChoice val2;
    VarChoice val3;
    SubInstruction(std::weak_ptr<Process> process, std::string var1,
                   VarChoice var2, VarChoice var3);
    void execute() override;
    std::uint64_t getLOC() override;
};

struct SleepInstruction : Instruction {
    std::uint8_t ticks;
    SleepInstruction(std::weak_ptr<Process> process, std::uint8_t ticks);
    void execute() override;
    std::uint64_t getLOC() override;
};

struct ForInstruction : Instruction {
    std::vector<std::unique_ptr<Instruction>> body;
    std::uint16_t repeats;

    ForInstruction(std::weak_ptr<Process> process,
                   std::vector<std::unique_ptr<Instruction>> body,
                   std::uint16_t repeats);
    void execute() override;
    std::uint64_t getLOC() override;
    bool isControl() const override { return true; }
};
