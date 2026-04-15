#include "headers.h"

#include <ranges>
#include <string_view>

using namespace std::string_view_literals;

using Callback = std::function<void(std::string_view, std::string_view)>;

void iterHeaders(std::string_view req, Callback &&callback) {
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
            callback(name, value);
        }
    }
}

std::pair<std::string, std::string> findHostPort(std::string_view req) {
    // code here
}

std::optional<size_t> findContentLength(std::string_view rsp) {
    // code here
}
