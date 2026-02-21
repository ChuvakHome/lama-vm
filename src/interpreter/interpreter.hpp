#ifndef INTERPRETER_INTERPRETER_HPP
#define INTERPRETER_INTERPRETER_HPP

#include <cstdint>

#include "../bytecode/source_file.hpp"
#include "../bytecode/bytecode_instructions.hpp"
#include "interpreter_runtime.hpp"

#include "lama_runtime.hpp"

namespace lama::interpreter {
#ifndef LAMA_CALL_STACK_CAPACITY
#define LAMA_CALL_STACK_CAPACITY 0xffff
#endif

constexpr std::size_t CALLSTACK_CAPACITY = LAMA_CALL_STACK_CAPACITY;

class CallstackFrame final {
public:
    CallstackFrame() = default;

    CallstackFrame(lama::runtime::Word *frameBase,
        std::size_t
        argsCount,
        std::size_t localsCount,
        bool hasClosure,
        bool hasCaptures
    )   : frameBase_(frameBase)
        , argsCount_(argsCount)
        , localsCount_(localsCount)
        , hasClosure_(hasClosure)
        , hasCaptures_(hasCaptures) {

    }

    ~CallstackFrame() = default;

    lama::runtime::Word* getFrameBase() {
        return frameBase_;
    }

    const lama::runtime::Word* getFrameBase() const {
        return frameBase_;
    }

    lama::runtime::Word* getArgumentValueAddress(lama::bytecode::offset_t i) {
        return getArgumentsStartAddress() + i;
    }

    const lama::runtime::Word* getArgumentValueAddress(lama::bytecode::offset_t i) const {
        return getArgumentsStartAddress() + i;
    }

    void setArgumentValue(lama::bytecode::offset_t i, lama::runtime::Word value) {
        *getArgumentValueAddress(i) = value;
    }

    lama::runtime::Word getArgumentValue(lama::bytecode::offset_t i) const {
        return *getArgumentValueAddress(i);
    }

    lama::runtime::Word* getLocalValueAddress(lama::bytecode::offset_t i) {
        return getLocalsStartAddress() + i;
    }

    const lama::runtime::Word* getLocalValueAddress(lama::bytecode::offset_t i) const {
        return getLocalsStartAddress() + i;
    }

    void setLocalValue(lama::bytecode::offset_t i, lama::runtime::Word value) {
        *getLocalValueAddress(i) = value;
    }

    lama::runtime::Word getLocalValue(lama::bytecode::offset_t i) const {
        return *getLocalValueAddress(i);
    }

    lama::runtime::Word* getCapturedValueAddress(lama::bytecode::offset_t i);
    const lama::runtime::Word* getCapturedValueAddress(lama::bytecode::offset_t i) const;

    void setCapturedValue(lama::bytecode::offset_t i, lama::runtime::Word value) {
        *getCapturedValueAddress(i) = value;
    }

    lama::runtime::Word getCapturedValue(lama::bytecode::offset_t i) const {
        return *getCapturedValueAddress(i);
    }

    std::uint32_t getArgumentsCount() const {
        return argsCount_;
    }

    std::uint32_t getLocalsCount() const {
        return localsCount_;
    }

    std::uint32_t getCapturesCount() const;

    bool hasClosure() const {
        return hasClosure_;
    }

    bool hasCaptures() const {
        return hasCaptures_;
    }
private:
    lama::runtime::Word *frameBase_;
    std::size_t argsCount_;
    std::size_t localsCount_;
    bool hasClosure_;
    bool hasCaptures_;

    lama::runtime::Word* getLocalsStartAddress();
    const lama::runtime::Word* getLocalsStartAddress() const;

    lama::runtime::Word* getArgumentsStartAddress();
    const lama::runtime::Word* getArgumentsStartAddress() const;

    lama::runtime::Word* getCapturedContentAddress();
    const lama::runtime::Word* getCapturedContentAddress() const;
};

namespace utils {
class CallStack {
public:
    static constexpr std::size_t capacity = CALLSTACK_CAPACITY;

    CallStack(std::size_t initialSize = 0);
    CallStack(const CallStack &other) = delete;
    CallStack(CallStack&& other) = delete;
    ~CallStack() = default;

    void push(const CallstackFrame &value) {
        buffer_[topIndex_++] = value;
    }

    void push(CallstackFrame&& value) {
        buffer_[topIndex_++] = std::move(value);
    }

