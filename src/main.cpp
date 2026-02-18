#include <cstdio>
#include <iostream>

#include "idiom/idiom_analyzer.hpp"
#include "bytecode/source_file.hpp"
#include "bytecode/source_file_reader.hpp"

#include "interpreter/interpreter.hpp"

namespace {
    void printUsage(std::ostream &os) {
        os << "Usage: ./lama-interpreter [bytecode-file]\n";
    }

    using instr_span_t = lama::bytecode::decoder::instruction_span_t;

    void printInstrSeq(const lama::bytecode::BytecodeFile *file, instr_span_t span) {
        const auto [offset, instrLen] = span;

        std::size_t ip = offset;

        while (ip < offset + instrLen) {
            const std::size_t curInstrLen = lama::bytecode::decoder::decodeInstruction(file, ip).value().second;
            lama::bytecode::decoder::printInstruction(stdout, file, ip);

            ip += curInstrLen;
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Too few arguments\n";
        printUsage(std::cerr);

        return -1;
    } else if (argc > 3) {
        std::cerr << "Too many arguments\n";
        printUsage(std::cerr);

        return -2;
    }

    enum class Mode {
        INTERPRETER_MODE,
        IDIOM_ANALYSIS_MODE,
    };

    Mode mode = Mode::INTERPRETER_MODE;
    std::size_t fileArgIndex = 1;

    while (fileArgIndex < argc) {
        const char * const arg = argv[fileArgIndex];

        if (arg[0] == '-') {
            if (arg[1] == 'i' && arg[2] == '\0') {
                mode = Mode::IDIOM_ANALYSIS_MODE;
            } else {
                std::cerr << "Unknown option: " << arg << '\n';
                printUsage(std::cerr);

                return -3;
            }
        } else {
            break;
        }

        ++fileArgIndex;
    }

    if (fileArgIndex >= argc) {
        std::cerr << "Input bytecode file is not specified\n";
        printUsage(std::cerr);

        return -4;
    }

    std::string_view inputFile = argv[fileArgIndex];
    auto result = lama::bytecode::readBytefileFromFile(inputFile);

    if (result.hasError()) {
        std::cerr << inputFile << ": " << lama::bytecode::stringifyReadBytefileEror(result.getError()) << '\n';
        printUsage(std::cerr);

        return static_cast<int>(result.getError());
    }

    const lama::bytecode::BytecodeFile& bcf = result.getResult();

    switch (mode) {
        case Mode::INTERPRETER_MODE:
            lama::interpreter::interpretBytecodeFile(&bcf);
            break;
        case Mode::IDIOM_ANALYSIS_MODE:
            lama::idiom::processIdiomsFrequencies(&bcf, [&bcf](const instr_span_t &span, std::uint32_t freq){
                std::cout << freq << '\t';
                printInstrSeq(&bcf, span);
                std::cout << '\n';
            });
            break;
    }

    return 0;
}
