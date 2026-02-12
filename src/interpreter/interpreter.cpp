#include "interpreter.hpp"

#include <cstdint>
#include <memory>

// #ifdef INTERPRETER_DEBUG
#include <iostream>
// #endif

#include "../bytecode/bytecode_instructions.hpp"
#include "Lama/runtime/runtime_common.h"
#include "lama_runtime.hpp"

#include "interpreter_runtime.hpp"

/* Stack implementation */

template<class T, std::size_t StackCapacity>
lama::interpreter::utils::Stack<T, StackCapacity>::Stack(std::size_t initialSize)
    : topIndex_(initialSize) {

}

template<class T, std::size_t StackCapacity>
void lama::interpreter::utils::Stack<T, StackCapacity>::push(const T &value) {
    buffer_[topIndex_++] = value;
}

template<class T, std::size_t StackCapacity>
void lama::interpreter::utils::Stack<T, StackCapacity>::push(T&& value) {
    buffer_[topIndex_++] = std::move(value);
}

template<class T, std::size_t StackCapacity>
T lama::interpreter::utils::Stack<T, StackCapacity>::peek(std::size_t offset) const {
    return buffer_[topIndex_ - offset];
}

template<class T, std::size_t StackCapacity>
T* lama::interpreter::utils::Stack<T, StackCapacity>::peekAddress(std::size_t offset) {
    return std::addressof(buffer_[topIndex_ - offset]);
}

template<class T, std::size_t StackCapacity>
const T* lama::interpreter::utils::Stack<T, StackCapacity>::peekAddress(std::size_t offset) const {
    return std::addressof(buffer_[topIndex_ - offset]);
}

template<class T, std::size_t StackCapacity>
T lama::interpreter::utils::Stack<T, StackCapacity>::pop() {
    T top = peek();
    --topIndex_;

    return top;
}

template<class T, std::size_t StackCapacity>
std::size_t lama::interpreter::utils::Stack<T, StackCapacity>::size() const {
    return topIndex_;
}

template<class T, std::size_t StackCapacity>
bool lama::interpreter::utils::Stack<T, StackCapacity>::empty() const {
    return size() == 0;
}

template<class T, std::size_t StackCapacity>
bool lama::interpreter::utils::Stack<T, StackCapacity>::nonEmpty() const {
    return size() > 0;
}

/* Callstack implementation */

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

lama::interpreter::CallstackFrame::CallstackFrame(
    lama::runtime::Word *frameBase,
    std::int32_t
    argsCount,
    std::int32_t localsCount,
    bool hasClosure,
    bool hasCaptures
)   : frameBase_(frameBase)
    , argsCount_(argsCount)
    , localsCount_(localsCount)
    , hasClosure_(hasClosure)
    , hasCaptures_(hasCaptures) {

}

lama::runtime::Word* lama::interpreter::CallstackFrame::getFrameBase() const {
    return frameBase_;
}

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

lama::runtime::Word* lama::interpreter::CallstackFrame::getArgumentValueAddress(std::int32_t i) {
    return getArgumentsStartAddress() + i;
}

const lama::runtime::Word* lama::interpreter::CallstackFrame::getArgumentValueAddress(std::int32_t i) const {
    return getArgumentsStartAddress() + i;
}

void lama::interpreter::CallstackFrame::setArgumentValue(std::int32_t i, lama::runtime::Word value) {
    *getArgumentValueAddress(i) = value;
}

lama::runtime::Word lama::interpreter::CallstackFrame::getArgumentValue(std::int32_t i) const {
    return *getArgumentValueAddress(i);
}

lama::runtime::Word* lama::interpreter::CallstackFrame::getLocalValueAddress(std::int32_t i) {
    return getLocalsStartAddress() + i;
}

const lama::runtime::Word* lama::interpreter::CallstackFrame::getLocalValueAddress(std::int32_t i) const {
    return getLocalsStartAddress() + i;
}

void lama::interpreter::CallstackFrame::setLocalValue(std::int32_t i, lama::runtime::Word value) {
    *getLocalValueAddress(i) = value;
}

lama::runtime::Word lama::interpreter::CallstackFrame::getLocalValue(std::int32_t i) const {
    return *getLocalValueAddress(i);
}

lama::runtime::Word* lama::interpreter::CallstackFrame::getCapturedValueAddress(std::int32_t i) {
    return getCapturedContentAddress() + i + 1;
}

const lama::runtime::Word* lama::interpreter::CallstackFrame::getCapturedValueAddress(std::int32_t i) const {
    return getCapturedContentAddress() + i + 1;
}

void lama::interpreter::CallstackFrame::setCapturedValue(std::int32_t i, lama::runtime::Word value) {
    *getCapturedValueAddress(i) = value;
}

lama::runtime::Word lama::interpreter::CallstackFrame::getCapturedValue(std::int32_t i) const {
    return *getCapturedValueAddress(i);
}

std::uint32_t lama::interpreter::CallstackFrame::getArgumentsCount() const {
    return argsCount_;
}

std::uint32_t lama::interpreter::CallstackFrame::getLocalsCount() const {
    return localsCount_;
}

std::uint32_t lama::interpreter::CallstackFrame::getCapturesCount() const {
    void *closureDataPtr = TO_DATA(getCapturedContentAddress());

    return ::get_len(static_cast<data *>(closureDataPtr)) - 1;
}

