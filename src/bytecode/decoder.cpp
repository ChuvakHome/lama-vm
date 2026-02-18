#include "decoder.hpp"

#include <cstdint>
#include <cstdio>
#include <cinttypes>

#include <optional>

#include "bytecode_instructions.hpp"

std::optional<lama::bytecode::decoder::instruction_span_t> lama::bytecode::decoder::decodeInstruction(const lama::bytecode::BytecodeFile *file, offset_t offset) {
    const lama::bytecode::InstructionOpCode op = file->getInstruction(offset);

    std::size_t instrLen = sizeof(op);

    switch (op) {
        case InstructionOpCode::BINOP_ADD:
        case InstructionOpCode::BINOP_SUB:
        case InstructionOpCode::BINOP_MUL:
        case InstructionOpCode::BINOP_DIV:
        case InstructionOpCode::BINOP_MOD:
        case InstructionOpCode::BINOP_LT:
        case InstructionOpCode::BINOP_LE:
        case InstructionOpCode::BINOP_GT:
        case InstructionOpCode::BINOP_GE:
        case InstructionOpCode::BINOP_EQ:
        case InstructionOpCode::BINOP_NE:
        case InstructionOpCode::BINOP_AND:
        case InstructionOpCode::BINOP_OR:
            break;
        case InstructionOpCode::CONST:
        case InstructionOpCode::STRING:
            instrLen += sizeof(std::int32_t);
            break;
        case InstructionOpCode::SEXP:
            instrLen += sizeof(std::int32_t) * 2;
            break;
        case InstructionOpCode::STI:
        case InstructionOpCode::STA:
            break;
        case InstructionOpCode::JMP:
            instrLen += sizeof(std::int32_t);
            break;
        case InstructionOpCode::END:
        case InstructionOpCode::RET:
        case InstructionOpCode::DROP:
        case InstructionOpCode::DUP:
        case InstructionOpCode::SWAP:
        case InstructionOpCode::ELEM:
            break;
        case InstructionOpCode::LD_G:
        case InstructionOpCode::LD_L:
        case InstructionOpCode::LD_A:
        case InstructionOpCode::LD_C:
        case InstructionOpCode::LDA_G:
        case InstructionOpCode::LDA_L:
        case InstructionOpCode::LDA_A:
        case InstructionOpCode::LDA_C:
        case InstructionOpCode::ST_G:
        case InstructionOpCode::ST_L:
        case InstructionOpCode::ST_A:
        case InstructionOpCode::ST_C:
        case InstructionOpCode::CJMPZ:
        case InstructionOpCode::CJMPNZ:
            instrLen += sizeof(std::int32_t);
            break;
        case InstructionOpCode::BEGIN:
        case InstructionOpCode::CBEGIN:
            instrLen += sizeof(std::int32_t) * 2;
            break;
        case InstructionOpCode::CLOSURE:
            instrLen += sizeof(std::int32_t) * 2;

            std::int32_t n;
            file->copyCodeBytes(reinterpret_cast<std::byte *>(&n), offset + sizeof(std::int32_t), sizeof(n));

            instrLen += (sizeof(std::byte) + sizeof(std::int32_t)) * n;
            break;
        case InstructionOpCode::CALLC:
            instrLen += sizeof(std::int32_t);
            break;
        case InstructionOpCode::CALL:
        case InstructionOpCode::TAG:
            instrLen += sizeof(std::int32_t) * 2;
            break;
        case InstructionOpCode::ARRAY:
            instrLen += sizeof(std::int32_t);
            break;
        case InstructionOpCode::FAIL:
            instrLen += sizeof(std::int32_t) * 2;
            break;
        case InstructionOpCode::LINE:
            instrLen += sizeof(std::int32_t);
            break;
        case InstructionOpCode::PATT_STR:
        case InstructionOpCode::PATT_STRING:
        case InstructionOpCode::PATT_ARRAY:
        case InstructionOpCode::PATT_SEXP:
        case InstructionOpCode::PATT_REF:
        case InstructionOpCode::PATT_VAL:
        case InstructionOpCode::PATT_FUN:
        case InstructionOpCode::CALL_LREAD:
        case InstructionOpCode::CALL_LWRITE:
        case InstructionOpCode::CALL_LLENGTH:
        case InstructionOpCode::CALL_LSTRING:
            break;
        case InstructionOpCode::CALL_BARRAY:
            instrLen += sizeof(std::int32_t);
            break;
        default:
            return std::nullopt;
    }

    return {{offset, instrLen}};
}

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

