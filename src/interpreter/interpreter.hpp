#ifndef INTERPRETER_INTERPRETER_HPP
#define INTERPRETER_INTERPRETER_HPP

#include <cstdint>

#include "../bytecode/source_file.hpp"

#include "../bytecode/bytecode_instructions.hpp"

#include "interpreter_runtime.hpp"

#include "lama_runtime.hpp"

namespace lama::interpreter {
namespace utils {
template<class T, std::size_t StackCapacity>
class Stack {
public:
    static constexpr std::size_t capacity = StackCapacity;

    Stack(std::size_t initialSize = 0);
    Stack(const Stack &other) = delete;
    Stack(Stack&& other) = delete;
    ~Stack() = default;

    void push(const T &value);
    void push(T&& value);

    T peek(std::size_t offset = 1) const;
    T* peekAddress(std::size_t offset = 1);
    const T* peekAddress(std::size_t offset = 1) const;

    T pop();

    std::size_t size() const;

    bool empty() const;
    bool nonEmpty() const;
private:
    T buffer_[StackCapacity];
    std::size_t topIndex_;
};
}

#ifndef LAMA_OP_STACK_CAPACITY
#define LAMA_OP_STACK_CAPACITY 0xf'ffff
#endif

constexpr std::size_t OP_STACK_CAPACITY = LAMA_OP_STACK_CAPACITY;

extern template class lama::interpreter::runtime::GcDataStack<lama::runtime::Word, OP_STACK_CAPACITY>;
using DataStack = lama::interpreter::runtime::GcDataStack<lama::runtime::Word, OP_STACK_CAPACITY>;

class CallstackFrame final {
public:
    CallstackFrame() = default;
    CallstackFrame(lama::runtime::Word *frameBase, std::int32_t argsCount, std::int32_t localsCount, bool hasClosure, bool hasCaptures);
    ~CallstackFrame() = default;

    lama::runtime::Word* getFrameBase() const;

    lama::runtime::Word* getArgumentValueAddress(std::int32_t i);
    const lama::runtime::Word* getArgumentValueAddress(std::int32_t i) const;

    void setArgumentValue(std::int32_t i, lama::runtime::Word value);
    lama::runtime::Word getArgumentValue(std::int32_t i) const;

    lama::runtime::Word* getLocalValueAddress(std::int32_t i);
    const lama::runtime::Word* getLocalValueAddress(std::int32_t i) const;

    void setLocalValue(std::int32_t i, lama::runtime::Word value);
    lama::runtime::Word getLocalValue(std::int32_t i) const;

    lama::runtime::Word* getCapturedValueAddress(std::int32_t i);
    const lama::runtime::Word* getCapturedValueAddress(std::int32_t i) const;

    void setCapturedValue(std::int32_t i, lama::runtime::Word value);
    lama::runtime::Word getCapturedValue(std::int32_t i) const;

    std::uint32_t getArgumentsCount() const;
    std::uint32_t getLocalsCount() const;
    std::uint32_t getCapturesCount() const;

    bool hasClosure() const;
    bool hasCaptures() const;
private:
    lama::runtime::Word *frameBase_;
    std::int32_t argsCount_;
    std::int32_t localsCount_;
    bool hasClosure_;
    bool hasCaptures_;

    lama::runtime::Word* getLocalsStartAddress();
    const lama::runtime::Word* getLocalsStartAddress() const;

    lama::runtime::Word* getArgumentsStartAddress();
    const lama::runtime::Word* getArgumentsStartAddress() const;

    lama::runtime::Word* getCapturedContentAddress();
    const lama::runtime::Word* getCapturedContentAddress() const;
};

#ifndef LAMA_CALL_STACK_CAPACITY
#define LAMA_CALL_STACK_CAPACITY 0xffff
#endif

constexpr std::size_t CALLSTACK_CAPACITY = LAMA_CALL_STACK_CAPACITY;
using CallStack = utils::Stack<CallstackFrame, CALLSTACK_CAPACITY>;

class BytecodeInterpreter {
public:
    BytecodeInterpreter(const lama::bytecode::BytecodeFile *bytecodeFile);
    ~BytecodeInterpreter();

