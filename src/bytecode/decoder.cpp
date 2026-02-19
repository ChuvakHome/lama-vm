#include "decoder.hpp"

#include <cstdint>

#include <optional>

#include "bytecode_instructions.hpp"

std::optional<std::int32_t> lama::bytecode::decoder::getJumpAddress(const lama::bytecode::BytecodeFile *file, offset_t offset) {
    const lama::bytecode::InstructionOpCode op = file->getInstruction(offset);

    std::int32_t val;
    std::optional<std::int32_t> res = std::nullopt;

    switch (op) {
        case InstructionOpCode::JMP:
        case InstructionOpCode::CJMPZ:
        case InstructionOpCode::CJMPNZ:
        case InstructionOpCode::CLOSURE:
        case InstructionOpCode::CALL:
            file->copyCodeBytes(reinterpret_cast<std::byte *>(&val), offset + sizeof(op), sizeof(val));
            res = {val};
            break;
        default:
            break;
    }

    return res;
}
