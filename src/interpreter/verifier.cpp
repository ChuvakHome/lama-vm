#include "verifier.hpp"

#include <cstdint>

#include "lama_runtime.hpp"

namespace {
    enum class CaptureType : unsigned char {
        GLOBAL = 0x0,
        LOCAL = 0x1,
        ARGUMENT = 0x2,
        CAPTURE = 0x3,
    };
}

lama::verifier::BytecodeVerifier::BytecodeVerifier(const lama::bytecode::BytecodeFile *bytecodeFile)
    : ip_(0)
    , instructionStartOffset_(0)
    , currentState_({
        /* argsCount = */ lama::runtime::MAIN_FUNCTION_ARGUMENTS,
        /* startIp = */ static_cast<lama::bytecode::offset_t>(bytecodeFile->getEntryPointOffset()),
        /* localsCount = */ 0,
        /* stackSize = */ 0,
        /* callstackSize = */ 1
    })
    , stackSizes_(bytecodeFile->getCodeSize())
    , pushNextState_(true)
    , bytecodeFile_(bytecodeFile) {
    worklist_.push_back(currentState_);
}

void lama::verifier::BytecodeVerifier::verifyBinop() {
    popWords(2);
    pushWord();
}

void lama::verifier::BytecodeVerifier::verifyConst() {
    fetchInt32();

    pushWord();
}

void lama::verifier::BytecodeVerifier::verifyString() {
    const std::uint32_t stringIndex = fetchInt32();
    checkStringIndex(stringIndex);

    pushWord();
}

void lama::verifier::BytecodeVerifier::verifySexp() {
    const std::uint32_t stringIndex = fetchInt32();
    checkStringIndex(stringIndex);

    const std::uint32_t n = fetchInt32();
    checkNonNegative(n, "sexp members count must not be negative");

    popWords(n);
    pushWord();
}

void lama::verifier::BytecodeVerifier::verifySti() {
    popWords(2);
    pushWord();
}

void lama::verifier::BytecodeVerifier::verifyJmp() {
    const std::uint32_t newIp = fetchInt32();
    checkCodeOffset(newIp);

    setIp(newIp);

    pushState({
        /* argsCount = */ currentState_.argsCount,
        /* startIp = */ newIp,
        /* localsCount = */ currentState_.localsCount,
        /* stackSize = */ currentState_.stackSize,
        /* callstackSize = */ currentState_.callstackSize
    });

    pushNextState_ = false;
}

void lama::verifier::BytecodeVerifier::verifyReturn() {
    popFrame();

    pushNextState_ = false;
}

void lama::verifier::BytecodeVerifier::verifyDrop() {
    popWord();
}

void lama::verifier::BytecodeVerifier::verifyDup() {
    popWord();
    pushWords(2);
}

void lama::verifier::BytecodeVerifier::verifySwap() {
    popWords(2);
    pushWords(2);
}

void lama::verifier::BytecodeVerifier::verifyElem() {
    popWords(2);
    pushWord();
}

void lama::verifier::BytecodeVerifier::verifyGlobalLoad() {
    const lama::bytecode::offset_t globalValueIndex = fetchInt32();
    checkGlobalValueIndex(globalValueIndex);

    pushWord();
}

void lama::verifier::BytecodeVerifier::verifyLocalLoad() {
    const lama::bytecode::offset_t localValueIndex = fetchInt32();
    checkLocalValueIndex(currentState_, localValueIndex);

    pushWord();
}

void lama::verifier::BytecodeVerifier::verifyArgumentLoad() {
    const lama::bytecode::offset_t argValueIndex = fetchInt32();
    checkArgumentValueIndex(currentState_, argValueIndex);

    pushWord();
}

void lama::verifier::BytecodeVerifier::verifyCapturedLoad() {
    const lama::bytecode::offset_t capturedValueIndex = fetchInt32();
    checkCapturedValueIndex(capturedValueIndex);

    pushWord();
}

void lama::verifier::BytecodeVerifier::verifyGlobalStore() {
    const lama::bytecode::offset_t globalValueIndex = fetchInt32();
    checkGlobalValueIndex(globalValueIndex);

    popWord();
    pushWord();
}

