#ifndef BYTECODE_SOURCE_FILE_HPP
#define BYTECODE_SOURCE_FILE_HPP

#include "bytecode_instructions.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace lama::bytecode {
typedef struct {
    std::uint32_t name;
    std::uint32_t offset;
} public_symbol_t;

typedef struct {
    char *string_ptr;                    /* Pointer to the beginning of the string table */
    public_symbol_t *public_ptr;         /* Pointer to the beginning of publics table    */
    std::byte *code_ptr;                 /* Pointer to the bytecode itself               */
    std::int32_t *global_ptr;            /* Pointer to the global area                   */
    std::int32_t stringtab_size;        /* Size (in bytes) of the string table          */
    std::int32_t global_area_size;      /* Size (in words) of global area               */
    std::int32_t public_symbols_number; /* Number of public symbols                     */
    std::byte buffer[0];
} bytefile_t;

#ifndef LAMA_ENTRYPOINT_NAME
#define LAMA_ENTRYPOINT_NAME "main"
#endif

constexpr std::string_view ENTRYPOINT_NAME = LAMA_ENTRYPOINT_NAME;

namespace detail {
struct RawBytecodeFileDeleter {
    void operator()(bytefile_t *bytefile) const;
};
}

using offset_t = std::uint32_t;

class BytecodeFile {
public:
    BytecodeFile(std::string_view path, bytefile_t *bytefile, std::size_t codeSize);
    BytecodeFile(const BytecodeFile &other) = delete;
    BytecodeFile(BytecodeFile&& other) = default;
    ~BytecodeFile() = default;

    std::string_view getFilePath() const;

    std::size_t getCodeSize() const;
    const std::byte& getCodeByte(offset_t offset) const;
    lama::bytecode::InstructionOpCode getInstruction(offset_t offset) const;
    std::size_t copyCodeBytes(std::byte *buffer, offset_t offset, std::size_t nbytes) const;

    std::uint32_t getStringTableSize() const;
    std::string_view getString(offset_t offset) const;

    std::int32_t getGlobal(std::size_t i) const;
    std::uint32_t getGlobalAreaSize() const;

    public_symbol_t getPublicSymbol(std::size_t i) const;
    std::string_view getPublicSymbolString(std::size_t i) const;
    std::uint32_t getPublicSymbolsNumber() const;

    std::int32_t getEntryPointOffset() const;

    const bytefile_t* getRawBytefile() const;
private:
    const std::string_view path_;
    const std::size_t codeSize_;
    const std::int32_t entryPointOffset_;
    std::unique_ptr<bytefile_t, detail::RawBytecodeFileDeleter> bytefile_;
};
}

#endif
