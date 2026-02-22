#include "interpreter.hpp"
#include "verifier.hpp"

#include <cstdint>

#ifdef INTERPRETER_DEBUG
#include <iostream>
#endif

#include "../bytecode/bytecode_instructions.hpp"
#include "Lama/runtime/gc.h"
#include "lama_runtime.hpp"

#include "interpreter_runtime.hpp"

/* CallStack implementation */

lama::interpreter::utils::CallStack::CallStack(std::size_t initialSize)
    : topIndex_(initialSize) {

}

/* CallstackFrame implementation */

/*

==================
|   local #n     |
| .............. |
|   local #0     |
==================
| return address | <------ frame ptr
==================
|  argument #m   |
| .............. |
|  argument #0   |
==================

*/

lama::runtime::Word* lama::interpreter::CallstackFrame::getLocalsStartAddress() {
    return getFrameBase() + 1;
}

const lama::runtime::Word* lama::interpreter::CallstackFrame::getLocalsStartAddress() const {
    return getFrameBase() + 1;
}

lama::runtime::Word* lama::interpreter::CallstackFrame::getArgumentsStartAddress() {
    return getFrameBase() - getArgumentsCount();
}

const lama::runtime::Word* lama::interpreter::CallstackFrame::getArgumentsStartAddress() const {
    return getFrameBase() - getArgumentsCount();
}

lama::runtime::Word* lama::interpreter::CallstackFrame::getCapturedContentAddress() {
    return reinterpret_cast<lama::runtime::Word *>(*(getArgumentsStartAddress() - 1));
}

const lama::runtime::Word* lama::interpreter::CallstackFrame::getCapturedContentAddress() const {
    return reinterpret_cast<lama::runtime::Word *>(*(getArgumentsStartAddress() - 1));
}

lama::runtime::Word* lama::interpreter::CallstackFrame::getCapturedValueAddress(lama::bytecode::offset_t i) {
    return getCapturedContentAddress() + i + 1;
}

const lama::runtime::Word* lama::interpreter::CallstackFrame::getCapturedValueAddress(lama::bytecode::offset_t i) const {
    return getCapturedContentAddress() + i + 1;
}

std::uint32_t lama::interpreter::CallstackFrame::getCapturesCount() const {
    void *closureDataPtr = TO_DATA(getCapturedContentAddress());

    return ::get_len(static_cast<data *>(closureDataPtr)) - 1;
}

/* BytecodeInterpreterState implementation */

namespace {
lama::runtime::Word dataStackBuffer[lama::interpreter::OP_STACK_CAPACITY];

lama::runtime::native_uint_t getBoxedIntAsUInt(lama::runtime::native_int_t x) {
    return getNativeUIntRepresentation(lama::interpreter::runtime::Value{x}.getRawWord());
}

lama::runtime::native_uint_t getBoxedUIntAsUInt(lama::runtime::native_uint_t x) {
    return getNativeUIntRepresentation(lama::interpreter::runtime::Value{x}.getRawWord());
}

lama::runtime::native_uint_t getBoxedPointerAsUInt(void *ptr) {
    return getBoxedUIntAsUInt(reinterpret_cast<std::uintptr_t>(ptr));
}

lama::runtime::native_uint_t getBoxedPointerAsUInt(const void *ptr) {
    return getBoxedUIntAsUInt(reinterpret_cast<std::uintptr_t>(ptr));
}

lama::runtime::native_uint_t getStringViewAsUint(std::string_view sv) {
    return getNativeUIntRepresentation(lama::interpreter::runtime::Value{sv}.getRawWord());
}

lama::runtime::native_uint_t getBoxedStringViewAsUint(std::string_view sv) {
    const std::uintptr_t ptrval = getNativeUIntRepresentation(lama::interpreter::runtime::Value{sv}.getRawWord());

    return getBoxedUIntAsUInt(ptrval);
}

enum class CaptureType : unsigned char {
    GLOBAL = 0x0,
    LOCAL = 0x1,
    ARGUMENT = 0x2,
    CAPTURE = 0x3,
};
}

#ifdef INTERPRETER_DEBUG
    #define DO_IF_DEBUG(X) (X)
#else
    #define DO_IF_DEBUG(X)
#endif

#define DO_IF_DYN_VER(X) do {\
    if (mode_ == VerificationMode::DYNAMIC_VERIFICATION) {\
         (X);\
    }\
 } while (0)

lama::interpreter::BytecodeInterpreterState::BytecodeInterpreterState(
    const lama::bytecode::BytecodeFile *bytecodeFile,
    VerificationMode mode
)
    : gcInitialized_(false)
    , ip_(bytecodeFile->getEntryPointOffset())
    , instructionStartOffset_(0)
    , mode_(mode)
    , stack_(dataStackBuffer, bytecodeFile->getGlobalAreaSize() + lama::runtime::MAIN_FUNCTION_ARGUMENTS)
    , callstack_()
    , isClosureCalled_(false)
    , endReached_(false)
    , bytecodeFile_(bytecodeFile) {
    pushValue(lama::runtime::native_uint_t{0});
}

