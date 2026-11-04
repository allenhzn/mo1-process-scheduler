#include "instructions.h"
#include "process.h"
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <variant>
#include <vector>

std::uint64_t PrintInstruction::getLOC() { return 1; }
std::uint64_t DeclareInstruction::getLOC() { return 1; }
std::uint64_t AddInstruction::getLOC() { return 1; }
std::uint64_t SubInstruction::getLOC() { return 1; }
std::uint64_t SleepInstruction::getLOC() { return 1; }

PrintInstruction::PrintInstruction(std::weak_ptr<Process> process,
                                   std::string msg)
    : Instruction(std::move(process)), msg(std::move(msg)) {}

void PrintInstruction::execute() {
    if (auto p = process.lock()) {
        auto now = std::chrono::system_clock::now();
        std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm local_tm = *std::localtime(&now_time_t);
        std::ostringstream oss;
        oss << std::put_time(&local_tm, "(%m/%d/%Y %I:%M:%S %p) ")
            << " Core:" << p->executingCore << "  " << '"' << msg << '"';
        p->logs.emplace_back(oss.str());
    } else {
        // process has been removed
    }
}

DeclareInstruction::DeclareInstruction(std::weak_ptr<Process> process,
                                       Variable var)
    : Instruction(std::move(process)), var(var) {}

void DeclareInstruction::execute() {
    if (auto p = process.lock()) {
        p->vars.emplace(var);
    } else {
        // process has been removed
    }
};

AddInstruction::AddInstruction(std::weak_ptr<Process> process, std::string var1,
                               VarChoice val2, VarChoice val3)
    : Instruction(std::move(process)), var1(var1), val2(val2), val3(val3) {}

void AddInstruction::execute() {
    if (auto p = process.lock()) {
        uint16_t a, b;

        if (std::holds_alternative<std::string>(val2)) {
            auto [it2, _2] =
                p->vars.try_emplace(std::get<std::string>(val2), 0);
            a = it2->second;
        } else {
            a = std::get<uint16_t>(val2);
        }

        if (std::holds_alternative<std::string>(val3)) {
            auto [it3, _3] =
                p->vars.try_emplace(std::get<std::string>(val3), 0);
            b = it3->second;
        } else {
            b = std::get<uint16_t>(val3);
        }

        p->vars[var1] = a + b;
        // var1 value = val2 + val3
    } else {
        // process has been removed
    }
};

SubInstruction::SubInstruction(std::weak_ptr<Process> process, std::string var1,
                               VarChoice val2, VarChoice val3)
    : Instruction(std::move(process)), var1(var1), val2(val2), val3(val3) {}

void SubInstruction::execute() {
    if (auto p = process.lock()) {
        uint16_t a, b;

        if (std::holds_alternative<std::string>(val2)) {
            auto [it2, _2] =
                p->vars.try_emplace(std::get<std::string>(val2), 0);
            a = it2->second;
        } else {
            a = std::get<uint16_t>(val2);
        }

        if (std::holds_alternative<std::string>(val3)) {
            auto [it3, _3] =
                p->vars.try_emplace(std::get<std::string>(val3), 0);
            b = it3->second;
        } else {
            b = std::get<uint16_t>(val3);
        }

        p->vars[var1] = a - b;
        // var1 value = val2 - val3
    } else {
        // process has been removed
    }
};

SleepInstruction::SleepInstruction(std::weak_ptr<Process> process,
                                   std::uint8_t ticks)
    : Instruction(std::move(process)), ticks(ticks) {};

void SleepInstruction::execute() {
    if (auto p = process.lock()) {
        p->sleepTicks += ticks;
    } else {
        // process has been removed
    }
}

ForInstruction::ForInstruction(std::weak_ptr<Process> process,
                               std::vector<std::unique_ptr<Instruction>> body,
                               std::uint16_t repeats)
    : Instruction(std::move(process)), body(std::move(body)), repeats(repeats) {
}

void ForInstruction::execute() {
    if (auto p = process.lock()) {
        p->pushFrame(body, repeats);
    } else {
        // process has been removed
    }
}

std::uint64_t ForInstruction::getLOC() {
    int total = 0;
    for (auto &inst : body)
        total += inst->getLOC();
    return total * repeats;
}