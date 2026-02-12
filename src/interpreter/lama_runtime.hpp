#ifndef INTERPRETER_LAMA_RUNTIME_HPP
#define INTERPRETER_LAMA_RUNTIME_HPP

#include <cstddef>

extern "C" {
    #include "Lama/runtime/runtime_common.h"
    #include "Lama/runtime/gc.h"

    extern size_t __gc_stack_top, __gc_stack_bottom;

    aint Lread ();
    aint Lwrite (aint n);

    aint Llength (void *p);
    void *Lstring (aint* args /* void *p */);

    aint LkindOf (void *p);
    aint LtagHash (char *s);

    void *Belem (void *p, aint i);
    void *Bstring (aint* args);
    void *Bclosure (aint* args, aint bn);
    void *Barray (aint* args, aint bn);
    void *Bsexp (aint* args, aint bn);

    aint Btag (void *d, aint t, aint n);
    aint Barray_patt (void *d, aint n);
    aint Bstring_patt (void *x, void *y);

    aint Bclosure_tag_patt (void *x);
    aint Bboxed_patt (void *x);
    aint Bunboxed_patt (void *x);
    aint Barray_tag_patt (void *x);
    aint Bstring_tag_patt (void *x);
    aint Bsexp_tag_patt (void *x);

    void *Bsta (void *x, aint i, void *v);
    void Bmatch_failure (void *v, char *fname, aint line, aint col);
    _Noreturn void failure (char *s, ...);

    aint get_len (data *d);
}

namespace lama::runtime {
using native_int_t = aint;
using native_uint_t = auint;

enum class Word : native_uint_t {};

inline native_uint_t getNativeUIntRepresentation(Word w) {
    return static_cast<native_uint_t>(w);
}
}

#endif