void lama::interpreter::BytecodeInterpreterState::executeArithBinop(lama::bytecode::InstructionOpCode opcode) {
    using lama::bytecode::InstructionOpCode;
    using lama::interpreter::runtime::LamaTag;

    const lama::runtime::native_int_t y = popIntValue().getNativeInt();
    const lama::runtime::native_int_t x = popIntValue().getNativeInt();

    lama::runtime::native_int_t result;

    switch (opcode) {
        case InstructionOpCode::BINOP_ADD:
            result = lama::runtime::native_int_t{x + y};
            break;
        case InstructionOpCode::BINOP_SUB:
            result = lama::runtime::native_int_t{x - y};
            break;
        case InstructionOpCode::BINOP_MUL:
            result = lama::runtime::native_int_t{x * y};
            break;
        case InstructionOpCode::BINOP_DIV:
            interpreterAssert(y != 0, "/ 0");
            result = lama::runtime::native_int_t{x / y};
            break;
        case InstructionOpCode::BINOP_MOD:
            interpreterAssert(y != 0, "% 0");
            result = lama::runtime::native_int_t{x % y};
            break;
        default:
            break;
    }

    pushValue(result);
}

void lama::interpreter::BytecodeInterpreterState::executeComparisonBinop(lama::bytecode::InstructionOpCode opcode) {
    using lama::bytecode::InstructionOpCode;

    bool flag = false;

    if (opcode == InstructionOpCode::BINOP_EQ) {
        const lama::interpreter::runtime::Value value1 = popValue();
        const lama::interpreter::runtime::Value value0 = popValue();

        interpreterAssert(value0.isInt() || value1.isInt(), "at least one of equality operands must be an integer");

        flag = value0.isInt() && value1.isInt() && (value0.getNativeInt() == value1.getNativeInt());
    } else {
        const lama::runtime::native_int_t y = popIntValue().getNativeInt();
        const lama::runtime::native_int_t x = popIntValue().getNativeInt();

        switch (opcode) {
            case InstructionOpCode::BINOP_LT:
                flag = x < y;
                break;
            case InstructionOpCode::BINOP_LE:
                flag = x <= y;
                break;
            case InstructionOpCode::BINOP_GT:
                flag = x > y;
                break;
            case InstructionOpCode::BINOP_GE:
                flag = x >= y;
                break;
            case InstructionOpCode::BINOP_NE:
                flag = x != y;
                break;
            default:
                break;
        }
    }

    pushValue(flag);
}

void lama::interpreter::BytecodeInterpreterState::executeLogicalBinop(lama::bytecode::InstructionOpCode opcode) {
    using lama::bytecode::InstructionOpCode;

    const lama::runtime::native_int_t y = popIntValue().getNativeInt();
    const lama::runtime::native_int_t x = popIntValue().getNativeInt();

    pushValue(static_cast<bool>(
        opcode == InstructionOpCode::BINOP_AND
        ? x && y
        : x || y
    ));
}

void lama::interpreter::BytecodeInterpreterState::executeBinop(lama::bytecode::InstructionOpCode opcode) {
    using lama::bytecode::InstructionOpCode;

    switch (opcode) {
        case InstructionOpCode::BINOP_ADD:
        case InstructionOpCode::BINOP_SUB:
        case InstructionOpCode::BINOP_MUL:
        case InstructionOpCode::BINOP_DIV:
        case InstructionOpCode::BINOP_MOD:
            executeArithBinop(opcode);
            break;
        case InstructionOpCode::BINOP_LT:
        case InstructionOpCode::BINOP_LE:
        case InstructionOpCode::BINOP_GT:
        case InstructionOpCode::BINOP_GE:
        case InstructionOpCode::BINOP_EQ:
        case InstructionOpCode::BINOP_NE:
            executeComparisonBinop(opcode);
            break;
        case InstructionOpCode::BINOP_AND:
        case InstructionOpCode::BINOP_OR:
            executeLogicalBinop(opcode);
            break;
        default:
            break;
    }

    #ifdef INTERPRETER_DEBUG
    static const char * const opStrings[] = {
        "+", "-", "*", "/", "%",
        "<", "<=", ">", ">=", "==", "!=",
        "&&", "!!",
    };

    DO_IF_DEBUG(std::cout << "BINOP\t" << opStrings[static_cast<unsigned int>(opcode) - 0x1] << '\n');
    #endif
}

void lama::interpreter::BytecodeInterpreterState::executeConst() {
    const std::int32_t constVal = fetchInt32();

    pushValue(lama::runtime::native_int_t{constVal});

    DO_IF_DEBUG(std::cout << "CONST\t" << constVal << '\n');
}

void lama::interpreter::BytecodeInterpreterState::executeString() {
    const std::int32_t strPos = fetchInt32();
    std::string_view strview = getString(strPos);
    const void * strTableEntity = strview.data();
    const void * const str = Bstring(reinterpret_cast<aint *>(&(strTableEntity)));

    pushValue(std::string_view{static_cast<const char *>(str)});

    DO_IF_DEBUG(std::cout << "STRING\t\"" << strview << "\"\n");
}

