#pragma once

#include <cassert>
#include <type_traits>
#include <utility>
#include <variant>

namespace mithril::core {

template <typename T, typename E>
class Expected {
public:
    Expected(const T& value) : storage_(value) {}
    Expected(T&& value) : storage_(std::move(value)) {}

    static Expected failure(const E& error) { return Expected(FailureTag{}, error); }
    static Expected failure(E&& error) { return Expected(FailureTag{}, std::move(error)); }

    [[nodiscard]] bool hasValue() const noexcept { return storage_.index() == 0; }
    [[nodiscard]] explicit operator bool() const noexcept { return hasValue(); }

    T& value() & {
        assert(hasValue());
        return std::get<0>(storage_);
    }
    const T& value() const& {
        assert(hasValue());
        return std::get<0>(storage_);
    }
    T&& value() && {
        assert(hasValue());
        return std::get<0>(std::move(storage_));
    }
    E& error() & {
        assert(!hasValue());
        return std::get<1>(storage_);
    }
    const E& error() const& {
        assert(!hasValue());
        return std::get<1>(storage_);
    }

private:
    struct FailureTag {};

    template <typename U>
    Expected(FailureTag, U&& error) : storage_(std::in_place_index<1>, std::forward<U>(error)) {}

    std::variant<T, E> storage_;
};

template <typename E>
class Expected<void, E> {
public:
    Expected() = default;

    static Expected failure(const E& error) {
        Expected result;
        result.error_.template emplace<1>(error);
        return result;
    }
    static Expected failure(E&& error) {
        Expected result;
        result.error_.template emplace<1>(std::move(error));
        return result;
    }

    [[nodiscard]] bool hasValue() const noexcept { return error_.index() == 0; }
    [[nodiscard]] explicit operator bool() const noexcept { return hasValue(); }
    E& error() & {
        assert(!hasValue());
        return std::get<1>(error_);
    }
    const E& error() const& {
        assert(!hasValue());
        return std::get<1>(error_);
    }

private:
    std::variant<std::monostate, E> error_;
};

} // namespace mithril::core
