#pragma once

#include <coroutine>
#include <exception>
#include <functional>
#include <string_view>

namespace coro_iter_headers {

using Callback = std::function<void(std::string_view, std::string_view)>;

class parser {

public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    parser(handle_type h) : coro(h) {}
    ~parser() {
        if (coro)
            coro.destroy();
    }

    parser(const parser &) = delete;
    parser &operator=(const parser &) = delete;

    bool next() {
        if (!coro || coro.done())
            return false;
        coro.resume();
        return !coro.done();
    }

private:
    handle_type coro;
};

parser coroIterHeaders(std::string_view req, Callback &&callback);
}  // namespace coro_iter_headers