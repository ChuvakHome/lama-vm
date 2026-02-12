#ifndef BYTECODE_BYTECODE_INSTRUCTIONS_HPP
#define BYTECODE_BYTECODE_INSTRUCTIONS_HPP

namespace lama::bytecode {
enum class InstructionOpCode : unsigned char {
    BINOP_ADD = 0x01,
    BINOP_SUB = 0x02,
    BINOP_MUL = 0x03,
    BINOP_DIV = 0x04,
    BINOP_MOD = 0x05,

    BINOP_LT = 0x06,
    BINOP_LE = 0x07,
    BINOP_GT = 0x08,
    BINOP_GE = 0x09,

    BINOP_EQ = 0x0a,
    BINOP_NE = 0x0b,

    BINOP_AND = 0x0c,
    BINOP_OR = 0x0d,

    CONST = 0x10,
    STRING = 0x11,
    SEXP = 0x12,
    STI = 0x13,
    STA = 0x14,

    JMP = 0x15,
    END = 0x16,

    RET = 0x17,
    DROP = 0x18,
    DUP = 0x19,
    SWAP = 0x1a,
    ELEM = 0x1b,

    LD_G = 0x20,
    LD_L = 0x21,
    LD_A = 0x22,
    LD_C = 0x23,

    LDA_G = 0x30,
    LDA_L = 0x31,
    LDA_A = 0x32,
    LDA_C = 0x33,

    ST_G = 0x40,
    ST_L = 0x41,
    ST_A = 0x42,
    ST_C = 0x43,

    CJMPZ = 0x50,
    CJMPNZ = 0x51,

    BEGIN = 0x52,
    CBEGIN = 0x53,

    CLOSURE = 0x54,

    CALLC = 0x55,
    CALL = 0x56,

    TAG = 0x57,
    ARRAY = 0x58,
    FAIL = 0x59,
    LINE = 0x5a,

    PATT_STR = 0x60,
    PATT_STRING = 0x61,
    PATT_ARRAY = 0x62,
    PATT_SEXP = 0x63,
    PATT_REF = 0x64,
    PATT_VAL = 0x65,
    PATT_FUN = 0x66,

    CALL_LREAD = 0x70,
    CALL_LWRITE = 0x71,
    CALL_LLENGTH = 0x72,
    CALL_LSTRING = 0x73,
    CALL_BARRAY = 0x74,
};
}

#endif