void lama::interpreter::BytecodeInterpreterState::executeSexp() {
    const std::int32_t sexpTagPos = fetchInt32();
    std::string_view sexpTagStr = getString(sexpTagPos);
    const lama::runtime::native_uint_t tagHash = ::LtagHash(const_cast<char *>(sexpTagStr.data()));
    pushWord(lama::runtime::Word{tagHash});

    const std::int32_t n = fetchInt32();
    DO_IF_DYN_VER(checkNonNegative(n, "sexp members count must not be negative"));
    lama::runtime::native_int_t *arrayPtr = reinterpret_cast<lama::runtime::native_int_t *>(peekWordAddress(n + 1));;

    lama::runtime::native_uint_t boxedMembers = getBoxedIntAsUInt(n + 1);
    lama::runtime::Word sexpPtr{reinterpret_cast<lama::runtime::native_uint_t>(::Bsexp(arrayPtr, boxedMembers))};

    popWords(n + 1);
    pushWord(sexpPtr);

    DO_IF_DEBUG(std::cout << "SEXP\t\"" << sexpTagStr << "\"\t" << n << "\n");
}

void lama::interpreter::BytecodeInterpreterState::executeSti() {
    const lama::runtime::Word value = popWord();
    void *valueAsPtr = reinterpret_cast<void *>(getNativeUIntRepresentation(value));

    const lama::interpreter::runtime::Value dst = popValue();
    interpreterAssert(!dst.isInt(), "expected a variable reference");

    lama::runtime::Word *dstPtr = reinterpret_cast<lama::runtime::Word *>(getNativeUIntRepresentation(dst.getRawWord()));

    ::Bsta(dstPtr, 0, valueAsPtr); // if snd is boxed (i. e. last bit is 0), then will act as STI

    pushWord(value);

    DO_IF_DEBUG(std::cout << "STI\n");
}

void lama::interpreter::BytecodeInterpreterState::executeSta() {
    const lama::runtime::Word value = popWord();
    void *valueAsPtr = reinterpret_cast<void *>(getNativeUIntRepresentation(value));

    lama::runtime::native_int_t index;

    const lama::interpreter::runtime::Value dst = popValue();
    lama::runtime::Word *dstPtr;

    if (dst.isInt()) {
        index = getBoxedIntAsUInt(dst.getNativeInt());
        dstPtr = reinterpret_cast<lama::runtime::Word *>(getNativeUIntRepresentation(popWord()));
    } else {
        index = 0;
        dstPtr = reinterpret_cast<lama::runtime::Word *>(getNativeUIntRepresentation(dst.getRawWord()));
    }

    ::Bsta(dstPtr, index, valueAsPtr);

    pushWord(value);

    DO_IF_DEBUG(std::cout << "STA\n");
}

void lama::interpreter::BytecodeInterpreterState::executeJmp() {
    const std::int32_t jmpAddress = fetchInt32();

    checkCodeOffset(jmpAddress);
    setIp(jmpAddress);

    DO_IF_DEBUG(std::cout << "JMP\t"
              << std::hex << std::showbase << jmpAddress
              << std::dec << '\n');
}

void lama::interpreter::BytecodeInterpreterState::executeEnd() {
    doReturnFromFunction();

    DO_IF_DEBUG(std::cout << "END\n");
}

void lama::interpreter::BytecodeInterpreterState::executeRet() {
    doReturnFromFunction();

    DO_IF_DEBUG(std::cout << "RET\n");
}

void lama::interpreter::BytecodeInterpreterState::doReturnFromFunction() {
    const CallstackFrame currentFrame = popFrame();

    const lama::runtime::Word result = popWord();

    while (peekWordAddress() != currentFrame.getFrameBase()) {
        popWord();
    }

    const std::int32_t retIp = popIntValue().getNativeInt();
    popWords(std::size_t{currentFrame.getArgumentsCount()});

    if (currentFrame.hasClosure()) {
        popWord(); // closure
    }

    pushWord(result);
    setIp(retIp);
}

void lama::interpreter::BytecodeInterpreterState::executeDrop() {
    popWord();

    DO_IF_DEBUG(std::cout << "DROP\n");
}

void lama::interpreter::BytecodeInterpreterState::executeDup() {
    lama::runtime::Word top = peekWord();
    pushWord(top);

    DO_IF_DEBUG(std::cout << "DUP\n");
}

void lama::interpreter::BytecodeInterpreterState::executeSwap() {
    lama::runtime::Word fst = popWord();
    lama::runtime::Word snd = popWord();

    pushWord(fst);
    pushWord(snd);

    DO_IF_DEBUG(std::cout << "SWAP\n");
}

void lama::interpreter::BytecodeInterpreterState::executeElem() {
    const lama::runtime::native_uint_t boxedIndex = getNativeUIntRepresentation(popWord());
    const std::uintptr_t ptrval = getNativeUIntRepresentation(popWord());

    const lama::runtime::native_uint_t elem = reinterpret_cast<std::uintptr_t>(Belem(reinterpret_cast<void *>(ptrval), boxedIndex));
    pushWord(lama::runtime::Word{elem});

    DO_IF_DEBUG(std::cout << "ELEM\n");
}

void lama::interpreter::BytecodeInterpreterState::executeLoadGlobalValue() {
    const std::int32_t globalIndex = fetchInt32();

    const lama::runtime::Word globalValue = getGlobalValue(globalIndex);

    pushWord(globalValue);

    DO_IF_DEBUG(std::cout << "LD\tG(" << globalIndex << ")\n");
}

