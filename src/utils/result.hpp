#ifndef UTILS_RESULT_HPP
#define UTILS_RESULT_HPP

#include <optional>
#include <utility>

namespace utils {
template<class ResultType, class ErrorEnum>
class Result {
public:
    Result(ResultType&& result)
        : result_(std::forward<ResultType>(result))
        , error_{} {

    }

    Result(ErrorEnum errorValue)
        : result_(std::nullopt)
        , error_(errorValue) {

    }

    Result(const Result &other) = default;
    Result(Result&& other) = default;
    ~Result() = default;

    bool hasResult() const {
        return static_cast<bool>(result_);
    }

    ResultType getResult() const {
        return *result_;
    }

    ResultType& getResult() {
        return *result_;
    }

    bool hasError() const {
        return !hasResult();
    }

    ErrorEnum getError() const {
        return error_;
    }

private:
    std::optional<ResultType> result_;
    ErrorEnum error_;
};
}

#endif
