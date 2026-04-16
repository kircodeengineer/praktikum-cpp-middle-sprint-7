#include <coro_iter_headers.hpp>

#include <ranges>

namespace coro_iter_headers {
using namespace std::string_view_literals;

using Callback = std::function<void(std::string_view, std::string_view)>;

struct parser::promise_type {
    auto get_return_object() { return parser{handle_type::from_promise(*this)}; }

    auto initial_suspend() { return std::suspend_always{}; }
    auto final_suspend() noexcept { return std::suspend_always{}; }

    void return_void() {}

    void unhandled_exception() { std::rethrow_exception(std::current_exception()); }

    auto yield_value() { return std::suspend_always{}; }
};

struct callback_awaiter {
    Callback cb;
    std::string_view name;
    std::string_view value;

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> /*handle*/) const { cb(name, value); }

    void await_resume() const noexcept {}
};

parser coroIterHeaders(std::string_view req, Callback &&callback) {
    auto lines = req | std::views::split("\r\n"sv);
    for (auto it = lines.begin(); it != lines.end(); ++it) {
        std::string_view line{*it};
        if (line.empty())
            break;
        auto colon{std::ranges::find(line, ':')};
        if (colon != line.end()) {
            std::string_view name{line.begin(), colon};
            std::string_view value{colon + 1, line.end()};
            value.remove_prefix(std::min(value.find_first_not_of(" \t"), value.size()));
            co_await callback_awaiter{callback, name, value};
        }
    }

    co_return;
}
}  // namespace coro_iter_headers