void lama::interpreter::BytecodeInterpreterState::executeLoadLocalValue() {
    const std::int32_t localIndex = fetchInt32();

    const CallstackFrame currentFrame = peekFrame();
    checkLocalValueIndex(currentFrame, localIndex);

    const lama::runtime::Word localValue = currentFrame.getLocalValue(localIndex);

    pushWord(localValue);

    DO_IF_DEBUG(std::cout << "LD\tL(" << localIndex << ")\n");
}

void lama::interpreter::BytecodeInterpreterState::executeLoadArgumentValue() {
    const std::int32_t argIndex = fetchInt32();

    const CallstackFrame currentFrame = peekFrame();

    checkArgumentValueIndex(currentFrame, argIndex);
    const lama::runtime::Word argValue = currentFrame.getArgumentValue(argIndex);

    pushWord(argValue);

    DO_IF_DEBUG(std::cout << "LD\tA(" << argIndex << ")\n");
}

void lama::interpreter::BytecodeInterpreterState::executeLoadCapturedValue() {
    const std::int32_t capturedValIndex = fetchInt32();

    const CallstackFrame currentFrame = peekFrame();

    checkCapturedValueIndex(currentFrame, capturedValIndex);
    const lama::runtime::Word capturedValue = currentFrame.getCapturedValue(capturedValIndex);

    pushWord(capturedValue);

    DO_IF_DEBUG(std::cout << "LD\tC(" << capturedValIndex << ")\n");
}

void lama::interpreter::BytecodeInterpreterState::executeLoadGlobalValueAddress() {
    const std::int32_t globalValIndex = fetchInt32();

    lama::runtime::Word *globalValPtr = getGlobalValueAddress(globalValIndex);

    pushValue(globalValPtr);

    DO_IF_DEBUG(std::cout << "LDA\tG(" << globalValIndex << ")\n");
}

void lama::interpreter::BytecodeInterpreterState::executeLoadLocalValueAddress() {
    const std::int32_t localValIndex = fetchInt32();

    CallstackFrame currentFrame = peekFrame();

    checkLocalValueIndex(currentFrame, localValIndex);
    lama::runtime::Word* localValPtr = currentFrame.getLocalValueAddress(localValIndex);

    pushValue(localValPtr);

    DO_IF_DEBUG(std::cout << "LDA\tL(" << localValIndex << ")\n");
}

void lama::interpreter::BytecodeInterpreterState::executeLoadArgumentValueAddress() {
    const std::int32_t argIndex = fetchInt32();

    CallstackFrame currentFrame = peekFrame();

    checkArgumentValueIndex(currentFrame, argIndex);
    lama::runtime::Word* argPtr = currentFrame.getArgumentValueAddress(argIndex);

    pushValue(argPtr);

    DO_IF_DEBUG(std::cout << "LDA\tA(" << argIndex << ")\n");
}

void lama::interpreter::BytecodeInterpreterState::executeLoadCapturedValueAddress() {
    const std::int32_t capturedValIndex = fetchInt32();

    const CallstackFrame currentFrame = peekFrame();

    checkCapturedValueIndex(currentFrame, capturedValIndex);
    const lama::runtime::Word *capturedValuePtr = currentFrame.getCapturedValueAddress(capturedValIndex);

    pushValue(capturedValuePtr);

    DO_IF_DEBUG(std::cout << "LDA\tC(" << capturedValIndex << ")\n");
}

void lama::interpreter::BytecodeInterpreterState::executeStoreGlobalValue() {
    const std::int32_t globalIndex = fetchInt32();
    const lama::runtime::Word value = popWord();

    setGlobalValue(globalIndex, value);

    pushWord(value);

    DO_IF_DEBUG(std::cout << "ST\tG(" << globalIndex << ")\n");
}

void lama::interpreter::BytecodeInterpreterState::executeStoreLocalValue() {
    const std::int32_t localIndex = fetchInt32();
    const lama::runtime::Word value = popWord();

    CallstackFrame currentFrame = peekFrame();

    checkLocalValueIndex(currentFrame, localIndex);
    currentFrame.setLocalValue(localIndex, value);

    pushWord(value);

    DO_IF_DEBUG(std::cout << "ST\tL(" << localIndex << ")\n");
}

void lama::interpreter::BytecodeInterpreterState::executeStoreArgumentValue() {
    const std::int32_t argumentIndex = fetchInt32();
    const lama::runtime::Word value = popWord();

    CallstackFrame currentFrame = peekFrame();

    checkArgumentValueIndex(currentFrame, argumentIndex);
    currentFrame.setArgumentValue(argumentIndex, value);

    pushWord(value);

    DO_IF_DEBUG(std::cout << "ST\tA(" << argumentIndex << ")\n");
}

void lama::interpreter::BytecodeInterpreterState::executeStoreCapturedValue() {
    const std::int32_t capturedValIndex = fetchInt32();
    const lama::runtime::Word value = popWord();

    CallstackFrame currentFrame = peekFrame();

    checkCapturedValueIndex(currentFrame, capturedValIndex);
    currentFrame.setCapturedValue(capturedValIndex, value);

    pushWord(value);

    DO_IF_DEBUG(std::cout << "ST\tC(" << capturedValIndex << ")\n");
}

