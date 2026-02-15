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

std::string_view lama::interpreter::runtime::Value::getString() const {
    return std::string_view{reinterpret_cast<const char *>(getNativeInt())};
}

lama::interpreter::runtime::LamaTag lama::interpreter::runtime::Value::getTag() const {
    unsigned char tagNum = ::LkindOf(reinterpret_cast<void *>(getNativeUIntRepresentation(rawWord_)));

    return lama::interpreter::runtime::LamaTag{tagNum};
}

template class lama::interpreter::runtime::GcDataStack<lama::runtime::Word, lama::interpreter::OP_STACK_CAPACITY>;
