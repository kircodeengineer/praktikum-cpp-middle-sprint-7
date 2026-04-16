#include <coro_iter_headers.hpp>
#include <headers.hpp>

#include <ranges>
#include <string_view>

using namespace std::string_view_literals;

using Callback = std::function<void(std::string_view, std::string_view)>;

void iterHeaders(std::string_view req, Callback &&callback) {
    auto coro{coro_iter_headers::coroIterHeaders(req, std::move(callback))};
    while (coro.next())
        ;
}

std::pair<std::string, std::string> findHostPort(std::string_view req) {
    std::string host{};
    std::string port{"80"};

    iterHeaders(req, [&](std::string_view name, std::string_view value) {
        if (name == "Host") {
            auto pos = value.find(':');
            if (pos != std::string_view::npos) {
                host = std::string(value.substr(0, pos));
                port = std::string(value.substr(pos + 1));
            } else
                host = std::string(value);
        }
    });

    if (host.empty())
        throw std::runtime_error("Host header not found");

    return {host, port};
}

std::optional<size_t> findContentLength(std::string_view rsp) {
    std::optional<size_t> result;
    iterHeaders(rsp, [&](std::string_view name, std::string_view value) {
        if (name == "Content-Length") {
            try {
                result = std::stoull(std::string(value));
            } catch (...) {
                result = std::nullopt;
            }
        }
    });
    return result;
}
