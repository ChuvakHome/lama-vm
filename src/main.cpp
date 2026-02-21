#include <cstdint>
#include <cstdio>
#include <iostream>

#include "idiom/idiom_analyzer.hpp"
#include "bytecode/source_file.hpp"
#include "bytecode/source_file_reader.hpp"

#include "interpreter/interpreter.hpp"

extern "C" {
    int32_t disassemble_instruction(FILE *f, const lama::bytecode::bytefile_t *bf, uint32_t offset);
}

namespace {
    void printUsage(std::ostream &os) {
        os << "Usage: ./lama-interpreter [-s | -i] [bytecode-file]\n";
    }

    void printInstrSeq(const lama::bytecode::BytecodeFile *file, lama::idiom::idiom_record_t span) {
        const auto [offset, instrNum] = span;

        std::uint32_t ip = offset;

        for (std::size_t i = 0; i < instrNum; ++i) {
            std::cout << '\t';

            const std::int32_t curInstrLen = ::disassemble_instruction(stdout, file->getRawBytefile(), ip);
            ip += curInstrLen;

            std::cout << "; ";
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
    lama::interpreter::VerificationMode verMode = lama::interpreter::VerificationMode::DYNAMIC_VERIFICATION;

    std::size_t fileArgIndex = 1;

    while (fileArgIndex < argc) {
        const char * const arg = argv[fileArgIndex];

        if (arg[0] == '-') {
            if (arg[1] == 'i' && arg[2] == '\0') {
                mode = Mode::IDIOM_ANALYSIS_MODE;
            } else if (arg[1] == 's' && arg[2] == '\0') {
                verMode = lama::interpreter::VerificationMode::STATIC_VERIFICATION;
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
            lama::interpreter::interpretBytecodeFile(&bcf, verMode);
            break;
        case Mode::IDIOM_ANALYSIS_MODE:
            lama::idiom::processIdiomsFrequencies(&bcf, [&bcf](const lama::idiom::idiom_record_t &span, std::uint32_t freq){
                std::cout << freq;
                printInstrSeq(&bcf, span);
                std::cout << '\n';
            });
            break;
    }

    return 0;
}
