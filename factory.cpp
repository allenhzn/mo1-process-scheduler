#include "factory.h"
#include "process.h"
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace {

std::mt19937 &rng() {
    static std::mt19937 gen{std::random_device{}()};
    return gen;
}

int randInt(int min, int max) { return std::uniform_int_distribution<int>(min, max)(rng()); }

std::string randVarName() { return "var" + std::to_string(randInt(1, 256)); }

VarChoice randOperand() {
    if (randInt(0, 1) == 0)
        return VarChoice{randVarName()};
    return VarChoice{static_cast<std::uint16_t>(randInt(0, 100))};
}

} // namespace

std::unique_ptr<Instruction> makeRandomInstruction(std::weak_ptr<Process> process,
                                                   const std::string &name, int maxDepth,
                                                   std::optional<std::string> customMsg) {
    int choice = randInt(0, maxDepth > 0 ? 5 : 4);
    // only generates FOR instructions up to the maximum recursion depth
    switch (choice) {
    case 0:
        return std::make_unique<PrintInstruction>(
            process, customMsg ? *customMsg : "Hello world from process " + name);
    case 1:
        return std::make_unique<DeclareInstruction>(
            process, Variable(randVarName(), static_cast<std::uint16_t>(randInt(0, 100))));
    case 2:
        return std::make_unique<AddInstruction>(process, randVarName(), randOperand(),
                                                randOperand());
    case 3:
        return std::make_unique<SubInstruction>(process, randVarName(), randOperand(),
                                                randOperand());
    case 4:
        return std::make_unique<SleepInstruction>(process,
                                                  static_cast<std::uint8_t>(randInt(1, 5)));
    default: {
        std::vector<std::unique_ptr<Instruction>> body;
        int n = randInt(1, 3);
        for (int i = 0; i < n; ++i)
            body.push_back(makeRandomInstruction(process, name, maxDepth - 1, customMsg));
        return std::make_unique<ForInstruction>(process, std::move(body),
                                                static_cast<std::uint16_t>(randInt(2, 4)));
    }
    }
}

std::shared_ptr<Process> makeProcess(const std::string &name, std::uint32_t minInstructions,
                                     std::uint32_t maxInstructions,
                                     std::optional<std::string> customMsg) {
    auto process = std::make_shared<Process>(name);
    std::weak_ptr<Process> weak = process;

    std::uint32_t target =
        std::uniform_int_distribution<std::uint32_t>(minInstructions, maxInstructions)(rng());

    std::uint32_t total = 0;
    while (total < target) {
        auto i = makeRandomInstruction(weak, name, (target - total > 50) ? 2 : 0, customMsg);
        if (total + static_cast<std::uint32_t>(i->getLOC()) > target)
            i = makeRandomInstruction(weak, name, 0, customMsg);
        total += static_cast<std::uint32_t>(i->getLOC());
        // if we get close enough to the limit just start making non-for loop instructions so we guarantee
        // following the LOC constraints

        process->addInstruction(std::move(i));
    }

    process->pushFrame(process->program, 1);
    return process;
}