void lama::interpreter::BytecodeInterpreterState::executeConditionalJmpIfZero() {
    const std::int32_t nextIp = fetchInt32();
    checkCodeOffset(nextIp);

    const lama::runtime::native_int_t val = popIntValue().getNativeInt();

    if (val == 0) {
        setIp(nextIp);
    }

    DO_IF_DEBUG(std::cout << "CJMPz\t"
              << std::hex << std::showbase << nextIp
              << std::dec << '\n');
}

void lama::interpreter::BytecodeInterpreterState::executeConditionalJmpIfNotZero() {
    const std::int32_t nextIp = fetchInt32();
    checkCodeOffset(nextIp);

    const lama::runtime::native_int_t val = popIntValue().getNativeInt();

    if (val != 0) {
        setIp(nextIp);
    }

    DO_IF_DEBUG(std::cout << "CJMPnz\t"
              << std::hex << std::showbase << nextIp
              << std::dec << '\n');
}

void lama::interpreter::BytecodeInterpreterState::executeBegin() {
    const std::int32_t argsNum = fetchInt32();
    DO_IF_DYN_VER(checkNonNegative(argsNum, "arguments number must not be negative"));

    const std::int32_t val = fetchInt32();

    const std::int16_t localsNum = val & 0xffff;
    DO_IF_DYN_VER(checkNonNegative(localsNum, "locals number must not be negative"));

    if (mode_ == lama::interpreter::VerificationMode::STATIC_VERIFICATION) {
        const std::uint16_t frameStackSize = (static_cast<std::uint32_t>(val) >> 16) & 0xffff;
        checkStackOverflow(stack_.size() + localsNum + frameStackSize);
    }

    processFunctionBegin(argsNum, localsNum, false);

    DO_IF_DEBUG(std::cout << "BEGIN\t" << argsNum << "\t" << localsNum << '\n');
}

void lama::interpreter::BytecodeInterpreterState::executeClosureBegin() {
    const std::int32_t argsNum = fetchInt32();
    DO_IF_DYN_VER(checkNonNegative(argsNum, "arguments number must not be negative"));

    const std::int32_t val = fetchInt32();
    const std::int16_t localsNum = val & 0xffff;
    DO_IF_DYN_VER(checkNonNegative(localsNum, "locals number must not be negative"));

    if (mode_ == lama::interpreter::VerificationMode::STATIC_VERIFICATION) {
        const std::uint16_t frameStackSize = (static_cast<std::uint32_t>(val) >> 16) & 0xffff;
        checkStackOverflow(stack_.size() + localsNum + frameStackSize);
    }

    processFunctionBegin(argsNum, localsNum, true);

    DO_IF_DEBUG(std::cout << "CBEGIN\t" << argsNum << "\t" << localsNum << '\n');
}

void lama::interpreter::BytecodeInterpreterState::processFunctionBegin(std::size_t argsNum, std::size_t localsNum, bool hasCaptures) {
    if (hasCaptures) {
        lama::interpreter::runtime::Value closureValue{peekWord(1 + argsNum + 1)}; // retIp + argsNum + closure

        interpreterAssert(closureValue.isClosure(), "closure value must be present in stack");
    }

    pushFrame({
        /* frameBase   = */ peekWordAddress(),
        /* argsCount   = */ argsNum,
        /* localsCount = */ localsNum,
        /* hasClosure  = */ isClosureCalled_,
        /* hasCaptures = */ hasCaptures,
    });

    for (std::int32_t i = 0; i < localsNum; ++i) {
        pushValue(lama::runtime::native_int_t{0});
    }
}

void lama::interpreter::BytecodeInterpreterState::executeClosure() {
    const std::int32_t locationAddress = fetchInt32();
    checkCodeOffset(locationAddress);

    const std::int32_t argsNum = fetchInt32();
    DO_IF_DYN_VER(checkNonNegative(argsNum, "arguments number must not be negative"));

    pushWord(lama::runtime::Word(locationAddress));

    const std::int32_t captureIp = getIp();

    for (std::int32_t i = 0; i < argsNum; ++i) {
        lama::runtime::Word w;

        CaptureType captureType{std::to_integer<unsigned char>(fetchByte())};
        const std::int32_t index = fetchInt32();

        lama::interpreter::CallstackFrame frame = peekFrame();

        switch (captureType) {
            case CaptureType::GLOBAL:
                w = getGlobalValue(index);
                break;
            case CaptureType::LOCAL:
                checkLocalValueIndex(frame, index);
                w = frame.getLocalValue(index);
                break;
            case CaptureType::ARGUMENT:
                checkArgumentValueIndex(frame, index);
                w = frame.getArgumentValue(index);
                break;
            case CaptureType::CAPTURE:
                checkCapturedValueIndex(frame, index);
                w = frame.getCapturedValue(index);
                break;
            default:
                DO_IF_DYN_VER(interpreterAssert(false, "invalid varspec"));
                break;
        }

        pushWord(w);
    }

    lama::runtime::native_int_t *ptrval = reinterpret_cast<lama::runtime::native_int_t *>(peekWordAddress(argsNum + 1));

    void *closurePtr = ::Bclosure(ptrval, getBoxedIntAsUInt(argsNum));

    popWords(argsNum + 1);

    pushValue(closurePtr);

    #ifdef INTERPRETER_DEBUG

    std::cout << "CLOSURE\t"
              << std::hex << std::showbase
              << locationAddress << std::dec;

    std::int32_t lookupIp = captureIp;

    for (std::int32_t i = 0; i < argsNum; ++i) {
        CaptureType captureType{std::to_integer<unsigned char>(lookupByte(lookupIp))};
        lookupIp += sizeof(std::byte);

        const std::int32_t index = lookupInt32(lookupIp);
        lookupIp += sizeof(std::int32_t);

        switch (captureType) {
            case CaptureType::GLOBAL:
                std::cout << "\tG(" << index << ")";;
                break;
            case CaptureType::LOCAL:
                std::cout << "\tL(" << index << ")";
                break;
            case CaptureType::ARGUMENT:
                std::cout << "\tA(" << index << ")";
                break;
            case CaptureType::CAPTURE:
                std::cout << "\tC(" << index << ")";
                break;
        }
    }

    std::cout << '\n';

    #endif
}

