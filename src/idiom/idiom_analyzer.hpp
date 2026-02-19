#ifndef BYTECODE_IDIOM_ANALYZER_HPP
#define BYTECODE_IDIOM_ANALYZER_HPP

#include <cstdint>
#include <vector>

#include "../bytecode/source_file.hpp"

namespace lama::idiom {
    using idiom_record_t = std::pair<lama::bytecode::offset_t, std::uint32_t>;

    namespace detail {
        class IdiomAnalyzer {
        public:
            IdiomAnalyzer(const bytecode::BytecodeFile *file);
            IdiomAnalyzer(const IdiomAnalyzer &) = default;
            IdiomAnalyzer(IdiomAnalyzer&&) = default;
            ~IdiomAnalyzer() = default;

            void findIdioms(std::vector<idiom_record_t> &idioms1, std::vector<idiom_record_t> &idioms2);
        private:
            const bytecode::BytecodeFile *bytecodeFile_;
            std::vector<bool> reachableInstrs_;
            std::vector<bool> labeled_;

            void preprocess();
        };

        void collectFrequencies(const bytecode::BytecodeFile *file, std::vector<idiom_record_t> &idioms);
    }

    template<class Func>
    void processIdiomsFrequencies(const bytecode::BytecodeFile *file, Func &&func) {
        detail::IdiomAnalyzer analyzer{file};
        std::vector<idiom_record_t> idioms1, idioms2;
        analyzer.findIdioms(idioms1, idioms2);

        detail::collectFrequencies(file, idioms1);
        detail::collectFrequencies(file, idioms2);

        std::size_t ptr1 = 0, ptr2 = 0;

        while (ptr1 < idioms1.size() || ptr2 < idioms2.size()) {
            std::uint32_t instrAddr;
            std::uint32_t instrsNum;
            std::uint32_t freq;

            if (ptr1 < idioms1.size() && ptr2 < idioms2.size()) {
                if (idioms1[ptr1].second > idioms2[ptr2].second) {
                    instrAddr = idioms1[ptr1].first;
                    freq = idioms1[ptr1].second;
                    instrsNum = 1;

                    ++ptr1;
                } else {
                    instrAddr = idioms2[ptr2].first;
                    freq = idioms2[ptr2].second;
                    instrsNum = 2;

                    ++ptr2;
                }
            } else if (ptr1 < idioms1.size()) {
                instrAddr = idioms1[ptr1].first;
                freq = idioms1[ptr1].second;
                instrsNum = 1;

                ++ptr1;
            } else {
                instrAddr = idioms2[ptr2].first;
                freq = idioms2[ptr2].second;
                instrsNum = 2;

                ++ptr2;
            }

            func({instrAddr, instrsNum}, freq);
        }
    }
}

#endif