void lama::verifier::BytecodeVerifier::verifyLocalStore() {
    const lama::bytecode::offset_t localValueIndex = fetchInt32();
    checkLocalValueIndex(currentState_, localValueIndex);

    popWord();
    pushWord();
}

void lama::verifier::BytecodeVerifier::verifyArgumentStore() {
    const lama::bytecode::offset_t argValueIndex = fetchInt32();
    checkArgumentValueIndex(currentState_, argValueIndex);

    popWord();
    pushWord();
}

void lama::verifier::BytecodeVerifier::verifyCapturedStore() {
    const lama::bytecode::offset_t capturedValueIndex = fetchInt32();
    checkCapturedValueIndex(capturedValueIndex);

    popWord();
    pushWord();
}

void lama::verifier::BytecodeVerifier::verifyConditionalJmp() {
    const lama::bytecode::offset_t newIp = fetchInt32();
    checkCodeOffset(newIp);

    popWord();

    pushState({
        /* argsCount = */ currentState_.argsCount,
        /* startIp = */ newIp,
        /* localsCount = */ currentState_.localsCount,
        /* stackSize = */ currentState_.stackSize,
        /* callstackSize = */ currentState_.callstackSize
    });

    pushState({
        /* argsCount = */ currentState_.argsCount,
        /* startIp = */ getIp(),
        /* localsCount = */ currentState_.localsCount,
        /* stackSize = */ currentState_.stackSize,
        /* callstackSize = */ currentState_.callstackSize
    });

    pushNextState_ = false;
}

void lama::verifier::BytecodeVerifier::verifyBegin() {
    const std::int32_t argsNum = fetchInt32();
    checkArgumentsNumber(argsNum);
    verifierAssert(argsNum == currentState_.argsCount, "the number of passed arguments differs from the number declared in BEGIN");

    const std::int32_t localsNum = fetchInt32();
    checkLocalsNumber(localsNum);

    currentState_.localsCount = localsNum;
}

void lama::verifier::BytecodeVerifier::verifyClosureBegin() {
    const std::int32_t argsNum = fetchInt32();
    checkArgumentsNumber(argsNum);
    verifierAssert(argsNum == currentState_.argsCount, "the number of passed arguments differs from the number declared in CBEGIN");

    const std::int32_t localsNum = fetchInt32();
    checkLocalsNumber(localsNum);

    currentState_.localsCount = localsNum;
}

void lama::verifier::BytecodeVerifier::verifyClosure() {
    const lama::bytecode::offset_t locationAddress = fetchInt32();
    checkCodeOffset(locationAddress);

    const lama::bytecode::InstructionOpCode locationOp = lookupInstrOpCode(locationAddress);
    verifierAssert(
        locationOp == lama::bytecode::InstructionOpCode::BEGIN || locationOp == lama::bytecode::InstructionOpCode::CBEGIN,
        "closure function should start with BEGIN or CBEGIN instruction"
    );

    const std::int32_t argsNum = fetchInt32();
    checkArgumentsNumber(argsNum);

    for (std::int32_t i = 0; i < argsNum; ++i) {
        CaptureType captureType{std::to_integer<unsigned char>(fetchByte())};
        const std::int32_t index = fetchInt32();

        switch (captureType) {
            case CaptureType::GLOBAL:
                checkGlobalValueIndex(index);
                break;
            case CaptureType::LOCAL:
                checkLocalValueIndex(currentState_, index);
                break;
            case CaptureType::ARGUMENT:
                checkArgumentValueIndex(currentState_, index);
                break;
            case CaptureType::CAPTURE:
                checkCapturedValueIndex(index);
                break;
            default:
                verifierAssert(false, "invalid varspec");
                break;
        }
    }

    pushWord();
}

void lama::verifier::BytecodeVerifier::verifyCallClosure() {
    const std::int32_t argsNum = fetchInt32();
    checkArgumentsNumber(argsNum);

    popWords(argsNum + 1); // args + closure object
    pushWord();
}