    CallstackFrame peek(std::size_t offset = 1) const {
        return *peekAddress();
    }

    CallstackFrame* peekAddress(std::size_t offset = 1) {
        return buffer_ + topIndex_ - offset;
    }

    const CallstackFrame* peekAddress(std::size_t offset = 1) const {
        return buffer_ + topIndex_ - offset;
    }

    CallstackFrame pop() {
        CallstackFrame top = peek();
        topIndex_--;

        return top;
    }

    std::size_t size() const {
        return topIndex_;
    }

    bool empty() const {
        return size() == 0;
    }

    bool nonEmpty() const {
        return size() != 0;
    }
private:
    CallstackFrame buffer_[CALLSTACK_CAPACITY];
    std::size_t topIndex_;
};
}

#ifndef LAMA_OP_STACK_CAPACITY
#define LAMA_OP_STACK_CAPACITY 0xf'ffff
#endif

constexpr std::size_t OP_STACK_CAPACITY = LAMA_OP_STACK_CAPACITY;

extern template class lama::interpreter::runtime::GcDataStack<lama::runtime::Word, OP_STACK_CAPACITY>;
using DataStack = lama::interpreter::runtime::GcDataStack<lama::runtime::Word, OP_STACK_CAPACITY>;

enum class VerificationMode {
    STATIC_VERIFICATION,
    DYNAMIC_VERIFICATION,
};

class BytecodeInterpreterState {
public:
    BytecodeInterpreterState(
        const lama::bytecode::BytecodeFile *bytecodeFile,
        VerificationMode mode = VerificationMode::DYNAMIC_VERIFICATION
    );

    ~BytecodeInterpreterState() {

    }

    lama::bytecode::offset_t getIp() const {
        return ip_;
    }

    lama::bytecode::offset_t getInstructionStartOffset() const {
        return instructionStartOffset_;
    }

    bool isEndReached() const {
        return endReached_;
    }

    void executeCurrentInstruction();
protected:
    std::byte lookupByte(lama::bytecode::offset_t pos) const {
        if (mode_ == VerificationMode::DYNAMIC_VERIFICATION) {
            interpreterAssert(pos < bytecodeFile_->getCodeSize(), "code offset out of range");
        }

        return bytecodeFile_->getCodeByte(pos);
    }

    std::byte lookupByte() const {
        return lookupByte(getIp());
    }

    lama::bytecode::InstructionOpCode lookupInstrOpCode(lama::bytecode::offset_t pos) const {
        return lama::bytecode::InstructionOpCode{static_cast<unsigned char>(lookupByte(pos))};
    }

    lama::bytecode::InstructionOpCode lookupInstrOpCode() const {
        return lookupInstrOpCode(getIp());
    }

    std::byte fetchByte() {
        std::byte val = lookupByte();
        advanceIp(sizeof(val));

        return val;
    }

    lama::bytecode::InstructionOpCode fetchInstrOpCode() {
        return lama::bytecode::InstructionOpCode{static_cast<unsigned char>(fetchByte())};
    }

    std::int32_t lookupInt32(lama::bytecode::offset_t pos) const {
        std::int32_t val;

        if (mode_ == VerificationMode::DYNAMIC_VERIFICATION) {
            interpreterAssert(pos + sizeof(val) <= bytecodeFile_->getCodeSize(), "code offset out of range");
        }

        bytecodeFile_->copyCodeBytes(static_cast<std::byte *>(static_cast<void *>(&val)), pos, sizeof(val));

        return val;
    }

    std::int32_t lookupInt32() const {
        return lookupInt32(getIp());
    }

    std::int32_t fetchInt32() {
        std::int32_t val = lookupInt32();
        advanceIp(sizeof(val));

        return val;
    }

    std::string_view getString(lama::bytecode::offset_t index) const {
        if (mode_ == VerificationMode::DYNAMIC_VERIFICATION) {
            interpreterAssert(index < bytecodeFile_->getStringTableSize(), "string table index is out of range");
        }

        return bytecodeFile_->getString(index);
    }

    void pushWord(lama::runtime::Word w) {
        if (mode_ == VerificationMode::DYNAMIC_VERIFICATION) {
            interpreterAssert(stack_.size() < stack_.capacity, "operand stack exhausted");
        }

        return stack_.push(w);
    }

    lama::runtime::Word peekWord(std::size_t offset = 1) const {
        if (mode_ == VerificationMode::DYNAMIC_VERIFICATION) {
            interpreterAssert(stack_.size() >= offset, "operand stack index overflow while peeking Lama Word");
        }

        return stack_.peek(offset);
    }