void lama::interpreter::BytecodeInterpreterState::executeCallClosure() {
    const std::int32_t argsNum = fetchInt32();
    DO_IF_DYN_VER(checkNonNegative(argsNum, "arguments number must not be negative"));

    const lama::runtime::Word closurePtrWord = peekWord(argsNum + 1);
    const lama::runtime::native_int_t *closureContentPtr = reinterpret_cast<const lama::runtime::native_int_t *>(closurePtrWord);

    const std::int32_t locationAddress = closureContentPtr[0];
    checkCodeOffset(locationAddress);
    lama::bytecode::InstructionOpCode startOp = lookupInstrOpCode(locationAddress);
    interpreterAssert(
        startOp == lama::bytecode::InstructionOpCode::BEGIN || startOp == lama::bytecode::InstructionOpCode::CBEGIN,
        "CALLC should go to BEGIN or CBEGIN instruction"
    );

    pushValue(lama::runtime::native_int_t{getIp()});

    setIp(locationAddress);
    isClosureCalled_ = true;

    DO_IF_DEBUG(std::cout << "CALLC\t" << argsNum << '\n');
}

void lama::interpreter::BytecodeInterpreterState::executeCall() {
    const std::int32_t locationAddress = fetchInt32();
    checkCodeOffset(locationAddress);
    lama::bytecode::InstructionOpCode startOp = lookupInstrOpCode(locationAddress);
    DO_IF_DYN_VER(interpreterAssert(
        startOp == lama::bytecode::InstructionOpCode::BEGIN,
        "CALL should go to BEGIN instruction"
    ));

    const std::int32_t argsNum = fetchInt32();
    DO_IF_DYN_VER(checkNonNegative(argsNum, "arguments number must not be negative"));

    pushValue(lama::runtime::native_int_t{getIp()});

    setIp(locationAddress);
    isClosureCalled_ = false;

    DO_IF_DEBUG(std::cout << std::hex << std::showbase
              << "CALL\t" << locationAddress << "\t" << argsNum
              << std::dec << '\n');
}

void lama::interpreter::BytecodeInterpreterState::executeTag() {
    const std::int32_t sexpTagPos = fetchInt32();
    const std::string_view sexpTagStr = getString(sexpTagPos);
    const lama::runtime::native_int_t tagHash = ::LtagHash(const_cast<char *>(sexpTagStr.data()));

    const std::int32_t n = fetchInt32();
    DO_IF_DYN_VER(checkNonNegative(n, "sexp members count must not be negative"));
    const lama::runtime::native_uint_t boxedMembers = getBoxedIntAsUInt(n);

    const lama::runtime::native_uint_t ptrval = getNativeUIntRepresentation(popWord());

    pushWord(lama::runtime::Word(::Btag(reinterpret_cast<void *>(ptrval), tagHash, boxedMembers)));

    DO_IF_DEBUG(std::cout << "TAG\t\"" << sexpTagStr << "\"\t" << n << '\n');
}

void lama::interpreter::BytecodeInterpreterState::executeArray() {
    const std::int32_t n = fetchInt32();
    DO_IF_DYN_VER(checkNonNegative(n, "array length must not be negative"));

    const lama::interpreter::runtime::Value boxedLenVal = lama::runtime::native_int_t{n};

    const void *ptr = reinterpret_cast<const void *>(getNativeUIntRepresentation(peekWord()));

    const std::uintptr_t ptrval = getNativeUIntRepresentation(popWord());

    pushWord(lama::runtime::Word(::Barray_patt(reinterpret_cast<void *>(ptrval), getNativeUIntRepresentation(boxedLenVal.getRawWord()))));

    DO_IF_DEBUG(std::cout << std::dec
              << "ARRAY\t" << n << '\n');
}

void lama::interpreter::BytecodeInterpreterState::executeFail() {
    const std::int32_t lineNum = fetchInt32();
    DO_IF_DYN_VER(interpreterAssert(lineNum >= 1, "line number must be greater than zero"));
    const lama::runtime::native_uint_t boxedLineNum = getBoxedIntAsUInt(lineNum);

    const std::int32_t colNum = fetchInt32();
    DO_IF_DYN_VER(interpreterAssert(colNum >= 1, "column number must be greater than zero"));
    const lama::runtime::native_uint_t boxedColNum = getBoxedIntAsUInt(colNum);

    const lama::runtime::native_uint_t value = getNativeUIntRepresentation(popWord());

    ::Bmatch_failure(reinterpret_cast<void *>(value), const_cast<char *>("<bytecode_file>"), boxedLineNum, boxedColNum);

    DO_IF_DEBUG(std::cout << "FAIL\t" << lineNum << "\t" << colNum << '\n');
}

