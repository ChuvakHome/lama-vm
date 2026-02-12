#include "source_file_reader.hpp"
#include "source_file.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <memory>
#include <new>
#include <string_view>
#include <system_error>
#include <unordered_map>

namespace {
    constexpr std::align_val_t BYTEFILE_ALIGNMENT = std::align_val_t{alignof(lama::bytecode::bytefile_t)};

    lama::bytecode::bytefile_t* allocateBytefile(std::size_t bcFileSize) {
        lama::bytecode::bytefile_t bc;

        const std::ptrdiff_t stringtabSizeOffset = reinterpret_cast<std::byte *>(&(bc.stringtab_size)) - reinterpret_cast<std::byte *>(&bc);

        return reinterpret_cast<lama::bytecode::bytefile_t *>(
            ::new (BYTEFILE_ALIGNMENT, std::nothrow) std::byte[stringtabSizeOffset + bcFileSize]
        );
    }

    bool readNBytesFromFileStream(std::ifstream &ifs, void *data, std::size_t nbytes) {
        ifs.read(static_cast<char *>(data), nbytes);

        return ifs.gcount() == nbytes;
    }

    template<class T>
    bool readFromFileStream(std::ifstream &ifs, T &x) {
        return readNBytesFromFileStream(ifs, std::addressof(x), sizeof(T));
    }

    bool readPublicSymbolsTable(std::ifstream &ifs, lama::bytecode::bytefile_t *bytefile) {
        return readNBytesFromFileStream(
            ifs,
            bytefile->public_ptr,
            bytefile->public_symbols_number * sizeof(lama::bytecode::public_symbol_t)
        );
    }

    bool readStringTable(std::ifstream &ifs, lama::bytecode::bytefile_t *bytefile) {
        return readNBytesFromFileStream(
            ifs,
            bytefile->string_ptr,
            bytefile->stringtab_size
        );
    }

    std::size_t getCodeBufSize(const lama::bytecode::bytefile_t *bytefile, std::size_t fileSize) {
        std::ptrdiff_t codeOffset = bytefile->code_ptr - reinterpret_cast<const std::byte *>(&(bytefile->stringtab_size));

        return fileSize - codeOffset;
    }

    bool readCode(std::ifstream &ifs, lama::bytecode::bytefile_t *bytefile, std::size_t fileSize) {
        const std::size_t codeBufSize = getCodeBufSize(bytefile, fileSize);

        return readNBytesFromFileStream(
            ifs,
            bytefile->code_ptr,
            codeBufSize
        );
    }
}

std::string lama::bytecode::stringifyReadBytefileEror(lama::bytecode::ReadBytefileError err) {
    std::unordered_map<lama::bytecode::ReadBytefileError, std::string> errorStrings = {
        {lama::bytecode::ReadBytefileError::NonExistingFileError, "file does not exist"},
        {lama::bytecode::ReadBytefileError::NotRegularFileError, "not a regular file"},
        {lama::bytecode::ReadBytefileError::ReadFileError, "error while reading file"},
        {lama::bytecode::ReadBytefileError::OutOfMemoryError, "out of memory"},
        {lama::bytecode::ReadBytefileError::WrongBytecodeFileError, "wrong bytecode"},
        {lama::bytecode::ReadBytefileError::WrongStringTableSize, "wrong string table size"},
        {lama::bytecode::ReadBytefileError::WrongPublicSymbolsNumber, "wrong pulic symbols number"},
        {lama::bytecode::ReadBytefileError::WrongGlobalAreaSize, "wrong global area size"},
    };

    return errorStrings[err];
}

lama::bytecode::read_bytefile_result_t lama::bytecode::readBytefileFromFile(const std::string_view path) {
    std::filesystem::file_status stat = std::filesystem::status(path);

    switch (stat.type()) {
        case std::filesystem::file_type::not_found:
            return lama::bytecode::ReadBytefileError::NonExistingFileError;
        case std::filesystem::file_type::regular:
            break;
        default:
            return lama::bytecode::ReadBytefileError::NotRegularFileError;
    }

    std::error_code ec;
    const std::size_t fileSize = std::filesystem::file_size(path, ec);

    if (ec) {
        return lama::bytecode::ReadBytefileError::ReadFileError;
    }

    lama::bytecode::bytefile_t *bc = allocateBytefile(fileSize);

    if (bc == nullptr) {
        return lama::bytecode::ReadBytefileError::OutOfMemoryError;
    }

    std::ifstream fis{path.data(), std::ios::binary};

    #define ASSERTION(C, ERR) do { if (!(C)) { return ERR; } } while (false)

    ASSERTION(
        readFromFileStream(fis, bc->stringtab_size),
        lama::bytecode::ReadBytefileError::WrongBytecodeFileError
    );
    ASSERTION(
        bc->stringtab_size >= 0,
        lama::bytecode::ReadBytefileError::WrongStringTableSize
    );

    ASSERTION(
        readFromFileStream(fis, bc->global_area_size),
        lama::bytecode::ReadBytefileError::WrongBytecodeFileError
    );
    ASSERTION(
        bc->global_area_size >= 0,
        lama::bytecode::ReadBytefileError::WrongGlobalAreaSize
    );

    ASSERTION(
        readFromFileStream(fis, bc->public_symbols_number),
        lama::bytecode::ReadBytefileError::WrongBytecodeFileError
    );
    ASSERTION(
        bc->public_symbols_number >= 0,
        lama::bytecode::ReadBytefileError::WrongPublicSymbolsNumber
    );

    bc->public_ptr = reinterpret_cast<lama::bytecode::public_symbol_t *>(&(bc->buffer[0]));
    bc->string_ptr = reinterpret_cast<char *>(&(bc->public_ptr[bc->public_symbols_number]));
    bc->code_ptr = reinterpret_cast<std::byte *>(&(bc->string_ptr[bc->stringtab_size]));
    bc->global_ptr = nullptr;

    ASSERTION(
        readPublicSymbolsTable(fis, bc),
        lama::bytecode::ReadBytefileError::WrongBytecodeFileError
    );

    ASSERTION(
        readStringTable(fis, bc),
        lama::bytecode::ReadBytefileError::WrongBytecodeFileError
    );

    ASSERTION(
        readCode(fis, bc, fileSize),
        lama::bytecode::ReadBytefileError::WrongBytecodeFileError
    );

    return BytecodeFile{path, bc, getCodeBufSize(bc, fileSize)};
}