namespace {
    unsigned char toByte(lama::bytecode::InstructionOpCode op) {
        return static_cast<unsigned char>(op);
    }
}

void lama::bytecode::decoder::printInstruction(::FILE *f, const lama::bytecode::BytecodeFile *file, offset_t offset) {
    const lama::bytecode::InstructionOpCode op = file->getInstruction(offset);
    const unsigned char opcodeByte = toByte(op);

    #define ADDR_SPEC "0x%08" PRIx32

    static const char * const binopCodes[] = {
        "+", "-", "*", "/", "%",
        "<", "<=", ">", ">=", "==", "!=",
        "&&", "!!",
    };

    static const char * const valueTypes[] = {
        "G", "L", "A", "C"
    };

    static const char * const cjmpCodes[] = {
        "CJMPz", "CJMPnz",
    };

    static const char * const beginCodes[] = {
        "BEGIN", "CBEGIN",
    };

    static const char * const patternCodes[] = {
        "=str", "#string", "#array", "#sexp",
        "#ref", "#val", "#fun",
    };

    static const char * const builtinFunCodes[] = {
        "Lread", "Lwrite", "Llength", "Lstring"
    };

    auto i8arg = [&offset, &file]() mutable {
        std::int8_t val;
        val = static_cast<std::int8_t>(file->getCodeByte(offset));
        offset += sizeof(val);

        return val;
    };

    auto i32arg = [&offset, &file]() mutable {
        std::int32_t val;
        file->copyCodeBytes(reinterpret_cast<std::byte *>(&val), offset, sizeof(val));
        offset += sizeof(val);

        return val;
    };

    fprintf(f, ADDR_SPEC ": ", offset);
    offset += sizeof(op);

    switch (op) {
        case InstructionOpCode::BINOP_ADD:
        case InstructionOpCode::BINOP_SUB:
        case InstructionOpCode::BINOP_MUL:
        case InstructionOpCode::BINOP_DIV:
        case InstructionOpCode::BINOP_MOD:
        case InstructionOpCode::BINOP_LT:
        case InstructionOpCode::BINOP_LE:
        case InstructionOpCode::BINOP_GT:
        case InstructionOpCode::BINOP_GE:
        case InstructionOpCode::BINOP_EQ:
        case InstructionOpCode::BINOP_NE:
        case InstructionOpCode::BINOP_AND:
        case InstructionOpCode::BINOP_OR:
            fprintf(f, "BINOP\t%s", binopCodes[opcodeByte - 0x1]);
            break;
        case InstructionOpCode::CONST:
            fprintf(f, "CONST\t%" PRId32, i32arg());
            break;
        case InstructionOpCode::STRING:
            fprintf(f, "STRING\t\"%s\"", file->getString(i32arg()).data());
            break;
        case InstructionOpCode::SEXP:
            fprintf(f, "SEXP\t%s\t%" PRId32,
                file->getString(i32arg()).data(),
                i32arg()
            );
            break;
        case InstructionOpCode::STI:
            fprintf(f, "STI");
            break;
        case InstructionOpCode::STA:
            fprintf(f, "STA");
            break;
        case InstructionOpCode::JMP:
            fprintf(f, "JMP\t" ADDR_SPEC, i32arg());
            break;
        case InstructionOpCode::END:
            fprintf(f, "END");
            break;
        case InstructionOpCode::RET:
            fprintf(f, "RET");
            break;
        case InstructionOpCode::DROP:
            fprintf(f, "DROP");
            break;
        case InstructionOpCode::DUP:
            fprintf(f, "DUP");
            break;
        case InstructionOpCode::SWAP:
            fprintf(f, "SWAP");
            break;
        case InstructionOpCode::ELEM:
            fprintf(f, "ELEM");
            break;
        case InstructionOpCode::LD_G:
        case InstructionOpCode::LD_L:
        case InstructionOpCode::LD_A:
        case InstructionOpCode::LD_C:
            fprintf(f, "LD\t%s(%" PRId32 ")",
                valueTypes[opcodeByte - toByte(InstructionOpCode::LD_G)],
                i32arg()
            );
            break;
        case InstructionOpCode::LDA_G:
        case InstructionOpCode::LDA_L:
        case InstructionOpCode::LDA_A:
        case InstructionOpCode::LDA_C:
            fprintf(f, "LDA\t%s(%" PRId32 ")",
                valueTypes[opcodeByte - toByte(InstructionOpCode::LDA_G)],
                i32arg()
            );
            break;
        case InstructionOpCode::ST_G:
        case InstructionOpCode::ST_L:
        case InstructionOpCode::ST_A:
        case InstructionOpCode::ST_C:
            fprintf(f, "ST\t%s(%" PRId32 ")",
                valueTypes[opcodeByte - toByte(InstructionOpCode::ST_G)],
                i32arg()
            );
            break;
        case InstructionOpCode::CJMPZ:
        case InstructionOpCode::CJMPNZ:
            fprintf(f, "%s\t" ADDR_SPEC,
                cjmpCodes[opcodeByte - toByte(lama::bytecode::InstructionOpCode::CJMPZ)],
                i32arg()
            );
            break;
        case InstructionOpCode::BEGIN:
        case InstructionOpCode::CBEGIN:
            fprintf(f, "%s\t%" PRId32 "\t%" PRId32,
                beginCodes[opcodeByte - toByte(lama::bytecode::InstructionOpCode::BEGIN)],
                i32arg(), i32arg()
            );
            break;
        case InstructionOpCode::CLOSURE: {
            const std::int32_t l = i32arg();
            const std::int32_t n = i32arg();

            fprintf(f, "CLOSURE\t" ADDR_SPEC "\t%" PRId32,
                l, n
            );

            for (std::int32_t i = 0; i < n; ++i) {
                const std::int8_t varType = i8arg();
                const std::int32_t varIndex = i32arg();

                fprintf(f, "\t%s(%" PRId32 ")",
                    valueTypes[varType],
                    varIndex
                );
            }
            break;
        }
        case InstructionOpCode::CALLC:
            fprintf(f, "CALLC\t%" PRId32, i32arg());
            break;
        case InstructionOpCode::CALL:
            fprintf(f, "CALL\t" ADDR_SPEC "\t%" PRId32, i32arg(), i32arg());
            break;
        case InstructionOpCode::TAG:
            fprintf(f, "TAG\t%s\t%" PRId32,
                file->getString(i32arg()).data(),
                i32arg()
            );
            break;
        case InstructionOpCode::ARRAY:
            fprintf(f, "ARRAY\t%" PRId32, i32arg());
            break;
        case InstructionOpCode::FAIL:
            fprintf(f, "FAIL\t%" PRId32 "\t%" PRId32, i32arg(), i32arg());
            break;
        case InstructionOpCode::LINE:
            fprintf(f, "LINE\t%" PRId32, i32arg());
            break;
        case InstructionOpCode::PATT_STR:
        case InstructionOpCode::PATT_STRING:
        case InstructionOpCode::PATT_ARRAY:
        case InstructionOpCode::PATT_SEXP:
        case InstructionOpCode::PATT_REF:
        case InstructionOpCode::PATT_VAL:
        case InstructionOpCode::PATT_FUN:
            fprintf(f, "PATT\t%s",
                patternCodes[opcodeByte - toByte(InstructionOpCode::PATT_STR)]
            );
            break;
        case InstructionOpCode::CALL_LREAD:
        case InstructionOpCode::CALL_LWRITE:
        case InstructionOpCode::CALL_LLENGTH:
        case InstructionOpCode::CALL_LSTRING:
            fprintf(f, "CALL\t%s",
                builtinFunCodes[opcodeByte - toByte(InstructionOpCode::CALL_LREAD)]
            );
            break;
        case InstructionOpCode::CALL_BARRAY:
            fprintf(f, "CALL\tBarray\t%" PRId32, i32arg());
            break;
    }

    fprintf(f, "; ");
}