    std::int32_t getIp() const;
    std::int32_t getInstructionStartOffset() const;

    void initializeGc();
    void deinitializeGc();

    bool isGcInitialized() const;

    void interpret();
    void runInterpreterLoop();
protected:
    std::byte lookupByte(std::int32_t pos) const;
    std::byte lookupByte() const;

    lama::bytecode::InstructionOpCode lookupInstrOpCode(std::int32_t pos) const;
    lama::bytecode::InstructionOpCode lookupInstrOpCode() const;

    std::byte fetchByte();
    lama::bytecode::InstructionOpCode fetchInstrOpCode();

    std::int32_t lookupInt32(std::int32_t pos) const;
    std::int32_t lookupInt32() const;

    std::int32_t fetchInt32();

    std::string_view getString(std::int32_t index) const;

    void pushWord(lama::runtime::Word w);

    lama::runtime::Word peekWord(std::size_t offset = 1) const;
    lama::runtime::Word *peekWordAddress(std::size_t offset = 1);
    const lama::runtime::Word *peekWordAddress(std::size_t offset = 1) const;
    lama::interpreter::runtime::Value peekValue(std::size_t offset = 1) const;

    lama::runtime::Word popWord();
    lama::interpreter::runtime::Value popValue();

    lama::interpreter::runtime::Value popIntValue(std::string_view message = "expected an integer");
    lama::interpreter::runtime::Value popStringValue(std::string_view message = "expected a string");
    lama::interpreter::runtime::Value popArrayValue(std::string_view message = "expected an array");
    lama::interpreter::runtime::Value popClosureValue(std::string_view message = "expected a closure");
    lama::interpreter::runtime::Value popSexpValue(std::string_view message = "expected a sexp");

    void popWords(std::size_t words);

    void pushValue(lama::interpreter::runtime::Value value);

    void pushFrame(const CallstackFrame &frame);
    void pushFrame(CallstackFrame&& frame);
    CallstackFrame peekFrame() const;
    CallstackFrame popFrame();

    lama::runtime::Word* getGlobalValueAddress(std::int32_t i);
    const lama::runtime::Word* getGlobalValueAddress(std::int32_t i) const;

    void setGlobalValue(std::int32_t i, lama::runtime::Word value);
    lama::runtime::Word getGlobalValue(std::int32_t i) const;

    void interpreterAssert(bool condition, std::string_view message) const;

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

    void processFunctionBegin(std::int32_t argsNum, std::int32_t localsNum, bool isClosure);

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
    std::int32_t ip_;
    std::int32_t instructionStartOffset_;
    alignas(16) DataStack stack_;
    CallStack callstack_;
    bool isClosureCalled_;
    bool endReached_;
    const lama::bytecode::BytecodeFile *bytecodeFile_;

    void setIp(std::int32_t newIp);
    void advanceIp(std::int32_t offset = 1);

    void setInstructionStartOffset(std::int32_t offset);

    const lama::runtime::Word* getGlobalsStartAddress() const;
    lama::runtime::Word* getGlobalsStartAddress();

    void executeArithBinop(lama::bytecode::InstructionOpCode opcode);
    void executeComparisonBinop(lama::bytecode::InstructionOpCode opcode);
    void executeLogicalBinop(lama::bytecode::InstructionOpCode opcode);

    void checkNonNegative(std::int32_t value, std::string_view message) const;

    void checkCodeOffset(std::int32_t offset) const;
    void checkGlobalValueIndex(std::int32_t globalValueIndex) const;
    void checkLocalValueIndex(CallstackFrame frame, std::int32_t localValueIndex) const;
    void checkArgumentValueIndex(CallstackFrame frame, std::int32_t argumentValueIndex) const;
    void checkCapturedValueIndex(CallstackFrame frame, std::int32_t capturedValueIndex) const;

    void doReturnFromFunction();
};
}

#endif