void lama::verifier::BytecodeVerifier::verifyCall() {
    const lama::bytecode::offset_t locationAddress = fetchInt32();
    checkCodeOffset(locationAddress);

    const std::int32_t argsNum = fetchInt32();
    checkArgumentsNumber(argsNum);

    pushState({
        /* argsCount = */ static_cast<std::size_t>(argsNum),
        /* startIp = */ locationAddress,
        /* localsCount = */ 0,
        /* stackSize = */ 0,
        /* callstackSize = */ currentState_.callstackSize + 1,
    });
    pushState({
        /* argsCount = */ currentState_.argsCount,
        /* startIp = */ getIp(),
        /* localsCount = */ currentState_.localsCount,
        /* stackSize = */ currentState_.stackSize - argsNum + 1,
        /* callstackSize = */ currentState_.callstackSize
    });

    pushNextState_ = false;
}

void lama::verifier::BytecodeVerifier::verifyTag() {
    const std::uint32_t s = fetchInt32();
    checkStringIndex(s);

    const std::int32_t n = fetchInt32();
    checkNonNegative(n, "sexp members count must not be negative");

    popWord();
    pushWord();
}

void lama::verifier::BytecodeVerifier::verifyArray() {
    const std::int32_t n = fetchInt32();
    checkNonNegative(n, "array length must not be negative");

    popWord();
    pushWord();
}

void lama::verifier::BytecodeVerifier::verifyFail() {
    const std::int32_t lineNumber = fetchInt32();
    const std::int32_t colNumber = fetchInt32();

    checkMin(lineNumber, 1, "line number should be greater than 0");
    checkMin(colNumber, 1, "column number should be greater than 0");

    pushNextState_ = false;
}

void lama::verifier::BytecodeVerifier::verifyLine() {
    fetchInt32();
}

void lama::verifier::BytecodeVerifier::verifyPattStr() {
    popWords(2);
    pushWord();
}

void lama::verifier::BytecodeVerifier::verifyPatt() {
    popWord();
    pushWord();
}

void lama::verifier::BytecodeVerifier::verifyCallLread() {
    pushWord();
}

void lama::verifier::BytecodeVerifier::verifyCallLwrite() {
    popWord();
    pushWord();
}

void lama::verifier::BytecodeVerifier::verifyCallLlength() {
    popWord();
    pushWord();
}

void lama::verifier::BytecodeVerifier::verifyCallLstring() {
    popWord();
    pushWord();
}

void lama::verifier::BytecodeVerifier::verifyCallBarray() {
    const std::int32_t n = fetchInt32();
    checkNonNegative(n, "array length must not be negative");

    popWords(n);
    pushWord();
}

bool lama::verifier::BytecodeVerifier::verifyBytecode() {
    while (!worklist_.empty()) {
        if (!verifyInstruction()) {
            return false;
        }
    }

    return true;
}

