#pragma once
#include "instructions.h"
#include <memory>
#include <optional>

std::unique_ptr<Instruction>
makeRandomInstruction(std::weak_ptr<Process> process, const std::string &name,
                      int maxDepth,
                      std::optional<std::string> customMsg = std::nullopt);

std::shared_ptr<Process>
makeProcess(std::uint64_t *nextProcessId, const std::string &name,
            std::uint64_t minInstructions, std::uint64_t maxInstructions,
            std::optional<std::string> customMsg = std::nullopt);

std::unique_ptr<Instruction>
makeAltPrintAdd(std::weak_ptr<Process> process, const std::string &name,
                int maxDepth, std::optional<std::string> customMsg, int *ctr);
