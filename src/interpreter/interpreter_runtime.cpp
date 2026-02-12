#include "interpreter_runtime.hpp"

#include <cstdint>
#include <string_view>

#include "Lama/runtime/runtime_common.h"
#include "lama_runtime.hpp"

#include "interpreter.hpp"

/* Value implementation */

lama::interpreter::runtime::Value::Value(lama::runtime::native_uint_t val)
    : rawWord_(lama::runtime::Word(BOX(val))) {

}

lama::interpreter::runtime::Value::Value(lama::runtime::native_int_t val)
    : rawWord_(lama::runtime::Word(BOX(val))) {

}

lama::interpreter::runtime::Value::Value(bool val)
    : Value(lama::runtime::native_int_t(val ? 1 : 0)) {

}

lama::interpreter::runtime::Value::Value(std::string_view view)
    : rawWord_(lama::runtime::Word{reinterpret_cast<std::uintptr_t>(view.data())}) {

}

lama::interpreter::runtime::Value::Value(void *ptr)
    : rawWord_(lama::runtime::Word{reinterpret_cast<std::uintptr_t>(ptr)}) {

}

lama::interpreter::runtime::Value::Value(const void *ptr)
    : rawWord_(lama::runtime::Word{reinterpret_cast<std::uintptr_t>(ptr)}) {

}

lama::interpreter::runtime::Value::Value(lama::runtime::Word rawWord)
    : rawWord_(rawWord) {

}

lama::runtime::native_int_t lama::interpreter::runtime::Value::getNativeInt() const {
    return UNBOX(getNativeUIntRepresentation(getRawWord()));
}

lama::runtime::native_uint_t lama::interpreter::runtime::Value::getNativeUInt() const {
    return UNBOX(getNativeUIntRepresentation(getRawWord()));
}

bool lama::interpreter::runtime::Value::getBool() const {
    return getNativeInt() != 0;
}

bool lama::interpreter::runtime::Value::isInt() const {
    return getTag() == LamaTag::UNBOXED;
}

bool lama::interpreter::runtime::Value::isString() const {
    return getTag() == LamaTag::STRING;
}

bool lama::interpreter::runtime::Value::isArray() const {
    return getTag() == LamaTag::ARRAY;
}

bool lama::interpreter::runtime::Value::isClosure() const {
    return getTag() == LamaTag::CLOSURE;
}

bool lama::interpreter::runtime::Value::isSexp() const {
    return getTag() == LamaTag::SEXP;
}

bool lama::interpreter::runtime::Value::isAggregate() const {
    return isString() || isArray() || isClosure() || isSexp();
}

std::string_view lama::interpreter::runtime::Value::getString() const {
    return std::string_view{reinterpret_cast<const char *>(getNativeInt())};
}

lama::interpreter::runtime::LamaTag lama::interpreter::runtime::Value::getTag() const {
    unsigned char tagNum = ::LkindOf(reinterpret_cast<void *>(getNativeUIntRepresentation(rawWord_)));

    return lama::interpreter::runtime::LamaTag{tagNum};
}

lama::runtime::Word lama::interpreter::runtime::Value::getRawWord() const {
    return rawWord_;
}

/* DataStack implementation */

template<class T, std::size_t StackCapacity>
lama::interpreter::runtime::GcDataStack<T, StackCapacity>::GcDataStack(T* pointer, std::size_t size) {
    __gc_stack_top = reinterpret_cast<std::size_t>(pointer);
    __gc_stack_bottom = __gc_stack_top + size * elementSize;
}

template<class T, std::size_t StackCapacity>
T* lama::interpreter::runtime::GcDataStack<T, StackCapacity>::data() {
    return reinterpret_cast<T *>(__gc_stack_top);
}

template<class T, std::size_t StackCapacity>
const T* lama::interpreter::runtime::GcDataStack<T, StackCapacity>::data() const {
    return reinterpret_cast<const T *>(__gc_stack_top);
}

template<class T, std::size_t StackCapacity>
void lama::interpreter::runtime::GcDataStack<T, StackCapacity>::push(const T &value) {
    *(reinterpret_cast<T *>(__gc_stack_bottom)) = value;
    __gc_stack_bottom += elementSize;
}

template<class T, std::size_t StackCapacity>
void lama::interpreter::runtime::GcDataStack<T, StackCapacity>::push(T&& value) {
    *(reinterpret_cast<T *>(__gc_stack_bottom)) = std::move(value);
    __gc_stack_bottom += elementSize;
}

template<class T, std::size_t StackCapacity>
T lama::interpreter::runtime::GcDataStack<T, StackCapacity>::peek(std::size_t offset) const {
    return *reinterpret_cast<T *>(__gc_stack_bottom - offset * elementSize);
}

template<class T, std::size_t StackCapacity>
T* lama::interpreter::runtime::GcDataStack<T, StackCapacity>::peekAddress(std::size_t offset) {
    return reinterpret_cast<T *>(__gc_stack_bottom - offset * elementSize);
}

template<class T, std::size_t StackCapacity>
const T* lama::interpreter::runtime::GcDataStack<T, StackCapacity>::peekAddress(std::size_t offset) const {
    return reinterpret_cast<T *>(__gc_stack_bottom - offset * elementSize);
}

template<class T, std::size_t StackCapacity>
T lama::interpreter::runtime::GcDataStack<T, StackCapacity>::pop() {
    T top = peek();
    __gc_stack_bottom -= elementSize;

    return top;
}

template<class T, std::size_t StackCapacity>
std::size_t lama::interpreter::runtime::GcDataStack<T, StackCapacity>::size() const {
    return (__gc_stack_bottom - __gc_stack_top) / elementSize;
}

template<class T, std::size_t StackCapacity>
bool lama::interpreter::runtime::GcDataStack<T, StackCapacity>::empty() const {
    return size() == 0;
}

template<class T, std::size_t StackCapacity>
bool lama::interpreter::runtime::GcDataStack<T, StackCapacity>::nonEmpty() const {
    return size() != 0;
}

template class lama::interpreter::runtime::GcDataStack<lama::runtime::Word, lama::interpreter::OP_STACK_CAPACITY>;
