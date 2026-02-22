#ifndef INTERPRETER_VERIFIER_HPP
#define INTERPRETER_VERIFIER_HPP

#include <cstdint>
#include <vector>

#include "interpreter.hpp"
#include "lama_runtime.hpp"

#include "../bytecode/source_file.hpp"

namespace lama::verifier {
struct VerifierAbstractState {
    lama::bytecode::offset_t functionBegin;
    std::uint16_t argsCount;
    lama::bytecode::offset_t startIp;
    std::uint16_t localsCount;
    std::uint16_t stackSize;
    std::uint16_t maxStackSize;
    std::uint16_t callstackSize;
};

class StackSize {
public:
    StackSize()
        : defined_(false)
        , stackSize_(0) {

    }

    StackSize(std::size_t stackSize)
        : defined_(true)
        , stackSize_(stackSize) {

    }

    bool isDefined() const {
        return defined_;
    }

    void setStackSize(std::size_t stackSize) {
        stackSize_ = stackSize;
        defined_ = true;
    }

    std::size_t getStackSize() const {
        return stackSize_;
    }
private:
    bool defined_;
    std::size_t stackSize_;
};

class BytecodeVerifier {
public:
    BytecodeVerifier(lama::bytecode::BytecodeFile *bytecodeFile);

    bool verifyBytecode();
    bool verifyInstruction();

    lama::bytecode::offset_t getIp() const {
        return ip_;
    }

    lama::bytecode::offset_t getInstructionStartOffset() const {
        return instructionStartOffset_;
    }
protected:
    std::byte lookupByte(lama::bytecode::offset_t pos) const {
        verifierAssert(pos < bytecodeFile_->getCodeSize(), "code offset out of range");

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

        verifierAssert(pos + sizeof(val) <= bytecodeFile_->getCodeSize(), "code offset out of range");

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

    void pushWords(std::size_t words) {
        checkStackOverflow(words);
        currentState_.stackSize += words;
    }

    void pushWord() {
        pushWords(1);
    }

    void popWords(std::size_t words) {
        checkStackUnderflow(words);
        currentState_.stackSize -= words;
    }

    void popWord() {
        popWords(1);
    }

    void pushFrame() {
        verifierAssert(currentState_.callstackSize < lama::interpreter::CALLSTACK_CAPACITY, "callstack exhausted");
        currentState_.callstackSize++;
    }

    void popFrame() {
        verifierAssert(currentState_.callstackSize > 0, "callstack is empty");
        --currentState_.callstackSize;
    }

    void pushState(const VerifierAbstractState &state) {
        worklist_.push_back(state);
    }

    VerifierAbstractState& peekState(std::size_t offset = 1) {
        return worklist_[worklist_.size() - offset];
    }

    const VerifierAbstractState& peekState(std::size_t offset = 1) const {
        return worklist_[worklist_.size() - offset];
    }

    VerifierAbstractState popState() {
        const VerifierAbstractState state = peekState();
        worklist_.pop_back();

        return state;
    }

    void verifyBinop();

    void verifyConst();
    void verifyString();
    void verifySexp();

    void verifySti();

    void verifyJmp();

    void verifyReturn();

    void verifyDrop();
    void verifyDup();
    void verifySwap();

    void verifyElem();

    void verifyGlobalLoad();
    void verifyLocalLoad();
    void verifyArgumentLoad();
    void verifyCapturedLoad();

    void verifyGlobalStore();
    void verifyLocalStore();
    void verifyArgumentStore();
    void verifyCapturedStore();

    void verifyConditionalJmp();

    void verifyBegin();
    void verifyClosureBegin();

    void verifyClosure();
    void verifyCallClosure();
    void verifyCall();

    void verifyTag();
    void verifyArray();

    void verifyFail();
    void verifyLine();

    void verifyPattStr();
    void verifyPatt();

    void verifyCallLread();
    void verifyCallLwrite();
    void verifyCallLlength();
    void verifyCallLstring();
    void verifyCallBarray();
private:
    lama::bytecode::offset_t ip_;
    lama::bytecode::offset_t instructionStartOffset_;
    std::vector<StackSize> stackSizes_;
    VerifierAbstractState currentState_;
    std::vector<VerifierAbstractState> worklist_;
    bool pushNextState_;
    lama::bytecode::BytecodeFile *bytecodeFile_;

    void setIp(lama::bytecode::offset_t newIp) {
        ip_ = newIp;
    }

    void advanceIp(lama::bytecode::offset_t offset = 1) {
        ip_ += offset;
    }

    void setInstructionStartOffset(lama::bytecode::offset_t offset) {
        instructionStartOffset_ = offset;
    }

    void saveStackSizeInfo(bytecode::offset_t offset, std::uint16_t stackSize, std::uint16_t localsNum);

    void checkGlobalValueIndex(lama::bytecode::offset_t globalValueIndex) const {
        verifierAssert(globalValueIndex < bytecodeFile_->getGlobalAreaSize(), "global value index out of range");
    }

    void checkLocalValueIndex(const VerifierAbstractState &state, std::uint32_t index) const {
        verifierAssert(index < state.localsCount, "local value index out of range");
    }

    void checkLocalsNumber(std::int32_t localsNum) const {
        checkNonNegative(localsNum, "locals number must not be negative");
    }

    void checkArgumentValueIndex(const VerifierAbstractState &state, std::uint32_t index) {
        verifierAssert(index < state.argsCount, "argument value index out of range");
    }

    void checkArgumentsNumber(std::int32_t argsNum) const {
        checkNonNegative(argsNum, "arguments number must not be negative");
    }

    void checkCapturedValueIndex(std::uint32_t index) {
        checkNonNegative(index, "captured value index out of range");
    }

    void checkStringIndex(std::uint32_t index) const {
        verifierAssert(index < bytecodeFile_->getStringTableSize(), "string table index is out of range");
    }

    void checkNonNegative(std::int32_t val, std::string_view message) const {
        verifierAssert(val >= 0, message);
    }

    void checkMin(std::int32_t val, std::int32_t min, std::string_view message) {
        verifierAssert(val >= min, message);
    }

    void checkStackUnderflow(std::size_t minSize = 1) const {
        verifierAssert(currentState_.stackSize >= minSize, "operand stack is empty");
    }

    void checkStackOverflow(std::size_t words = 1) const {
        verifierAssert(currentState_.stackSize + words < lama::interpreter::OP_STACK_CAPACITY, "operand stack exhausted");
    }

    void checkCodeOffset(lama::bytecode::offset_t offset) const {
        verifierAssert(offset < bytecodeFile_->getCodeSize(), "code offset out of range");
    }

    void verifierAssert(bool condition, std::string_view message) const {
        if (!condition) {
            ::failure(
                const_cast<char *>("verification error (file: %s, code offset: %" PRIdAI "): %s\n"),
                bytecodeFile_->getFilePath().data(),
                getInstructionStartOffset(),
                message.data()
             );
        }
    }
};

bool verifyBytecodeFile(bytecode::BytecodeFile *file);
}

#endif
