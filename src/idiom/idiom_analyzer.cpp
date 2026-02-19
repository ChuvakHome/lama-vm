#include "idiom_analyzer.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ios>
#include <iostream>
#include <optional>
#include <stack>
#include <string_view>
#include <vector>

#include "../bytecode/bytecode_instructions.hpp"
#include "../bytecode/decoder.hpp"
#include "../bytecode/source_file.hpp"

extern "C" {
    int32_t disassemble_instruction(FILE *f, const lama::bytecode::bytefile_t *bf, uint32_t offset);
}

namespace {
    bool isJumpInstr(lama::bytecode::InstructionOpCode opcode) {
        return opcode == lama::bytecode::InstructionOpCode::JMP
               || opcode == lama::bytecode::InstructionOpCode::CJMPZ
               || opcode == lama::bytecode::InstructionOpCode::CJMPNZ
               || opcode == lama::bytecode::InstructionOpCode::CALL // CALLC performs jump too, but has no explicit jump address
               ;
    }

    bool isCallInstr(lama::bytecode::InstructionOpCode opcode) {
        return opcode == lama::bytecode::InstructionOpCode::CALL
               || opcode == lama::bytecode::InstructionOpCode::CALLC
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

    int compareIdioms(const lama::bytecode::BytecodeFile *file, const lama::idiom::idiom_record_t &r1, const lama::idiom::idiom_record_t &r2) {
        const std::byte * const ptr1 = &file->getCodeByte(r1.first);
        const std::byte * const ptr2 = &file->getCodeByte(r2.first);

        return std::memcmp(ptr1, ptr2, r1.second);
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

    void checkInstruction(std::int32_t instrLen, lama::bytecode::offset_t ip) {
        assertWithIp(instrLen >= 0, "invalid opcode", ip);
        assertWithIp(instrLen > 0, "unexpected end of code section", ip);
    }
}

lama::idiom::detail::IdiomAnalyzer::IdiomAnalyzer(const bytecode::BytecodeFile *file)
    : bytecodeFile_(file)
    , reachableInstrs_(file->getCodeSize(), false) // all addresses are considered unreachable until otherwise is proven
    , labeled_(file->getCodeSize(), false) {

}

void lama::idiom::detail::IdiomAnalyzer::preprocess() {
    std::stack<lama::bytecode::offset_t> instructionsToProcess;

    for (std::size_t i = 0; i < bytecodeFile_->getPublicSymbolsNumber(); ++i) {
        auto&&[_, offset] = bytecodeFile_->getPublicSymbol(i);
        assertCodeOffset(offset, bytecodeFile_->getCodeSize());

        // avoid public symbols overlapping problems
        if (!labeled_[offset]) {
            labeled_[offset] = true;
            instructionsToProcess.push(offset);
        }
    }

    while (!instructionsToProcess.empty()) {
        const lama::bytecode::offset_t instrPos = instructionsToProcess.top();
        assertCodeOffset(instrPos, bytecodeFile_->getCodeSize());

        instructionsToProcess.pop();

        reachableInstrs_[instrPos] = true;
        const lama::bytecode::InstructionOpCode op = bytecodeFile_->getInstruction(instrPos);

        const std::int32_t instrLen = ::disassemble_instruction(stdin, bytecodeFile_->getRawBytefile(), instrPos);
        checkInstruction(instrLen, instrPos);

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

            if (nextInstrPos < bytecodeFile_->getCodeSize()) {
                if (!reachableInstrs_[nextInstrPos]) {
                    reachableInstrs_[nextInstrPos] = true;
                    instructionsToProcess.push(nextInstrPos);
                }

                if (isCallInstr(op)) {
                    labeled_[nextInstrPos] = true;
                }
            }
        }
    }
}

void lama::idiom::detail::IdiomAnalyzer::findIdioms(
    std::vector<idiom_record_t> &idioms1,
    std::vector<idiom_record_t> &idioms2
) {
    preprocess();

    idioms1.reserve(bytecodeFile_->getCodeSize());
    idioms2.reserve(bytecodeFile_->getCodeSize());

    lama::bytecode::offset_t ip = 0;

    while (ip < bytecodeFile_->getCodeSize()) {
        if (!reachableInstrs_[ip]) {
            ip++;
            continue;
        }

        const lama::bytecode::InstructionOpCode op = bytecodeFile_->getInstruction(ip);
        const std::int32_t instrLen = ::disassemble_instruction(stdin, bytecodeFile_->getRawBytefile(), ip);
        checkInstruction(instrLen, ip);

        idioms1.push_back({ip, instrLen});

        const lama::bytecode::offset_t nextInstrPos = ip + instrLen;

        if (nextInstrPos < bytecodeFile_->getCodeSize() && !isBreakingBytecodeSequenceInstr(op)) {
            if (!labeled_[nextInstrPos] && reachableInstrs_[nextInstrPos]) {
                const std::int32_t nextInstrLen = ::disassemble_instruction(stdin, bytecodeFile_->getRawBytefile(), nextInstrPos);
                checkInstruction(nextInstrLen, nextInstrPos);

                idioms2.push_back({ip, instrLen + nextInstrLen});
            }
        }

        ip += instrLen;
    }
}

void lama::idiom::detail::collectFrequencies(
    const bytecode::BytecodeFile *file,
    std::vector<idiom_record_t> &idioms
) {
    std::sort(idioms.begin(), idioms.end(), [file](const idiom_record_t &r1, const idiom_record_t &r2) {
        return compareIdioms(file, r1, r2) < 0;
    });

    std::uint32_t freq = 1;
    std::uint32_t uniqIdioms = 0;

    for (std::uint32_t i = 1; i < idioms.size(); ++i) {
        const std::uint32_t span1Len = idioms[i - 1].second;
        const std::uint32_t span2Len = idioms[i].second;

        if (compareIdioms(file, { idioms[i - 1].first, span1Len }, { idioms[i].first, span2Len }) == 0) {
            freq++;
        } else {
            idioms[uniqIdioms++] = { idioms[i - 1].first, freq };
            freq = 1;
        }
    }

    idioms[uniqIdioms++] = { idioms.back().first, freq };
    idioms.resize(uniqIdioms);

    std::sort(idioms.begin(), idioms.end(), [](const idiom_record_t &r1, const idiom_record_t &r2){
        return r1.second > r2.second;
    });
}
