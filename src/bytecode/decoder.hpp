#ifndef BYTECODE_DECODER_HPP
#define BYTECODE_DECODER_HPP

#include <cstdio>
#include <optional>

#include "source_file.hpp"

namespace lama::bytecode::decoder {
using instruction_span_t = std::pair<offset_t, std::uint32_t>;

std::optional<instruction_span_t> decodeInstruction(const lama::bytecode::BytecodeFile *file, offset_t offset);
std::optional<std::int32_t> getJumpAddress(const lama::bytecode::BytecodeFile *file, offset_t offset);
void printInstruction(::FILE *f, const lama::bytecode::BytecodeFile *file, offset_t offset);
}

#endif