    lama::runtime::Word *peekWordAddress(std::size_t offset = 1) {
        if (mode_ == VerificationMode::DYNAMIC_VERIFICATION) {
            interpreterAssert(stack_.size() >= offset, "operand stack index overflow while peeking Lama Word addr");
        }

        return stack_.peekAddress(offset);
    }

    const lama::runtime::Word *peekWordAddress(std::size_t offset = 1) const {
        if (mode_ == VerificationMode::DYNAMIC_VERIFICATION) {
            interpreterAssert(stack_.size() >= offset, "operand stack index overflow while peeking Lama Word addr");
        }

        return stack_.peekAddress(offset);
    }

    lama::interpreter::runtime::Value peekValue(std::size_t offset = 1) const {
        if (mode_ == VerificationMode::DYNAMIC_VERIFICATION) {
            interpreterAssert(stack_.size() >= offset, "operand stack index overflow while peeking Lama Value");
        }

        return lama::interpreter::runtime::Value{peekWord(offset)};
    }

    lama::runtime::Word popWord() {
        if (mode_ == VerificationMode::DYNAMIC_VERIFICATION) {
            interpreterAssert(stack_.nonEmpty(), "operand stack is empty");
        }

        return stack_.pop();
    }

    lama::interpreter::runtime::Value popValue() {
        return lama::interpreter::runtime::Value{popWord()};
    }

    lama::interpreter::runtime::Value popIntValue(std::string_view message = "expected an integer") {
        const lama::interpreter::runtime::Value val = popValue();

        interpreterAssert(val.isInt(), message);

        return val;
    }

    lama::interpreter::runtime::Value popStringValue(std::string_view message = "expected a string") {
        const lama::interpreter::runtime::Value val = popValue();

        interpreterAssert(val.isString(), message);

        return val;
    }

    lama::interpreter::runtime::Value popArrayValue(std::string_view message = "expected an array") {
        const lama::interpreter::runtime::Value val = popValue();

        interpreterAssert(val.isArray(), message);

        return val;
    }

    lama::interpreter::runtime::Value popClosureValue(std::string_view message = "expected a closure") {
        const lama::interpreter::runtime::Value val = popValue();

        interpreterAssert(val.isClosure(), message);

        return val;
    }

    lama::interpreter::runtime::Value popSexpValue(std::string_view message = "expected a sexp") {
        const lama::interpreter::runtime::Value val = popValue();

        interpreterAssert(val.isSexp(), message);

        return val;
    }

    void popWords(std::size_t words) {
        for (; words > 0; --words) {
            popWord();
        }
    }

    void pushValue(lama::interpreter::runtime::Value value) {
        pushWord(value.getRawWord());
    }

    void pushFrame(const CallstackFrame &frame) {
        interpreterAssert(callstack_.size() < callstack_.capacity, "callstack exhausted");
        callstack_.push(frame);
    }

    void pushFrame(CallstackFrame&& frame) {
        interpreterAssert(callstack_.size() < callstack_.capacity, "callstack exhausted");
        callstack_.push(std::move(frame));
    }

    CallstackFrame peekFrame() const {
        interpreterAssert(callstack_.nonEmpty(), "callstack is empty");
        return callstack_.peek();
    }

    CallstackFrame popFrame() {
        const CallstackFrame top = peekFrame();
        callstack_.pop();

        return top;
    }

    lama::runtime::Word* getGlobalValueAddress(lama::bytecode::offset_t i) {
        checkGlobalValueIndex(i);

        return getGlobalsStartAddress() + i;
    }

    const lama::runtime::Word* getGlobalValueAddress(lama::bytecode::offset_t i) const {
        checkGlobalValueIndex(i);

        return getGlobalsStartAddress() + i;
    }

    void setGlobalValue(lama::bytecode::offset_t i, lama::runtime::Word value) {
        *getGlobalValueAddress(i) = value;
    }

    lama::runtime::Word getGlobalValue(lama::bytecode::offset_t i) const {
        return *getGlobalValueAddress(i);
    }

    void interpreterAssert(bool condition, std::string_view message) const {
        if (!condition) {
            ::failure(
                const_cast<char *>("internal error (file: %s, code offset: %" PRIdAI "): %s\n"),
                bytecodeFile_->getFilePath().data(),
                getInstructionStartOffset(),
                message.data()
             );
        }
    }