bool lama::interpreter::CallstackFrame::hasClosure() const {
    return hasClosure_;
}

bool lama::interpreter::CallstackFrame::hasCaptures() const {
    return hasCaptures_;
}

/* BytecodeInterpreter implementation */

namespace {
lama::runtime::Word dataStackBuffer[LAMA_OP_STACK_CAPACITY];

constexpr std::size_t MAIN_FUNCTION_ARGUMENTS = 2;

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
    #define DEBUG_TRY(X) (X)
#else
    #define DEBUG_TRY(X)
#endif

lama::interpreter::BytecodeInterpreter::BytecodeInterpreter(const lama::bytecode::BytecodeFile *bytecodeFile)
    : gcInitialized_(false)
    , ip_(bytecodeFile->getEntryPointOffset())
    , instructionStartOffset_(0)
    , stack_(dataStackBuffer, bytecodeFile->getGlobalAreaSize() + MAIN_FUNCTION_ARGUMENTS + 1)
    , callstack_()
    , isClosureCalled_(false)
    , endReached_(false)
    , bytecodeFile_(bytecodeFile) {

}

lama::interpreter::BytecodeInterpreter::~BytecodeInterpreter() {
    deinitializeGc();
}

void lama::interpreter::BytecodeInterpreter::setIp(std::int32_t newIp) {
    ip_ = newIp;
}

std::int32_t lama::interpreter::BytecodeInterpreter::getIp() const {
    return ip_;
}

void lama::interpreter::BytecodeInterpreter::advanceIp(std::int32_t offset) {
    ip_ += offset;
}

std::int32_t lama::interpreter::BytecodeInterpreter::getInstructionStartOffset() const {
    return instructionStartOffset_;
}

void lama::interpreter::BytecodeInterpreter::setInstructionStartOffset(std::int32_t offset) {
    instructionStartOffset_ = offset;
}

void lama::interpreter::BytecodeInterpreter::initializeGc() {
    if (!gcInitialized_) {
        __init();
        gcInitialized_ = true;
    }
}

void lama::interpreter::BytecodeInterpreter::deinitializeGc() {
    if (gcInitialized_) {
        __shutdown();
        gcInitialized_ = false;
    }
}

bool lama::interpreter::BytecodeInterpreter::isGcInitialized() const {
    return gcInitialized_;
}

const lama::runtime::Word* lama::interpreter::BytecodeInterpreter::getGlobalsStartAddress() const {
    return stack_.data();
}

lama::runtime::Word* lama::interpreter::BytecodeInterpreter::getGlobalsStartAddress() {
    return stack_.data();
}

std::byte lama::interpreter::BytecodeInterpreter::lookupByte(std::int32_t pos) const {
    interpreterAssert(pos >= 0, "code offset must not be negative");
    interpreterAssert(pos < bytecodeFile_->getCodeSize(), "code offset out of range");

    return bytecodeFile_->getCodeByte(pos);
}

std::byte lama::interpreter::BytecodeInterpreter::lookupByte() const {
    return lookupByte(getIp());
}

lama::bytecode::InstructionOpCode lama::interpreter::BytecodeInterpreter::lookupInstrOpCode(std::int32_t pos) const {
    return lama::bytecode::InstructionOpCode{static_cast<unsigned char>(lookupByte(pos))};
}

lama::bytecode::InstructionOpCode lama::interpreter::BytecodeInterpreter::lookupInstrOpCode() const {
    return lama::bytecode::InstructionOpCode{static_cast<unsigned char>(lookupByte())};
}

std::byte lama::interpreter::BytecodeInterpreter::fetchByte() {
    std::byte val = lookupByte();
    advanceIp(sizeof(std::byte));

    return val;
}

lama::bytecode::InstructionOpCode lama::interpreter::BytecodeInterpreter::fetchInstrOpCode() {
    return lama::bytecode::InstructionOpCode{static_cast<unsigned char>(fetchByte())};
}

std::int32_t lama::interpreter::BytecodeInterpreter::lookupInt32(std::int32_t pos) const {
    interpreterAssert(pos + 4 >= 4, "code offset must not be negative");
    interpreterAssert(pos + 4 < bytecodeFile_->getCodeSize(), "code offset out of range");

    std::int32_t val;
    bytecodeFile_->copyCodeBytes(static_cast<std::byte *>(static_cast<void *>(&val)), pos, sizeof(val));

    return val;
}

std::int32_t lama::interpreter::BytecodeInterpreter::lookupInt32() const {
    return lookupInt32(getIp());
}

std::int32_t lama::interpreter::BytecodeInterpreter::fetchInt32() {
    std::int32_t val = lookupInt32();
    advanceIp(sizeof(std::int32_t));

    return val;
}

std::string_view lama::interpreter::BytecodeInterpreter::getString(std::int32_t index) const {
    checkNonNegative(index, "string table index must not be negative");
    interpreterAssert(index < bytecodeFile_->getStringTableSize(), "string table index is out of range");

    return bytecodeFile_->getString(index);
}

void lama::interpreter::BytecodeInterpreter::pushWord(lama::runtime::Word w) {
    interpreterAssert(stack_.size() < stack_.capacity, "Operand stack exhausted");

    stack_.push(w);
}

lama::runtime::Word lama::interpreter::BytecodeInterpreter::peekWord(std::size_t offset) const {
    interpreterAssert(stack_.size() >= offset, "Operand stack index overflow while peeking Lama Word");

    return stack_.peek(offset);
}

lama::runtime::Word *lama::interpreter::BytecodeInterpreter::peekWordAddress(std::size_t offset) {
    interpreterAssert(stack_.size() >= offset, "Operand stack index overflow while peeking Lama Word addr");

    return stack_.peekAddress(offset);
}

const lama::runtime::Word *lama::interpreter::BytecodeInterpreter::peekWordAddress(std::size_t offset) const {
    interpreterAssert(stack_.size() >= offset, "Operand stack index overflow while peeking Lama Word addr");

    return stack_.peekAddress(offset);
}

lama::interpreter::runtime::Value lama::interpreter::BytecodeInterpreter::peekValue(std::size_t offset) const {
    interpreterAssert(stack_.size() >= offset, "Operand stack index overflow while peeking Lama Value");

    const lama::runtime::Word w = peekWord(offset);

    return lama::interpreter::runtime::Value{w};
}

lama::runtime::Word lama::interpreter::BytecodeInterpreter::popWord() {
    interpreterAssert(stack_.nonEmpty(), "Operand stack is empty");

    lama::runtime::Word w = stack_.pop();

    return w;
}

lama::interpreter::runtime::Value lama::interpreter::BytecodeInterpreter::popValue() {
    const lama::runtime::Word w = popWord();

    return lama::interpreter::runtime::Value{w};
}

lama::interpreter::runtime::Value lama::interpreter::BytecodeInterpreter::popIntValue(std::string_view message) {
    const lama::interpreter::runtime::Value val = popValue();

    interpreterAssert(val.isInt(), message);

    return val;
}

lama::interpreter::runtime::Value lama::interpreter::BytecodeInterpreter::popStringValue(std::string_view message) {
    const lama::interpreter::runtime::Value val = popValue();

    interpreterAssert(val.isString(), message);

    return val;
}

lama::interpreter::runtime::Value lama::interpreter::BytecodeInterpreter::popArrayValue(std::string_view message) {
    const lama::interpreter::runtime::Value val = popValue();

    interpreterAssert(val.isArray(), message);

    return val;
}

lama::interpreter::runtime::Value lama::interpreter::BytecodeInterpreter::popClosureValue(std::string_view message) {
    const lama::interpreter::runtime::Value val = popValue();

    interpreterAssert(val.isClosure(), message);

    return val;
}

lama::interpreter::runtime::Value lama::interpreter::BytecodeInterpreter::popSexpValue(std::string_view message) {
    const lama::interpreter::runtime::Value val = popValue();

    interpreterAssert(val.isSexp(), message);

    return val;
}

void lama::interpreter::BytecodeInterpreter::popWords(std::size_t words) {
    for (std::size_t i = 0; i < words; ++i) {
        popWord();
    }
}

void lama::interpreter::BytecodeInterpreter::pushValue(lama::interpreter::runtime::Value value) {
    pushWord(value.getRawWord());
}

void lama::interpreter::BytecodeInterpreter::pushFrame(const lama::interpreter::CallstackFrame &frame) {
    interpreterAssert(callstack_.size() < callstack_.capacity, "callstack exhausted");
    callstack_.push(frame);
}

void lama::interpreter::BytecodeInterpreter::pushFrame(lama::interpreter::CallstackFrame&& frame) {
    interpreterAssert(callstack_.size() < callstack_.capacity, "callstack exhausted");
    callstack_.push(std::move(frame));
}

lama::interpreter::CallstackFrame lama::interpreter::BytecodeInterpreter::peekFrame() const {
    interpreterAssert(callstack_.nonEmpty(), "callstack is empty");
    return callstack_.peek();
}

lama::interpreter::CallstackFrame lama::interpreter::BytecodeInterpreter::popFrame() {
    interpreterAssert(callstack_.nonEmpty(), "callstack is empty");
    return callstack_.pop();
}

lama::runtime::Word* lama::interpreter::BytecodeInterpreter::getGlobalValueAddress(std::int32_t i) {
    checkGlobalValueIndex(i);

    return getGlobalsStartAddress() + i;
}

const lama::runtime::Word* lama::interpreter::BytecodeInterpreter::getGlobalValueAddress(std::int32_t i) const {
    checkGlobalValueIndex(i);

    return getGlobalsStartAddress() + i;
}

void lama::interpreter::BytecodeInterpreter::setGlobalValue(std::int32_t i, lama::runtime::Word value) {
    *getGlobalValueAddress(i) = value;
}

lama::runtime::Word lama::interpreter::BytecodeInterpreter::getGlobalValue(std::int32_t i) const {
    return *getGlobalValueAddress(i);
}

void lama::interpreter::BytecodeInterpreter::interpreterAssert(bool condition, std::string_view message) const {
    if (!condition) {
        ::failure(
            const_cast<char *>("internal error (file: %s, code offset: %" PRIdAI "): %s\n"),
            bytecodeFile_->getFilePath().data(),
            getInstructionStartOffset(),
            message.data()
         );
    }
}

void lama::interpreter::BytecodeInterpreter::executeArithBinop(lama::bytecode::InstructionOpCode opcode) {
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

void lama::interpreter::BytecodeInterpreter::executeComparisonBinop(lama::bytecode::InstructionOpCode opcode) {
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

void lama::interpreter::BytecodeInterpreter::executeLogicalBinop(lama::bytecode::InstructionOpCode opcode) {
    using lama::bytecode::InstructionOpCode;

    const lama::runtime::native_int_t y = popIntValue().getNativeInt();
    const lama::runtime::native_int_t x = popIntValue().getNativeInt();

    bool flag;

    switch (opcode) {
        case InstructionOpCode::BINOP_AND:
            flag = x != 0 ? y != 0 : false;
            break;
        case InstructionOpCode::BINOP_OR:
            flag = x == 0 ? y != 0 : true;
            break;
        default:
            break;
    }

    pushValue(flag);
}

void lama::interpreter::BytecodeInterpreter::executeBinop(lama::bytecode::InstructionOpCode opcode) {
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

    const char *opStrings[] = {
        "+", "-", "*", "/", "%",
        "<", "<=", ">", ">=", "==", "!=",
        "&&", "!!",
    };

    DEBUG_TRY(std::cout << "BINOP\t" << opStrings[static_cast<unsigned int>(opcode) - 0x1] << '\n');
}

void lama::interpreter::BytecodeInterpreter::executeConst() {
    const std::int32_t constVal = fetchInt32();

    pushValue(lama::runtime::native_int_t{constVal});

    DEBUG_TRY(std::cout << "CONST\t" << constVal << '\n');
}

void lama::interpreter::BytecodeInterpreter::executeString() {
    const std::int32_t strPos = fetchInt32();
    std::string_view strview = getString(strPos);
    const void * strTableEntity = strview.data();
    const void * const str = Bstring(reinterpret_cast<aint *>(&(strTableEntity)));

    pushValue(std::string_view{static_cast<const char *>(str)});

    DEBUG_TRY(std::cout << "STRING\t\"" << strview << "\"\n");
}

void lama::interpreter::BytecodeInterpreter::executeSexp() {
    const std::int32_t sexpTagPos = fetchInt32();
    std::string_view sexpTagStr = getString(sexpTagPos);
    const lama::runtime::native_uint_t tagHash = ::LtagHash(const_cast<char *>(sexpTagStr.data()));
    pushWord(lama::runtime::Word{tagHash});

    const std::int32_t n = fetchInt32();
    checkNonNegative(n, "sexp members count must not be negative");
    lama::runtime::native_int_t *arrayPtr = reinterpret_cast<lama::runtime::native_int_t *>(peekWordAddress(n + 1));;

    lama::runtime::native_uint_t boxedMembers = getBoxedIntAsUInt(n + 1);
    lama::runtime::Word sexpPtr{reinterpret_cast<lama::runtime::native_uint_t>(::Bsexp(arrayPtr, boxedMembers))};

    popWords(n + 1);
    pushWord(sexpPtr);

    DEBUG_TRY(std::cout << "SEXP\t\"" << sexpTagStr << "\"\t" << n << "\n");
}

void lama::interpreter::BytecodeInterpreter::executeSti() {
    const lama::runtime::Word value = popWord();
    void *valueAsPtr = reinterpret_cast<void *>(getNativeUIntRepresentation(value));

    const lama::interpreter::runtime::Value dst = popValue();
    interpreterAssert(!dst.isInt(), "expected a variable reference");

    lama::runtime::Word *dstPtr = reinterpret_cast<lama::runtime::Word *>(getNativeUIntRepresentation(dst.getRawWord()));

    ::Bsta(dstPtr, 0, valueAsPtr); // if snd is boxed (i. e. last bit is 0), then will act as STI

    pushWord(value);

    DEBUG_TRY(std::cout << "STI\n");
}

void lama::interpreter::BytecodeInterpreter::executeSta() {
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

    DEBUG_TRY(std::cout << "STA\n");
}

void lama::interpreter::BytecodeInterpreter::executeJmp() {
    const std::int32_t jmpAddress = fetchInt32();

    checkCodeOffset(jmpAddress);
    setIp(jmpAddress);

    DEBUG_TRY(std::cout << "JMP\t0x"
              << std::hex << jmpAddress
              << std::dec << '\n');
}

void lama::interpreter::BytecodeInterpreter::executeEnd() {
    // bool endReached = callstack_.size() <= 1;

    // if (!endReached) {
        doReturnFromFunction();
    // }

    DEBUG_TRY(std::cout << "END\n");

    // if (endReached) {
        // std::cout << "That's all folks!" << '\n';

        // exit(0);
    // }
}

void lama::interpreter::BytecodeInterpreter::executeRet() {
    // bool endReached = callstack_.size() <= 1;

    // if (!endReached) {
        doReturnFromFunction();
    // }

    DEBUG_TRY(std::cout << "RET\n");
}

void lama::interpreter::BytecodeInterpreter::checkNonNegative(std::int32_t value, std::string_view message) const {
    interpreterAssert(value >= 0, message);
}

void lama::interpreter::BytecodeInterpreter::checkCodeOffset(std::int32_t offset) const {
    checkNonNegative(offset, "code offset must not be negative");
    interpreterAssert(offset < bytecodeFile_->getCodeSize(), "code offset out of range");
}

void lama::interpreter::BytecodeInterpreter::checkGlobalValueIndex(std::int32_t globalValueIndex) const {
    checkNonNegative(globalValueIndex, "global value index must not be negative");
    interpreterAssert(globalValueIndex < bytecodeFile_->getGlobalAreaSize(), "global value index out of range");
}

void lama::interpreter::BytecodeInterpreter::checkLocalValueIndex(lama::interpreter::CallstackFrame frame, std::int32_t localValueIndex) const {
    checkNonNegative(localValueIndex, "local value index must not be negative");
    interpreterAssert(localValueIndex < frame.getLocalsCount(), "local value index out of range");
}

void lama::interpreter::BytecodeInterpreter::checkArgumentValueIndex(lama::interpreter::CallstackFrame frame, std::int32_t argumentValueIndex) const {
    checkNonNegative(argumentValueIndex, "argument value index must not be negative");
    interpreterAssert(argumentValueIndex < frame.getArgumentsCount(), "argument value index out of range");
}

void lama::interpreter::BytecodeInterpreter::checkCapturedValueIndex(lama::interpreter::CallstackFrame frame, std::int32_t capturedValueIndex) const {
    interpreterAssert(frame.hasCaptures(), "function cannot use captured values");
    checkNonNegative(capturedValueIndex, "captured value index must not be negative");
    interpreterAssert(capturedValueIndex < frame.getCapturesCount(), "captured value index out of range");
}

void lama::interpreter::BytecodeInterpreter::doReturnFromFunction() {
    const CallstackFrame currentFrame = popFrame();

    const lama::runtime::Word result = popWord();
    const std::int32_t retIp = lama::interpreter::runtime::Value{*currentFrame.getFrameBase()}.getNativeInt();

    popWords(std::size_t{currentFrame.getLocalsCount()});
    popWord(); // retIp
    popWords(std::size_t{currentFrame.getArgumentsCount()});

    if (currentFrame.hasClosure()) {
        popWord(); // closure
    }

    pushWord(result);
    setIp(retIp);
}

void lama::interpreter::BytecodeInterpreter::executeDrop() {
    popWord();

    DEBUG_TRY(std::cout << "DROP\n");
}

void lama::interpreter::BytecodeInterpreter::executeDup() {
    lama::runtime::Word top = peekWord();
    pushWord(top);

    DEBUG_TRY(std::cout << "DUP\n");
}

void lama::interpreter::BytecodeInterpreter::executeSwap() {
    lama::runtime::Word fst = popWord();
    lama::runtime::Word snd = popWord();

    pushWord(fst);
    pushWord(snd);

    DEBUG_TRY(std::cout << "SWAP\n");
}

void lama::interpreter::BytecodeInterpreter::executeElem() {
    const lama::runtime::native_uint_t boxedIndex = getNativeUIntRepresentation(popWord());
    const std::uintptr_t ptrval = getNativeUIntRepresentation(popWord());

    const lama::runtime::native_uint_t elem = reinterpret_cast<std::uintptr_t>(Belem(reinterpret_cast<void *>(ptrval), boxedIndex));
    pushWord(lama::runtime::Word{elem});

    DEBUG_TRY(std::cout << "ELEM\n");
}

void lama::interpreter::BytecodeInterpreter::executeLoadGlobalValue() {
    const std::int32_t globalIndex = fetchInt32();

    const lama::runtime::Word globalValue = getGlobalValue(globalIndex);

    pushWord(globalValue);

    DEBUG_TRY(std::cout << "LD\tG(" << globalIndex << ")\n");
}

void lama::interpreter::BytecodeInterpreter::executeLoadLocalValue() {
    const std::int32_t localIndex = fetchInt32();

    const CallstackFrame currentFrame = peekFrame();
    checkLocalValueIndex(currentFrame, localIndex);

    const lama::runtime::Word localValue = currentFrame.getLocalValue(localIndex);

    pushWord(localValue);

    DEBUG_TRY(std::cout << "LD\tL(" << localIndex << ")\n");
}

void lama::interpreter::BytecodeInterpreter::executeLoadArgumentValue() {
    const std::int32_t argIndex = fetchInt32();

    const CallstackFrame currentFrame = peekFrame();
    checkArgumentValueIndex(currentFrame, argIndex);

    const lama::runtime::Word argValue = currentFrame.getArgumentValue(argIndex);

    pushWord(argValue);

    DEBUG_TRY(std::cout << "LD\tA(" << argIndex << ")\n");
}

void lama::interpreter::BytecodeInterpreter::executeLoadCapturedValue() {
    const std::int32_t capturedValIndex = fetchInt32();

    const CallstackFrame currentFrame = peekFrame();

    checkCapturedValueIndex(currentFrame, capturedValIndex);
    const lama::runtime::Word capturedValue = currentFrame.getCapturedValue(capturedValIndex);

    pushWord(capturedValue);

    DEBUG_TRY(std::cout << "LD\tC(" << capturedValIndex << ")\n");
}

void lama::interpreter::BytecodeInterpreter::executeLoadGlobalValueAddress() {
    const std::int32_t globalValIndex = fetchInt32();

    lama::runtime::Word *globalValPtr = getGlobalValueAddress(globalValIndex);

    pushValue(globalValPtr);

    DEBUG_TRY(std::cout << "LDA\tG(" << globalValIndex << ")\n");
}

void lama::interpreter::BytecodeInterpreter::executeLoadLocalValueAddress() {
    const std::int32_t localValIndex = fetchInt32();

    CallstackFrame currentFrame = peekFrame();

    checkLocalValueIndex(currentFrame, localValIndex);
    lama::runtime::Word* localValPtr = currentFrame.getLocalValueAddress(localValIndex);

    pushValue(localValPtr);

    DEBUG_TRY(std::cout << "LDA\tL(" << localValIndex << ")\n");
}

void lama::interpreter::BytecodeInterpreter::executeLoadArgumentValueAddress() {
    const std::int32_t argIndex = fetchInt32();

    CallstackFrame currentFrame = peekFrame();

    checkArgumentValueIndex(currentFrame, argIndex);
    lama::runtime::Word* argPtr = currentFrame.getArgumentValueAddress(argIndex);

    pushValue(argPtr);

    DEBUG_TRY(std::cout << "LDA\tA(" << argIndex << ")\n");
}

void lama::interpreter::BytecodeInterpreter::executeLoadCapturedValueAddress() {
    const std::int32_t capturedValIndex = fetchInt32();

    const CallstackFrame currentFrame = peekFrame();

    checkCapturedValueIndex(currentFrame, capturedValIndex);
    const lama::runtime::Word *capturedValuePtr = currentFrame.getCapturedValueAddress(capturedValIndex);

    pushValue(capturedValuePtr);

    DEBUG_TRY(std::cout << "LDA\tC(" << capturedValIndex << ")\n");
}

void lama::interpreter::BytecodeInterpreter::executeStoreGlobalValue() {
    const std::int32_t globalIndex = fetchInt32();
    const lama::runtime::Word value = popWord();

    setGlobalValue(globalIndex, value);

    pushWord(value);

    DEBUG_TRY(std::cout << "ST\tG(" << globalIndex << ")\n");
}

void lama::interpreter::BytecodeInterpreter::executeStoreLocalValue() {
    const std::int32_t localIndex = fetchInt32();
    const lama::runtime::Word value = popWord();

    CallstackFrame currentFrame = peekFrame();

    checkLocalValueIndex(currentFrame, localIndex);
    currentFrame.setLocalValue(localIndex, value);

    pushWord(value);

    DEBUG_TRY(std::cout << "ST\tL(" << localIndex << ")\n");
}

void lama::interpreter::BytecodeInterpreter::executeStoreArgumentValue() {
    const std::int32_t argumentIndex = fetchInt32();
    const lama::runtime::Word value = popWord();

    CallstackFrame currentFrame = peekFrame();

    checkArgumentValueIndex(currentFrame, argumentIndex);
    currentFrame.setArgumentValue(argumentIndex, value);

    pushWord(value);

    DEBUG_TRY(std::cout << "ST\tA(" << argumentIndex << ")\n");
}

void lama::interpreter::BytecodeInterpreter::executeStoreCapturedValue() {
    const std::int32_t capturedValIndex = fetchInt32();
    const lama::runtime::Word value = popWord();

    CallstackFrame currentFrame = peekFrame();

    checkCapturedValueIndex(currentFrame, capturedValIndex);
    currentFrame.setCapturedValue(capturedValIndex, value);

    pushWord(value);

    DEBUG_TRY(std::cout << "ST\tC(" << capturedValIndex << ")\n");
}

void lama::interpreter::BytecodeInterpreter::executeConditionalJmpIfZero() {
    const std::int32_t nextIp = fetchInt32();
    checkCodeOffset(nextIp);

    const lama::runtime::native_int_t val = popIntValue().getNativeInt();

    if (val == 0) {
        setIp(nextIp);
    }

    DEBUG_TRY(std::cout << "CJMPz\t"
              << std::hex << std::showbase << nextIp
              << std::dec << '\n');
}

void lama::interpreter::BytecodeInterpreter::executeConditionalJmpIfNotZero() {
    const std::int32_t nextIp = fetchInt32();
    checkCodeOffset(nextIp);

    const lama::runtime::native_int_t val = popIntValue().getNativeInt();

    if (val != 0) {
        setIp(nextIp);
    }

    DEBUG_TRY(std::cout << "CJMPnz\t"
              << std::hex << std::showbase << nextIp
              << std::dec << '\n');
}

void lama::interpreter::BytecodeInterpreter::executeBegin() {
    const std::int32_t argsNum = fetchInt32();
    checkNonNegative(argsNum, "arguments number must not be negative");

    const std::int32_t localsNum = fetchInt32();
    checkNonNegative(argsNum, "locals number must not be negative");

    processFunctionBegin(argsNum, localsNum, false);

    DEBUG_TRY(std::cout << "BEGIN\t" << argsNum << "\t" << localsNum << '\n');
}

void lama::interpreter::BytecodeInterpreter::executeClosureBegin() {
    const std::int32_t argsNum = fetchInt32();
    checkNonNegative(argsNum, "arguments number must not be negative");

    const std::int32_t localsNum = fetchInt32();
    checkNonNegative(argsNum, "locals number must not be negative");

    processFunctionBegin(argsNum, localsNum, true);

    DEBUG_TRY(std::cout << "CBEGIN\t" << argsNum << "\t" << localsNum << '\n');
}

void lama::interpreter::BytecodeInterpreter::processFunctionBegin(std::int32_t argsNum, std::int32_t localsNum, bool hasCaptures) {
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

void lama::interpreter::BytecodeInterpreter::executeClosure() {
    const std::int32_t locationAddress = fetchInt32();
    checkCodeOffset(locationAddress);

    const std::int32_t argsNum = fetchInt32();
    checkNonNegative(argsNum, "arguments number must not be negative");

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
        }

        pushWord(w);
    }

    lama::runtime::native_int_t *ptrval = reinterpret_cast<lama::runtime::native_int_t *>(peekWordAddress(argsNum + 1));

    void *closurePtr = ::Bclosure(ptrval, getBoxedIntAsUInt(argsNum));

    popWords(argsNum + 1);

    pushValue(closurePtr);

    DEBUG_TRY(std::cout << "CLOSURE\t"
              << std::hex << std::showbase
              << locationAddress << std::dec);

    std::int32_t lookupIp = captureIp;

    for (std::int32_t i = 0; i < argsNum; ++i) {
        CaptureType captureType{std::to_integer<unsigned char>(lookupByte(lookupIp))};
        lookupIp += sizeof(std::byte);

        const std::int32_t index = lookupInt32(lookupIp);
        lookupIp += sizeof(std::int32_t);

        switch (captureType) {
            case CaptureType::GLOBAL:
                DEBUG_TRY(std::cout << "\tG(" << index << ")");
                break;
            case CaptureType::LOCAL:
                DEBUG_TRY(std::cout << "\tL(" << index << ")");
                break;
            case CaptureType::ARGUMENT:
                DEBUG_TRY(std::cout << "\tA(" << index << ")");
                break;
            case CaptureType::CAPTURE:
                DEBUG_TRY(std::cout << "\tC(" << index << ")");
                break;
        }
    }

    DEBUG_TRY(std::cout << '\n');
}

void lama::interpreter::BytecodeInterpreter::executeCallClosure() {
    const std::int32_t argsNum = fetchInt32();
    checkNonNegative(argsNum, "arguments number must not be negative");

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

    DEBUG_TRY(std::cout << "CALLC\t" << argsNum << '\n');
}

void lama::interpreter::BytecodeInterpreter::executeCall() {
    const std::int32_t locationAddress = fetchInt32();
    checkCodeOffset(locationAddress);
    lama::bytecode::InstructionOpCode startOp = lookupInstrOpCode(locationAddress);
    interpreterAssert(
        startOp == lama::bytecode::InstructionOpCode::BEGIN,
        "CALL should go to BEGIN instruction"
    );

    const std::int32_t argsNum = fetchInt32();
    checkNonNegative(argsNum, "arguments number must not be negative");

    pushValue(lama::runtime::native_int_t{getIp()});

    setIp(locationAddress);
    isClosureCalled_ = false;

    DEBUG_TRY(std::cout << std::hex
              << "CALL\t0x" << locationAddress << "\t" << argsNum
              << std::dec << '\n');
}

void lama::interpreter::BytecodeInterpreter::executeTag() {
    const std::int32_t sexpTagPos = fetchInt32();
    const std::string_view sexpTagStr = getString(sexpTagPos);
    const lama::runtime::native_int_t tagHash = ::LtagHash(const_cast<char *>(sexpTagStr.data()));

    const std::int32_t n = fetchInt32();
    checkNonNegative(n, "sexp members count must not be negative");
    const lama::runtime::native_uint_t boxedMembers = getBoxedIntAsUInt(n);

    const lama::runtime::native_uint_t ptrval = getNativeUIntRepresentation(popWord());

    pushWord(lama::runtime::Word(::Btag(reinterpret_cast<void *>(ptrval), tagHash, boxedMembers)));

    DEBUG_TRY(std::cout << "TAG\t\"" << sexpTagStr << "\"\t" << n << '\n');
}

void lama::interpreter::BytecodeInterpreter::executeArray() {
    const std::int32_t n = fetchInt32();
    checkNonNegative(n, "array length must not be negative");

    const lama::interpreter::runtime::Value boxedLenVal = lama::runtime::native_int_t{n};

    const void *ptr = reinterpret_cast<const void *>(getNativeUIntRepresentation(peekWord()));

    const std::uintptr_t ptrval = getNativeUIntRepresentation(popWord());

    pushWord(lama::runtime::Word(::Barray_patt(reinterpret_cast<void *>(ptrval), getNativeUIntRepresentation(boxedLenVal.getRawWord()))));

    DEBUG_TRY(std::cout << std::dec
              << "ARRAY\t" << n << '\n');
}

void lama::interpreter::BytecodeInterpreter::executeFail() {
    const std::int32_t lineNum = fetchInt32();
    interpreterAssert(lineNum >= 1, "line number must be greater than zero");
    const lama::runtime::native_uint_t boxedLineNum = getBoxedIntAsUInt(lineNum);

    const std::int32_t colNum = fetchInt32();
    interpreterAssert(colNum >= 1, "column number must be greater than zero");
    const lama::runtime::native_uint_t boxedColNum = getBoxedIntAsUInt(colNum);

    const lama::runtime::native_uint_t value = getNativeUIntRepresentation(popWord());

    ::Bmatch_failure(reinterpret_cast<void *>(value), const_cast<char *>("<bytecode_file>"), boxedLineNum, boxedColNum);

    DEBUG_TRY(std::cout << "FAIL\t" << lineNum << "\t" << colNum << '\n');
}

void lama::interpreter::BytecodeInterpreter::executeLine() {
    const std::int32_t lineNum = fetchInt32();

    DEBUG_TRY(std::cout << "LINE\t" << lineNum << '\n');
}

void lama::interpreter::BytecodeInterpreter::executePattStr() {
    const std::uintptr_t y = getNativeUIntRepresentation(popWord());
    void *str1 = reinterpret_cast<void *>(y);

    const std::uintptr_t x = getNativeUIntRepresentation(popWord());
    void *str0 = reinterpret_cast<void *>(x);

    pushWord(lama::runtime::Word(::Bstring_patt(str0, str1)));

    DEBUG_TRY(std::cout << "PATT\t=str\n");
}

void lama::interpreter::BytecodeInterpreter::executePattString() {
    const std::uintptr_t ptrval = getNativeUIntRepresentation(popWord());

    pushWord(lama::runtime::Word(::Bstring_tag_patt(reinterpret_cast<void *>(ptrval))));

    DEBUG_TRY(std::cout << "PATT\t#string\n");
}

void lama::interpreter::BytecodeInterpreter::executePattArray() {
    const std::uintptr_t ptrval = getNativeUIntRepresentation(popWord());

    pushWord(lama::runtime::Word(::Barray_tag_patt(reinterpret_cast<void *>(ptrval))));

    DEBUG_TRY(std::cout << "PATT\t#array\n");
}

void lama::interpreter::BytecodeInterpreter::executePattSexp() {
    const std::uintptr_t ptrval = getNativeUIntRepresentation(popWord());

    pushWord(lama::runtime::Word(::Bsexp_tag_patt(reinterpret_cast<void *>(ptrval))));

    DEBUG_TRY(std::cout << "PATT\t#sexp\n");
}

void lama::interpreter::BytecodeInterpreter::executePattRef() {
    const std::uintptr_t ptrval = getNativeUIntRepresentation(popWord());

    pushWord(lama::runtime::Word(::Bboxed_patt(reinterpret_cast<void *>(ptrval))));

    DEBUG_TRY(std::cout << "PATT\t#ref\n");
}

void lama::interpreter::BytecodeInterpreter::executePattVal() {
    const std::uintptr_t ptrval = getNativeUIntRepresentation(popWord());

    pushWord(lama::runtime::Word(::Bunboxed_patt(reinterpret_cast<void *>(ptrval))));

    DEBUG_TRY(std::cout << "PATT\t#val\n");
}

void lama::interpreter::BytecodeInterpreter::executePattFun() {
    const std::uintptr_t ptrval = getNativeUIntRepresentation(popWord());

    pushWord(lama::runtime::Word(::Bclosure_tag_patt(reinterpret_cast<void *>(ptrval))));

    DEBUG_TRY(std::cout << "PATT\t#fun\n");
}

void lama::interpreter::BytecodeInterpreter::executeCallLread() {
    const lama::runtime::Word w{static_cast<lama::runtime::native_uint_t>(::Lread())};

    pushWord(w);

    DEBUG_TRY(std::cout << "CALL\tLread\n");
}

void lama::interpreter::BytecodeInterpreter::executeCallLwrite() {
    const lama::interpreter::runtime::Value val = popIntValue();

    ::Lwrite(getNativeUIntRepresentation(val.getRawWord()));

    pushWord(lama::runtime::Word{});

    DEBUG_TRY(std::cout << "CALL\tLwrite\n");
}

void lama::interpreter::BytecodeInterpreter::executeCallLlength() {
    const std::uintptr_t ptrval = getNativeUIntRepresentation(popWord());

    pushWord(lama::runtime::Word(::Llength(reinterpret_cast<void *>(ptrval))));

    DEBUG_TRY(std::cout << "CALL\tLlength\n");
}

void lama::interpreter::BytecodeInterpreter::executeCallLstring() {
    std::uintptr_t ptrval = getNativeUIntRepresentation(popWord());

    pushValue(::Lstring(reinterpret_cast<lama::runtime::native_int_t *>(&ptrval)));

    DEBUG_TRY(std::cout << "CALL\tLstring\n");
}

void lama::interpreter::BytecodeInterpreter::executeCallBarray() {
    const std::int32_t n = fetchInt32();
    lama::runtime::native_uint_t boxedLen = getBoxedIntAsUInt(n);

    lama::runtime::native_int_t *arrayPtr = reinterpret_cast<lama::runtime::native_int_t *>(peekWordAddress(n));

    const void *allocatedArray = Barray(arrayPtr, boxedLen);

    popWords(n);

    pushWord(lama::runtime::Word{reinterpret_cast<std::uintptr_t>(allocatedArray)});

    DEBUG_TRY(std::cout << std::dec << "CALL\tBarray " << n << "\n");
}

void lama::interpreter::BytecodeInterpreter::interpret() {
    if (endReached_) {
        return;
    }

    using lama::bytecode::InstructionOpCode;

    setInstructionStartOffset(getIp());

    DEBUG_TRY(std::cerr << "[interpreter-debug]: "
              << "ip = 0x" << std::hex << getInstructionStartOffset()
              << ", op = 0x" << std::to_integer<unsigned int>(lookupByte())
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
            interpreterAssert(false, "Invalid instruction");
            // {
            //     std::cout << "instr byte: "
            //               << std::hex << std::showbase << static_cast<unsigned int>(op)
            //               << std::dec
            //               << "\noffset: " << ip_ << '\n';
            //
            //     for (std::size_t i = 1; i < stack_.size(); ++i) {
            //         std::cout << std::hex
            //                   << "stack[0x" << i << "] = 0x" << getNativeUIntRepresentation(peekWord(i))
            //                   << std::dec << '\n';
            //     }
            // }
            //
            // exit(1);
            break;
    }
}

void lama::interpreter::BytecodeInterpreter::runInterpreterLoop() {
    while (!endReached_) {
        interpret();
    }
}
