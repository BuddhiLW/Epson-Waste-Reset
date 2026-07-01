#pragma once
#include <string>
#include <utility>

// A minimal Result<T> / Error seam so the I/O boundaries (load, parse, build)
// can report WHY they failed instead of returning an empty vector or a bare
// bool. Proportionate to a single-user CLI — no exceptions-as-control-flow, no
// third-party expected<> dependency.

namespace ewr {

    enum class ErrorCode
    {
        FileNotFound,
        ParseFailed,
        EmptyPlan,
        DownloadFailed,
    };

    struct Error
    {
        ErrorCode   code;
        std::string message;
    };

    template <typename T>
    class Result
    {
    public:
        static Result Ok(T value) { return Result(std::move(value)); }
        static Result Err(Error error) { return Result(std::move(error)); }
        static Result Err(ErrorCode code, std::string message) { return Result(Error{code, std::move(message)}); }

        bool ok() const { return ok_; }
        explicit operator bool() const { return ok_; }

        const T& value() const { return value_; }
        T&       value()       { return value_; }
        const Error& error() const { return error_; }

    private:
        explicit Result(T value) : ok_(true), value_(std::move(value)) {}
        explicit Result(Error error) : ok_(false), error_(std::move(error)) {}

        bool  ok_;
        T     value_{};
        Error error_{};
    };

} // namespace ewr