    void executeBinop(lama::bytecode::InstructionOpCode opcode);

    void executeConst();
    void executeString();
    void executeSexp();

    void executeSti();
    void executeSta();

    void executeJmp();

    void executeEnd();
    void executeRet();

    void executeDrop();
    void executeDup();
    void executeSwap();

    void executeElem();

    void executeLoadGlobalValue();
    void executeLoadLocalValue();
    void executeLoadArgumentValue();
    void executeLoadCapturedValue();

    void executeLoadGlobalValueAddress();
    void executeLoadLocalValueAddress();
    void executeLoadArgumentValueAddress();
    void executeLoadCapturedValueAddress();

    void executeStoreGlobalValue();
    void executeStoreLocalValue();
    void executeStoreArgumentValue();
    void executeStoreCapturedValue();

    void executeConditionalJmpIfZero();
    void executeConditionalJmpIfNotZero();

    void executeBegin();
    void executeClosureBegin();

    void processFunctionBegin(std::size_t argsNum, std::size_t localsNum, bool isClosure);

    void executeClosure();
    void executeCallClosure();
    void executeCall();
    void executeTag();
    void executeArray();
    void executeFail();
    void executeLine();

    void executePattStr();
    void executePattString();
    void executePattArray();
    void executePattSexp();
    void executePattRef();
    void executePattVal();
    void executePattFun();

    void executeCallLread();
    void executeCallLwrite();
    void executeCallLlength();
    void executeCallLstring();
    void executeCallBarray();
private:
    bool gcInitialized_;
    lama::bytecode::offset_t ip_;
    lama::bytecode::offset_t instructionStartOffset_;
    VerificationMode mode_;
    alignas(16) DataStack stack_;
    utils::CallStack callstack_;
    bool isClosureCalled_;
    bool endReached_;
    const lama::bytecode::BytecodeFile *bytecodeFile_;

    void setIp(lama::bytecode::offset_t newIp) {
        ip_ = newIp;
    }

    void advanceIp(lama::bytecode::offset_t offset = 1) {
        ip_ += offset;
    }

    void setInstructionStartOffset(lama::bytecode::offset_t offset) {
        instructionStartOffset_ = offset;
    }

    const lama::runtime::Word* getGlobalsStartAddress() const {
        return stack_.data();
    }

    lama::runtime::Word* getGlobalsStartAddress() {
        return stack_.data();
    }

    void executeArithBinop(lama::bytecode::InstructionOpCode opcode);
    void executeComparisonBinop(lama::bytecode::InstructionOpCode opcode);
    void executeLogicalBinop(lama::bytecode::InstructionOpCode opcode);

    void checkNonNegative(std::int32_t value, std::string_view message) const {
        interpreterAssert(value >= 0, message);
    }

    void checkCodeOffset(lama::bytecode::offset_t offset) const {
        if (mode_ == VerificationMode::DYNAMIC_VERIFICATION) {
            interpreterAssert(offset < bytecodeFile_->getCodeSize(), "code offset out of range");
        }
    }

    void checkGlobalValueIndex(lama::bytecode::offset_t globalValueIndex) const {
        if (mode_ == VerificationMode::DYNAMIC_VERIFICATION) {
            interpreterAssert(globalValueIndex < bytecodeFile_->getGlobalAreaSize(), "global value index out of range");
        }
    }

    void checkLocalValueIndex(CallstackFrame frame, lama::bytecode::offset_t localValueIndex) const {
        if (mode_ == VerificationMode::DYNAMIC_VERIFICATION) {
            interpreterAssert(localValueIndex < frame.getLocalsCount(), "local value index out of range");
        }
    }

    void checkArgumentValueIndex(CallstackFrame frame, lama::bytecode::offset_t argumentValueIndex) const {
        if (mode_ == VerificationMode::DYNAMIC_VERIFICATION) {
            interpreterAssert(argumentValueIndex < frame.getArgumentsCount(), "argument value index out of range");
        }
    }

    void checkCapturedValueIndex(CallstackFrame frame, lama::bytecode::offset_t capturedValueIndex) const {
        interpreterAssert(frame.hasCaptures(), "function cannot use captured values");
        interpreterAssert(capturedValueIndex < frame.getCapturesCount(), "captured value index out of range");
    }

    void doReturnFromFunction();
};

void interpretBytecodeFile(const bytecode::BytecodeFile *file, VerificationMode mode = VerificationMode::DYNAMIC_VERIFICATION);
}

#endif