void lama::interpreter::BytecodeInterpreterState::executeLine() {
    const std::int32_t lineNum = fetchInt32();

    DO_IF_DEBUG(std::cout << "LINE\t" << lineNum << '\n');
}

void lama::interpreter::BytecodeInterpreterState::executePattStr() {
    const std::uintptr_t y = getNativeUIntRepresentation(popWord());
    void *str1 = reinterpret_cast<void *>(y);

    const std::uintptr_t x = getNativeUIntRepresentation(popWord());
    void *str0 = reinterpret_cast<void *>(x);

    pushWord(lama::runtime::Word(::Bstring_patt(str0, str1)));

    DO_IF_DEBUG(std::cout << "PATT\t=str\n");
}

void lama::interpreter::BytecodeInterpreterState::executePattString() {
    const std::uintptr_t ptrval = getNativeUIntRepresentation(popWord());

    pushWord(lama::runtime::Word(::Bstring_tag_patt(reinterpret_cast<void *>(ptrval))));

    DO_IF_DEBUG(std::cout << "PATT\t#string\n");
}

void lama::interpreter::BytecodeInterpreterState::executePattArray() {
    const std::uintptr_t ptrval = getNativeUIntRepresentation(popWord());

    pushWord(lama::runtime::Word(::Barray_tag_patt(reinterpret_cast<void *>(ptrval))));

    DO_IF_DEBUG(std::cout << "PATT\t#array\n");
}

void lama::interpreter::BytecodeInterpreterState::executePattSexp() {
    const std::uintptr_t ptrval = getNativeUIntRepresentation(popWord());

    pushWord(lama::runtime::Word(::Bsexp_tag_patt(reinterpret_cast<void *>(ptrval))));

    DO_IF_DEBUG(std::cout << "PATT\t#sexp\n");
}

void lama::interpreter::BytecodeInterpreterState::executePattRef() {
    const std::uintptr_t ptrval = getNativeUIntRepresentation(popWord());

    pushWord(lama::runtime::Word(::Bboxed_patt(reinterpret_cast<void *>(ptrval))));

    DO_IF_DEBUG(std::cout << "PATT\t#ref\n");
}

void lama::interpreter::BytecodeInterpreterState::executePattVal() {
    const std::uintptr_t ptrval = getNativeUIntRepresentation(popWord());

    pushWord(lama::runtime::Word(::Bunboxed_patt(reinterpret_cast<void *>(ptrval))));

    DO_IF_DEBUG(std::cout << "PATT\t#val\n");
}

void lama::interpreter::BytecodeInterpreterState::executePattFun() {
    const std::uintptr_t ptrval = getNativeUIntRepresentation(popWord());

    pushWord(lama::runtime::Word(::Bclosure_tag_patt(reinterpret_cast<void *>(ptrval))));

    DO_IF_DEBUG(std::cout << "PATT\t#fun\n");
}

void lama::interpreter::BytecodeInterpreterState::executeCallLread() {
    const lama::runtime::Word w{static_cast<lama::runtime::native_uint_t>(::Lread())};

    pushWord(w);

    DO_IF_DEBUG(std::cout << "CALL\tLread\n");
}

void lama::interpreter::BytecodeInterpreterState::executeCallLwrite() {
    const lama::interpreter::runtime::Value val = popIntValue();

    ::Lwrite(getNativeUIntRepresentation(val.getRawWord()));

    pushWord(lama::runtime::Word{});

    DO_IF_DEBUG(std::cout << "CALL\tLwrite\n");
}

void lama::interpreter::BytecodeInterpreterState::executeCallLlength() {
    const std::uintptr_t ptrval = getNativeUIntRepresentation(popWord());

    pushWord(lama::runtime::Word(::Llength(reinterpret_cast<void *>(ptrval))));

    DO_IF_DEBUG(std::cout << "CALL\tLlength\n");
}

void lama::interpreter::BytecodeInterpreterState::executeCallLstring() {
    std::uintptr_t ptrval = getNativeUIntRepresentation(popWord());

    pushValue(::Lstring(reinterpret_cast<lama::runtime::native_int_t *>(&ptrval)));

    DO_IF_DEBUG(std::cout << "CALL\tLstring\n");
}

void lama::interpreter::BytecodeInterpreterState::executeCallBarray() {
    const std::int32_t n = fetchInt32();
    lama::runtime::native_uint_t boxedLen = getBoxedIntAsUInt(n);

    lama::runtime::native_int_t *arrayPtr = reinterpret_cast<lama::runtime::native_int_t *>(peekWordAddress(n));

    const void *allocatedArray = Barray(arrayPtr, boxedLen);

    popWords(n);

    pushWord(lama::runtime::Word{reinterpret_cast<std::uintptr_t>(allocatedArray)});

    DO_IF_DEBUG(std::cout << std::dec << "CALL\tBarray " << n << "\n");
}

