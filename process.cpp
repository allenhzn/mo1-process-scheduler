#include "instructions.h"
#include "process.h"
#include <iostream>
#include <memory>

Process::Process(std::string name) : name(std::move(name)) {}

void Process::execInstruction() {
    while (!frames.empty()) {
        Frame &f = frames.back();
        Instruction *i = (*f.body)[f.next_instruction].get();
        f.next_instruction++;
        // increase before execution because the ForInstruction can add frames
        // and shift the frame list to make the index inaccurate

        bool control = i->isControl();
        i->execute();
        if (!control) {
            executedLOC++;
        }
        cleanupFrames();
        if (!control) {
            return;
        }
    }
}

void Process::cleanupFrames() {
    while (!frames.empty()) {
        Frame &f = frames.back();
        if (f.next_instruction < f.body->size()) {
            return;
            // there's still remaining instructions in this pass, don't run cleanup yet
        }

        if (f.remaining > 1) {
            f.remaining--;
            f.next_instruction = 0;
            // move onto the next pass by decreasing remaining and resetting next instruction pointer
        } else {
            frames.pop_back();
            // final pass done, fully remove current frame
        }
    }
}

void Process::pushFrame(std::vector<std::unique_ptr<Instruction>> &body,
                        std::uint16_t repeats) {
    if (repeats == 0 || body.empty())
        return; // exit once no more instructions are there
    frames.push_back(Frame{&body, 0, repeats});
}

void Process::addInstruction(std::unique_ptr<Instruction> instruction) {
    totalLOC += instruction->getLOC();
    program.push_back(std::move(instruction));
}

void Process::printLogs() {
    for (auto log : logs) {
        std::cout << log << std::endl;
    }
}

void Process::printVars() {
    for (const auto &var : vars) {
        std::cout << "Name: " << var.first << " | Value: " << var.second
                  << std::endl;
    }
}

// TODO (2026-11-02) consider separating Process struct into execution environment for Instructions
// and the actual administrative process info like PID used by the CPU scheduler
