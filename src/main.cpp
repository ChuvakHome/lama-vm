#include <iostream>

#include "bytecode/source_file.hpp"
#include "bytecode/source_file_reader.hpp"

#include "interpreter/interpreter.hpp"

namespace {
    void printUsage(std::ostream &os) {
        os << "Usage: ./lama-interpreter [bytecode-file]\n";
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printUsage(std::cerr);

        return -1;
    }

    auto result = lama::bytecode::readBytefileFromFile(argv[1]);

    if (result.hasError()) {
        std::cerr << argv[1] << ": " << lama::bytecode::stringifyReadBytefileEror(result.getError()) << '\n';

        printUsage(std::cerr);

        return static_cast<int>(result.getError());
    }

    const lama::bytecode::BytecodeFile& bcf = result.getResult();

    lama::interpreter::BytecodeInterpreter interpreter{&bcf};
    interpreter.initializeGc();
    interpreter.runInterpreterLoop();
    interpreter.deinitializeGc();

    return 0;
}
