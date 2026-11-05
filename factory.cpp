#include "factory.h"
#include "instructions.h"
#include "process.h"
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::mt19937 &rng() {
    static std::mt19937 gen{std::random_device{}()};
    return gen;
}

int randInt(int min, int max) {
    return std::uniform_int_distribution<int>(min, max)(rng());
}

std::string randVarName() { return "var" + std::to_string(randInt(1, 256)); }

VarChoice randOperand() {
    if (randInt(0, 1) == 0)
        return VarChoice{randVarName()};
    return VarChoice{static_cast<std::uint16_t>(randInt(0, 100))};
}

std::vector<std::string_view> split(std::string_view str, char delim) {
    std::vector<std::string_view> result;
    size_t start = 0;
    size_t end = str.find(delim);

    while (end != std::string_view::npos) {
        result.push_back(str.substr(start, end - start));
        start = end + 1;
        end = str.find(delim, start);
    }

    result.push_back(str.substr(start));
    return result;
}

} // namespace

std::unique_ptr<Instruction>
makeRandomInstruction(std::weak_ptr<Process> process, const std::string &name,
                      int maxDepth, std::optional<std::string> customMsg) {
    int choice = randInt(0, maxDepth > 0 ? 5 : 4);
    // only generates FOR instructions up to the maximum recursion depth
    switch (choice) {
    case 0:
        return std::make_unique<PrintInstruction>(
            process,
            customMsg ? *customMsg : "Hello world from process " + name + "!");
    case 1:
        return std::make_unique<DeclareInstruction>(
            process, Variable(randVarName(),
                              static_cast<std::uint16_t>(randInt(0, 100))));
    case 2:
        return std::make_unique<AddInstruction>(process, randVarName(),
                                                randOperand(), randOperand());
    case 3:
        return std::make_unique<SubInstruction>(process, randVarName(),
                                                randOperand(), randOperand());
    case 4:
        return std::make_unique<SleepInstruction>(
            process, static_cast<std::uint8_t>(randInt(1, 5)));
    default: {
        std::vector<std::unique_ptr<Instruction>> body;
        int n = randInt(1, 3);
        for (int i = 0; i < n; ++i)
            body.push_back(
                makeRandomInstruction(process, name, maxDepth - 1, customMsg));
        return std::make_unique<ForInstruction>(
            process, std::move(body),
            static_cast<std::uint16_t>(randInt(2, 4)));
    }
    }
}

std::unique_ptr<Instruction>
makeAltPrintAdd(std::weak_ptr<Process> process, const std::string &name,
                int maxDepth, std::optional<std::string> customMsg, int *ctr) {

    if (auto p = process.lock()) {
        switch (*ctr % 2) {
        case 0:
            return std::make_unique<PrintInstruction>(process,
                                                      "Value from: ", "x");
        case 1:
            return std::make_unique<AddInstruction>(
                process, "x", "x", static_cast<uint16_t>(randInt(1, 10)));
        }
    }
    return nullptr;
}

std::shared_ptr<Process> makeProcess(std::uint64_t *nextProcessId,
                                     const std::string &name,
                                     std::uint64_t minInstructions,
                                     std::uint64_t maxInstructions,
                                     std::optional<std::string> customMsg) {
    auto process = std::make_shared<Process>(name);
    std::weak_ptr<Process> weak = process;

    std::uint64_t target = std::uniform_int_distribution<std::uint64_t>(
        minInstructions, maxInstructions)(rng());

    std::uint64_t total = 0;
    while (total < target) {
        auto i = makeRandomInstruction(
            weak, name, (target - total > 50) ? 2 : 0, customMsg);
        if (total + static_cast<std::uint64_t>(i->getLOC()) > target)
            i = makeRandomInstruction(weak, name, 0, customMsg);
        total += static_cast<std::uint64_t>(i->getLOC());
        // if we get close enough to the limit just start making non-for loop instructions so we guarantee
        // following the LOC constraints

        process->addInstruction(std::move(i));
    }

    // auto parts = split(name, '_');

    // if (parts.front() == "testCustomMsg" && parts.size() == 3) {
    //     auto name = std::string(parts[1]);
    //     auto valString = std::string(parts[2]);
    //     uint16_t val = 0;
    //     std::stringstream ss;

    //     ss << "The value of custom variable " << name
    //        << " is: " << std::to_string(val);

    //     auto [ptr, ec] = std::from_chars(
    //         valString.data(), valString.data() + valString.size(), val);
    //     if (ec == std::errc()) {
    //         process->addInstruction(std::make_unique<DeclareInstruction>(
    //             weak, Variable(name, val)));
    //         process->addInstruction(
    //             std::make_unique<PrintInstruction>(weak, ss.str(), name));
    //     }
    // }

    process->pushFrame(process->program, 1);
    process->id = (*nextProcessId)++;
    // process->vars.try_emplace("x", 0);

    auto now = std::chrono::system_clock::now();
    std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
    process->creationTime = *std::localtime(&now_time_t);
    return process;
}