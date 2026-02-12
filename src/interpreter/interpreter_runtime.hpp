#ifndef INTERPRETER_INTERPRETER_RUNTIME_HPP
#define INTERPRETER_INTERPRETER_RUNTIME_HPP

#include "lama_runtime.hpp"

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

    bool getBool() const;
    std::string_view getString() const;

    LamaTag getTag() const;
    bool isInt() const;
    bool isString() const;
    bool isArray() const;
    bool isClosure() const;
    bool isSexp() const;
    bool isAggregate() const;

    lama::runtime::Word getRawWord() const;
private:
    lama::runtime::Word rawWord_;
};

template<class T, std::size_t StackCapacity>
class GcDataStack {
private:
    static constexpr std::size_t elementSize = sizeof(T);
public:
    static constexpr std::size_t capacity = StackCapacity;

    GcDataStack(T* pointer, std::size_t size);
    ~GcDataStack() = default;

    T* data();

    const T* data() const;

    void push(const T &value);
    void push(T&& value);

    T peek(std::size_t offset = 1) const;
    T* peekAddress(std::size_t offset = 1);
    const T* peekAddress(std::size_t offset = 1) const;

    T pop();

    std::size_t size() const;

    bool empty() const;
    bool nonEmpty() const;
};
}

#endif