void lama::interpreter::BytecodeInterpreterState::executeCurrentInstruction() {
    if (isEndReached()) {
        return;
    }

    using lama::bytecode::InstructionOpCode;

    setInstructionStartOffset(getIp());

    DO_IF_DEBUG(std::cerr << "[interpreter-debug]: "
              << "ip = " << std::hex << std::showbase << getInstructionStartOffset()
              << ", op = " << std::to_integer<unsigned int>(lookupByte())
              << std::dec << '\n');

    InstructionOpCode op = fetchInstrOpCode();

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
            executeBinop(op);
            break;
        case InstructionOpCode::CONST:
            executeConst();
            break;
        case InstructionOpCode::STRING:
            executeString();
            break;
        case InstructionOpCode::SEXP:
            executeSexp();
            break;
        case InstructionOpCode::STI:
            executeSti();
            break;
        case InstructionOpCode::STA:
            executeSta();
            break;
        case InstructionOpCode::JMP:
            executeJmp();
            break;
        case InstructionOpCode::END:
            executeEnd();
            endReached_ = callstack_.empty();
            break;
        case InstructionOpCode::RET:
            executeRet();
            endReached_ = callstack_.empty();
            break;
        case InstructionOpCode::DROP:
            executeDrop();
            break;
        case InstructionOpCode::DUP:
            executeDup();
            break;
        case InstructionOpCode::SWAP:
            executeSwap();
            break;
        case InstructionOpCode::ELEM:
            executeElem();
            break;
        case InstructionOpCode::LD_G:
            executeLoadGlobalValue();
            break;
        case InstructionOpCode::LD_L:
            executeLoadLocalValue();
            break;
        case InstructionOpCode::LD_A:
            executeLoadArgumentValue();
            break;
        case InstructionOpCode::LD_C:
            executeLoadCapturedValue();
            break;
        case InstructionOpCode::LDA_G:
            executeLoadGlobalValueAddress();
            break;
        case InstructionOpCode::LDA_L:
            executeLoadLocalValueAddress();
            break;
        case InstructionOpCode::LDA_A:
            executeLoadArgumentValueAddress();
            break;
        case InstructionOpCode::LDA_C:
            executeLoadCapturedValueAddress();
            break;
        case InstructionOpCode::ST_G:
            executeStoreGlobalValue();
            break;
        case InstructionOpCode::ST_L:
            executeStoreLocalValue();
            break;
        case InstructionOpCode::ST_A:
            executeStoreArgumentValue();
            break;
        case InstructionOpCode::ST_C:
            executeStoreCapturedValue();
            break;
        case InstructionOpCode::CJMPZ:
            executeConditionalJmpIfZero();
            break;
        case InstructionOpCode::CJMPNZ:
            executeConditionalJmpIfNotZero();
            break;
        case InstructionOpCode::BEGIN:
            executeBegin();
            break;
        case InstructionOpCode::CBEGIN:
            executeClosureBegin();
            break;
        case InstructionOpCode::CLOSURE:
            executeClosure();
            break;
        case InstructionOpCode::CALLC:
            executeCallClosure();
            break;
        case InstructionOpCode::CALL:
            executeCall();
            break;
        case InstructionOpCode::TAG:
            executeTag();
            break;
        case InstructionOpCode::ARRAY:
            executeArray();
            break;
        case InstructionOpCode::FAIL:
            executeFail();
            break;
        case InstructionOpCode::LINE:
            executeLine();
            break;
        case InstructionOpCode::PATT_STR:
            executePattStr();
            break;
        case InstructionOpCode::PATT_STRING:
            executePattString();
            break;
        case InstructionOpCode::PATT_ARRAY:
            executePattArray();
            break;
        case InstructionOpCode::PATT_SEXP:
            executePattSexp();
            break;
        case InstructionOpCode::PATT_REF:
            executePattRef();
            break;
        case InstructionOpCode::PATT_VAL:
            executePattVal();
            break;
        case InstructionOpCode::PATT_FUN:
            executePattFun();
            break;
        case InstructionOpCode::CALL_LREAD:
            executeCallLread();
            break;
        case InstructionOpCode::CALL_LWRITE:
            executeCallLwrite();
            break;
        case InstructionOpCode::CALL_LLENGTH:
            executeCallLlength();
            break;
        case InstructionOpCode::CALL_LSTRING:
            executeCallLstring();
            break;
        case InstructionOpCode::CALL_BARRAY:
            executeCallBarray();
            break;
        default:
            DO_IF_DYN_VER(interpreterAssert(false, "invalid instruction"));
            break;
    }
}

void lama::interpreter::interpretBytecodeFile(bytecode::BytecodeFile *file, VerificationMode mode) {
    ::__init();

    /*
     * A verifier tries statically check the bytecode file.
     * If verifier meets problematic instructions (e.g. STA), verification won't be finished.
     * Incomplete static verification is not an obstacle, we can enable dynamic checks.
     * Even though the verifier may have changed some bytes, interpretation will work as usual,
     * a number of local variables will be read from 2 lower bytes of second parameter of
     * [C]BEGIN bytecode instruction
     */
    if (mode == VerificationMode::STATIC_VERIFICATION && !lama::verifier::verifyBytecodeFile(file)) {
        mode = VerificationMode::DYNAMIC_VERIFICATION;
    }

    BytecodeInterpreterState state{file, mode};

    while (!state.isEndReached()) {
        state.executeCurrentInstruction();
    }

    ::__shutdown();
}
