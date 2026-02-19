#ifndef BYTECODE_DECODER_HPP
#define BYTECODE_DECODER_HPP

#include <optional>

#include "source_file.hpp"

namespace lama::bytecode::decoder {
std::optional<std::int32_t> getJumpAddress(const lama::bytecode::BytecodeFile *file, offset_t offset);
}

#endif
