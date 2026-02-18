#ifndef BYTECODE_IDIOM_ANALYZER_HPP
#define BYTECODE_IDIOM_ANALYZER_HPP

#include <algorithm>
#include <cstdint>
#include <vector>

#include "../bytecode/decoder.hpp"
#include "../bytecode/source_file.hpp"

namespace lama::idiom {
    namespace detail {
        class IdiomAnalyzer {
        public:
            IdiomAnalyzer(const bytecode::BytecodeFile *file);
            IdiomAnalyzer(const IdiomAnalyzer &) = default;
            IdiomAnalyzer(IdiomAnalyzer&&) = default;
            ~IdiomAnalyzer() = default;

            std::vector<lama::bytecode::decoder::instruction_span_t> findIdioms();
        private:
            const bytecode::BytecodeFile *bytecodeFile_;
            bool preprocessed_;
            std::vector<bool> reachableInstrs_;
            std::vector<bool> labeled_;

            void preprocess();
        };

        using frequency_result_t = std::pair<lama::bytecode::decoder::instruction_span_t, std::uint32_t>;
        std::vector<frequency_result_t> countFrequencies(const bytecode::BytecodeFile *file, std::vector<lama::bytecode::decoder::instruction_span_t> &idioms);
    }

    template<class Func>
    void processIdiomsFrequencies(const bytecode::BytecodeFile *file, Func &&func) {
        detail::IdiomAnalyzer analyzer{file};
        auto idioms = analyzer.findIdioms();
        std::vector<detail::frequency_result_t> frequencies = detail::countFrequencies(file, idioms);

        std::sort(
            frequencies.begin(),
            frequencies.end(),
            [](detail::frequency_result_t r1, detail::frequency_result_t r2) {
            return r1.second > r2.second;
        });

        std::for_each(frequencies.begin(), frequencies.end(), [&func](detail::frequency_result_t r) {
            func(r.first, r.second);
        });
    }
}

#endif
