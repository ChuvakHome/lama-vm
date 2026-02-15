#ifndef INTERPRETER_INTERPRETER_RUNTIME_HPP
#define INTERPRETER_INTERPRETER_RUNTIME_HPP

#include "lama_runtime.hpp"

#include <new>
#include <string_view>

namespace lama::interpreter::runtime {
enum class LamaTag : unsigned char {
    STRING = STRING_TAG,
    ARRAY = ARRAY_TAG,
    SEXP = SEXP_TAG,
    CLOSURE = CLOSURE_TAG,
    UNBOXED = UNBOXED_TAG,
};

class Value {
public:
    Value(lama::runtime::native_uint_t val);
    Value(lama::runtime::native_int_t val);
    Value(bool val);
    Value(std::string_view view);
    Value(void *ptr);
    Value(const void *ptr);
    explicit Value(lama::runtime::Word rawWord);
    ~Value() = default;

    lama::runtime::native_int_t getNativeInt() const;
    lama::runtime::native_uint_t getNativeUInt() const;

    bool getBool() const {
        return getNativeInt() != 0;
    }

    std::string_view getString() const;

    LamaTag getTag() const;

    bool isInt() const {
        return getTag() == LamaTag::UNBOXED;
    }

    bool isString() const {
        return getTag() == LamaTag::STRING;
    }

    bool isArray() const {
        return getTag() == LamaTag::ARRAY;
    }

    bool isClosure() const {
        return getTag() == LamaTag::CLOSURE;
    }

    bool isSexp() const {
        return getTag() == LamaTag::SEXP;
    }

    bool isAggregate() const {
        return isString() || isArray() || isClosure() || isSexp();
    }

    lama::runtime::Word getRawWord() const {
        return rawWord_;
    }
private:
    lama::runtime::Word rawWord_;
};

template<class T, std::size_t StackCapacity>
class GcDataStack {
private:
    static constexpr std::size_t elementSize = sizeof(T);
public:
    static constexpr std::size_t capacity = StackCapacity;

    GcDataStack(T* pointer, std::size_t size) {
        __gc_stack_top = reinterpret_cast<std::size_t>(pointer);
        __gc_stack_bottom = __gc_stack_top;

        for (std::size_t i = 0; i < size; ++i) {
            T *ptr = std::launder(reinterpret_cast<T *>(__gc_stack_bottom));

            new(ptr) T;

            __gc_stack_bottom += elementSize;
        }
    }

    ~GcDataStack() {
        for (T *ptr = data(); ptr < end(); ++ptr) {
            ptr->~T();
        }
    }

    T* data() {
        return std::launder(reinterpret_cast<T *>(__gc_stack_top));
    }

    const T* data() const {
        return std::launder(reinterpret_cast<const T *>(__gc_stack_top));
    }

    T* begin() {
        return data();
    }

    const T* begin() const {
        return data();
    }

    T* end() {
        return peekAddress(0);
    }

    const T* end() const {
        return peekAddress(0);
    }

    void push(const T &value) {
        T *ptr = peekAddress(0);
        new (ptr) T(value);
        __gc_stack_bottom += elementSize;
    }

    void push(T&& value) {
        T *ptr = peekAddress(0);
        new (ptr) T(std::move(value));
        __gc_stack_bottom += elementSize;
    }

    T peek(std::size_t offset = 1) const {
        return *peekAddress(offset);
    }

    T* peekAddress(std::size_t offset = 1) {
        return std::launder(reinterpret_cast<T *>(__gc_stack_bottom - offset * elementSize));
    }

    const T* peekAddress(std::size_t offset = 1) const {
        return std::launder(reinterpret_cast<const T *>(__gc_stack_bottom - offset * elementSize));
    }

    T pop() {
        T *ptr = peekAddress();
        const T top = std::move(*ptr);
        ptr->~T();

        __gc_stack_bottom -= elementSize;

        return top;
    }

    std::size_t size() const {
        return (__gc_stack_bottom - __gc_stack_top) / elementSize;
    }

    bool empty() const {
        return size() == 0;
    }

    bool nonEmpty() const {
        return size() != 0;
    }
};
}

#endif