bool lama::verifier::BytecodeVerifier::verifyInstruction() {
    currentState_ = popState();

    setIp(currentState_.startIp);
    setInstructionStartOffset(getIp());

    if (stackSizes_.at(getInstructionStartOffset()).isDefined()) {
        verifierAssert(
            stackSizes_.at(getInstructionStartOffset()).getStackSize() == currentState_.stackSize,
            "stack size inconsistency"
        );

        return true;
    } else {
        stackSizes_[getInstructionStartOffset()] = currentState_.stackSize;
    }

    pushNextState_ = true;

    const lama::bytecode::InstructionOpCode op = fetchInstrOpCode();

    switch (op) {
        case bytecode::InstructionOpCode::BINOP_ADD:
        case bytecode::InstructionOpCode::BINOP_SUB:
        case bytecode::InstructionOpCode::BINOP_MUL:
        case bytecode::InstructionOpCode::BINOP_DIV:
        case bytecode::InstructionOpCode::BINOP_MOD:
        case bytecode::InstructionOpCode::BINOP_LT:
        case bytecode::InstructionOpCode::BINOP_LE:
        case bytecode::InstructionOpCode::BINOP_GT:
        case bytecode::InstructionOpCode::BINOP_GE:
        case bytecode::InstructionOpCode::BINOP_EQ:
        case bytecode::InstructionOpCode::BINOP_NE:
        case bytecode::InstructionOpCode::BINOP_AND:
        case bytecode::InstructionOpCode::BINOP_OR:
            verifyBinop();
            break;
        case bytecode::InstructionOpCode::CONST:
            verifyConst();
            break;
        case bytecode::InstructionOpCode::STRING:
            verifyString();
            break;
        case bytecode::InstructionOpCode::SEXP:
            verifySexp();
            break;
        case bytecode::InstructionOpCode::STI:
            verifySti();
            break;
        case bytecode::InstructionOpCode::STA:
            return false;  // indicates that verifier can't completely verify the bytecode
        case bytecode::InstructionOpCode::JMP:
            verifyJmp();
            break;
        case bytecode::InstructionOpCode::END:
        case bytecode::InstructionOpCode::RET:
            verifyReturn();
            break;
        case bytecode::InstructionOpCode::DROP:
            verifyDrop();
            break;
        case bytecode::InstructionOpCode::DUP:
            verifyDup();
            break;
        case bytecode::InstructionOpCode::SWAP:
            verifySwap();
            break;
        case bytecode::InstructionOpCode::ELEM:
            verifyElem();
            break;
        case bytecode::InstructionOpCode::LD_G:
        case bytecode::InstructionOpCode::LDA_G:
            verifyGlobalLoad();
            break;
        case bytecode::InstructionOpCode::LD_L:
        case bytecode::InstructionOpCode::LDA_L:
            verifyLocalLoad();
            break;
        case bytecode::InstructionOpCode::LD_A:
        case bytecode::InstructionOpCode::LDA_A:
            verifyArgumentLoad();
            break;
        case bytecode::InstructionOpCode::LD_C:
        case bytecode::InstructionOpCode::LDA_C:
            verifyCapturedLoad();
            break;
        case bytecode::InstructionOpCode::ST_G:
            verifyGlobalStore();
            break;
        case bytecode::InstructionOpCode::ST_L:
            verifyLocalStore();
            break;
        case bytecode::InstructionOpCode::ST_A:
            verifyArgumentStore();
            break;
        case bytecode::InstructionOpCode::ST_C:
            verifyCapturedStore();
            break;
        case bytecode::InstructionOpCode::CJMPZ:
        case bytecode::InstructionOpCode::CJMPNZ:
            verifyConditionalJmp();
            break;
        case bytecode::InstructionOpCode::BEGIN:
            verifyBegin();
            break;
        case bytecode::InstructionOpCode::CBEGIN:
            verifyClosureBegin();
            break;
        case bytecode::InstructionOpCode::CLOSURE:
            verifyClosure();
            break;
        case bytecode::InstructionOpCode::CALLC:
            verifyCallClosure();
            break;
        case bytecode::InstructionOpCode::CALL:
            verifyCall();
            break;
        case bytecode::InstructionOpCode::TAG:
            verifyTag();
            break;
        case bytecode::InstructionOpCode::ARRAY:
            verifyArray();
            break;
        case bytecode::InstructionOpCode::FAIL:
            verifyFail();
            break;
        case bytecode::InstructionOpCode::LINE:
            verifyLine();
            break;
        case bytecode::InstructionOpCode::PATT_STR:
            verifyPattStr();
            break;
        case bytecode::InstructionOpCode::PATT_STRING:
        case bytecode::InstructionOpCode::PATT_ARRAY:
        case bytecode::InstructionOpCode::PATT_SEXP:
        case bytecode::InstructionOpCode::PATT_REF:
        case bytecode::InstructionOpCode::PATT_VAL:
        case bytecode::InstructionOpCode::PATT_FUN:
            verifyPatt();
            break;
        case bytecode::InstructionOpCode::CALL_LREAD:
            verifyCallLread();
            break;
        case bytecode::InstructionOpCode::CALL_LWRITE:
            verifyCallLwrite();
            break;
        case bytecode::InstructionOpCode::CALL_LLENGTH:
            verifyCallLlength();
            break;
        case bytecode::InstructionOpCode::CALL_LSTRING:
            verifyCallLstring();
            break;
        case bytecode::InstructionOpCode::CALL_BARRAY:
            verifyCallBarray();
            break;
        default:
            verifierAssert(false, "invalid instruction");
            break;
    }

    if (pushNextState_) {
        pushState({
            /* argsCount = */ currentState_.argsCount,
            /* startIp = */ getIp(),
            /* localsCount = */ currentState_.localsCount,
            /* stackSize = */ currentState_.stackSize,
            /* callstackSize = */ currentState_.callstackSize
        });
    }

    return true;
}

bool lama::verifier::verifyBytecodeFile(const lama::bytecode::BytecodeFile *file) {
    lama::verifier::BytecodeVerifier verifier{file};

    return verifier.verifyBytecode();
}
