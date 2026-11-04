#pragma once
#include "instructions.h"
#include <memory>
#include <optional>

std::unique_ptr<Instruction>
makeRandomInstruction(std::weak_ptr<Process> process, const std::string &name, int maxDepth,
                      std::optional<std::string> customMsg = std::nullopt);

std::shared_ptr<Process> makeProcess(const std::string &name, std::uint32_t minInstructions,
                                     std::uint32_t maxInstructions,
                                     std::optional<std::string> customMsg = std::nullopt);