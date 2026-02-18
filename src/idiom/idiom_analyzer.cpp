#include "idiom_analyzer.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ios>
#include <iostream>
#include <optional>
#include <stack>
#include <string_view>

#include "../bytecode/bytecode_instructions.hpp"
#include "../bytecode/decoder.hpp"
#include "../bytecode/source_file.hpp"

using lama::bytecode::decoder::instruction_span_t;

namespace {
    bool isJumpInstr(lama::bytecode::InstructionOpCode opcode) {
        return opcode == lama::bytecode::InstructionOpCode::JMP
               || opcode == lama::bytecode::InstructionOpCode::CJMPZ
               || opcode == lama::bytecode::InstructionOpCode::CJMPNZ
               || opcode == lama::bytecode::InstructionOpCode::CALL // CALLC performs jump too, but has no explicit jump address
               ;
    }

    bool isTerminalInstr(lama::bytecode::InstructionOpCode opcode) {
        return opcode == lama::bytecode::InstructionOpCode::JMP
               || opcode == lama::bytecode::InstructionOpCode::RET
               || opcode == lama::bytecode::InstructionOpCode::END
               || opcode == lama::bytecode::InstructionOpCode::FAIL
               ;
    }

    bool isBreakingBytecodeSequenceInstr(lama::bytecode::InstructionOpCode opcode) {
        return opcode == lama::bytecode::InstructionOpCode::JMP
               || opcode == lama::bytecode::InstructionOpCode::CALL
               || opcode == lama::bytecode::InstructionOpCode::CALLC
               || opcode == lama::bytecode::InstructionOpCode::RET
               || opcode == lama::bytecode::InstructionOpCode::END
               || opcode == lama::bytecode::InstructionOpCode::FAIL
               ;
    }

    int compareIdioms(const lama::bytecode::BytecodeFile *file, const instruction_span_t &span1, const instruction_span_t &span2) {
        const std::byte * const ptr1 = &file->getCodeByte(span1.first);
        const std::byte * const ptr2 = &file->getCodeByte(span2.first);

        const int cmpres = std::memcmp(ptr1, ptr2, std::min(span1.second, span2.second));

        return cmpres == 0
                ? span1.second - span2.second
                : cmpres
                ;
    }

    void assertCodeOffset(lama::bytecode::offset_t offset, std::size_t codeSize) {
        if (offset >= codeSize) {
            std::cerr << "Internal error: code offset ("
                      << std::hex << std::showbase << offset
                      << ") out of range\n";

            std::exit(-42);
        }
    }

    void assertWithIp(bool condition, std::string_view msg, lama::bytecode::offset_t ip) {
        if (!condition) {
            std::cerr << "Internal error (code offset: "
                      << std::hex << std::showbase << ip
                      << "): " << msg << '\n';

            std::exit(-42);
        }
    }
}

lama::idiom::detail::IdiomAnalyzer::IdiomAnalyzer(const bytecode::BytecodeFile *file)
    : bytecodeFile_(file)
    , preprocessed_(false)
    , reachableInstrs_(file->getCodeSize(), false) // all addresses are considered unreachable until otherwise is proven
    , labeled_(file->getCodeSize(), false) {

}

void lama::idiom::detail::IdiomAnalyzer::preprocess() {
    std::stack<lama::bytecode::offset_t> instructionsToProcess;

    for (std::size_t i = 0; i < bytecodeFile_->getPublicSymbolsNumber(); ++i) {
        auto&&[_, offset] = bytecodeFile_->getPublicSymbol(i);

        assertCodeOffset(offset, bytecodeFile_->getCodeSize());
        instructionsToProcess.push(offset);
    }

    while (!instructionsToProcess.empty()) {
        const lama::bytecode::offset_t instrPos = instructionsToProcess.top();
        assertCodeOffset(instrPos, bytecodeFile_->getCodeSize());

        instructionsToProcess.pop();

        reachableInstrs_[instrPos] = true;
        const lama::bytecode::InstructionOpCode op = bytecodeFile_->getInstruction(instrPos);

        auto decodeRes = lama::bytecode::decoder::decodeInstruction(bytecodeFile_, instrPos);
        assertWithIp(decodeRes.has_value(), "wrong instruction opcode", instrPos);

        const std::size_t instrLen = decodeRes.value().second;

        if (isJumpInstr(op)) {
            const std::int32_t jumpTarget = lama::bytecode::decoder::getJumpAddress(bytecodeFile_, instrPos).value();
            assertWithIp(jumpTarget >= 0 && jumpTarget < bytecodeFile_->getCodeSize(), "wrong jump", instrPos);

            labeled_[jumpTarget] = true;

            if (!reachableInstrs_[jumpTarget]) {
                reachableInstrs_[jumpTarget] = true;
                instructionsToProcess.push(jumpTarget);
            }
        }

        if (!isTerminalInstr(op)) {
            const lama::bytecode::offset_t nextInstrPos = instrPos + instrLen;

            if (nextInstrPos < bytecodeFile_->getCodeSize() && !reachableInstrs_[nextInstrPos]) {
                reachableInstrs_[nextInstrPos] = true;
                instructionsToProcess.push(nextInstrPos);
            }
        }
    }
}

std::vector<instruction_span_t> lama::idiom::detail::IdiomAnalyzer::findIdioms() {
    if (!preprocessed_) {
        preprocess();
        preprocessed_ = true;
    }

    std::vector<instruction_span_t> idioms;

    lama::bytecode::offset_t ip = 0;

    while (ip < bytecodeFile_->getCodeSize()) {
        if (!reachableInstrs_[ip]) {
            ip++;
            continue;
        }

        const lama::bytecode::InstructionOpCode op = bytecodeFile_->getInstruction(ip);

        auto decodeRes = lama::bytecode::decoder::decodeInstruction(bytecodeFile_, ip);
        assertWithIp(decodeRes.has_value(), "wrong instruction opcode", ip);

        const std::size_t instrLen = decodeRes.value().second;

        idioms.push_back({ip, instrLen});

        const lama::bytecode::offset_t nextInstrPos = ip + instrLen;

        if (nextInstrPos < bytecodeFile_->getCodeSize() && !isBreakingBytecodeSequenceInstr(op)) {
            if (!labeled_[nextInstrPos] && reachableInstrs_[nextInstrPos]) {
                decodeRes = lama::bytecode::decoder::decodeInstruction(bytecodeFile_, nextInstrPos);
                assertWithIp(decodeRes.has_value(), "wrong instruction opcode", ip);

                const std::size_t nextInstrLen = decodeRes.value().second;

                idioms.push_back({ip, instrLen + nextInstrLen});
            }
        }

        ip += instrLen;
    }

    return idioms;
}

std::vector<lama::idiom::detail::frequency_result_t> lama::idiom::detail::countFrequencies(
    const bytecode::BytecodeFile *file,
    std::vector<instruction_span_t> &idioms
) {
    std::sort(idioms.begin(), idioms.end(), [file](const instruction_span_t &span1, const instruction_span_t &span2) {
        return compareIdioms(file, span1, span2) < 0;
    });

    std::vector<lama::idiom::detail::frequency_result_t> frequencies;
    std::size_t freq = 1;

    for (std::size_t i = 1; i < idioms.size(); ++i) {
        instruction_span_t prev = idioms[i - 1];
        instruction_span_t curr = idioms[i];

        if (compareIdioms(file, prev, curr) == 0) {
            ++freq;
        } else {
            frequencies.push_back({prev, freq});
            freq = 1;
        }
    }

    frequencies.push_back({idioms.back(), freq});

    return frequencies;
}
