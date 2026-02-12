#ifndef BYTECODE_SOURCE_FILE_READER_HPP
#define BYTECODE_SOURCE_FILE_READER_HPP

#include "source_file.hpp"

#include "../utils/result.hpp"

namespace lama::bytecode {
enum class ReadBytefileError {
    NonExistingFileError = 1,
    NotRegularFileError,
    ReadFileError,
    WrongBytecodeFileError,
    WrongStringTableSize,
    WrongPublicSymbolsNumber,
    WrongGlobalAreaSize,
    OutOfMemoryError,
};

std::string stringifyReadBytefileEror(ReadBytefileError err);

using read_bytefile_result_t = utils::Result<BytecodeFile, ReadBytefileError>;

read_bytefile_result_t readBytefileFromFile(const std::string_view path);
}

#endif
