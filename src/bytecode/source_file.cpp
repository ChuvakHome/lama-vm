#include "source_file.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <string_view>

namespace {
    std::int32_t findEntryPointOffset(const lama::bytecode::bytefile_t *bytefile) {
        for (std::int32_t i = 0; i < bytefile->public_symbols_number; ++i) {
            auto [namePtr, offset] = bytefile->public_ptr[i];

            std::string_view pubSymName = &bytefile->string_ptr[namePtr];

            if (pubSymName == lama::bytecode::ENTRYPOINT_NAME) {
                return offset;
            }
        }

        return -1;
    }

    constexpr std::align_val_t BYTEFILE_ALIGNMENT = std::align_val_t{alignof(lama::bytecode::bytefile_t)};
}

void lama::bytecode::detail::RawBytecodeFileDeleter::operator()(bytefile_t *bytefile) const {
    ::operator delete[](reinterpret_cast<std::byte *>(bytefile), BYTEFILE_ALIGNMENT, std::nothrow);
}

lama::bytecode::BytecodeFile::BytecodeFile(std::string_view path, lama::bytecode::bytefile_t *bytefile, std::size_t codeSize)
    : path_(path)
    , codeSize_(codeSize)
    , bytefile_(bytefile)
    , entryPointOffset_(findEntryPointOffset(bytefile)) {

}

std::string_view lama::bytecode::BytecodeFile::getFilePath() const {
    return path_;
}

std::byte lama::bytecode::BytecodeFile::getCodeByte(std::size_t offset) const {
    return bytefile_->code_ptr[offset];
}

std::size_t lama::bytecode::BytecodeFile::copyCodeBytes(std::byte *buffer, std::size_t offset, std::size_t nbytes) const {
    std::byte *last = std::copy_n(&(bytefile_->code_ptr[offset]), nbytes, buffer);

    return last - buffer;
}

std::size_t lama::bytecode::BytecodeFile::getCodeSize() const {
    return codeSize_;
}

std::uint32_t lama::bytecode::BytecodeFile::getStringTableSize() const {
    return bytefile_->stringtab_size;
}

std::string_view lama::bytecode::BytecodeFile::getString(std::size_t offset) const {
    return std::string_view{&(bytefile_->string_ptr[offset])};
}

std::int32_t lama::bytecode::BytecodeFile::getGlobal(std::size_t i) const {
    return bytefile_->global_ptr[i];
}

std::uint32_t lama::bytecode::BytecodeFile::getGlobalAreaSize() const {
    return bytefile_->global_area_size;
}

lama::bytecode::public_symbol_t lama::bytecode::BytecodeFile::getPublicSymbol(std::size_t i) const {
    return bytefile_->public_ptr[i];
}

std::string_view lama::bytecode::BytecodeFile::getPublicSymbolString(std::size_t i) const {
    return getString(getPublicSymbol(i).name);
}

std::uint32_t lama::bytecode::BytecodeFile::getPublicSymbolsNumber() const {
    return bytefile_->public_symbols_number;
}

std::int32_t lama::bytecode::BytecodeFile::getEntryPointOffset() const {
    return entryPointOffset_;
}
