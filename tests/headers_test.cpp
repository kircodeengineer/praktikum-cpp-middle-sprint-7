#include "headers.hpp"
#include <gtest/gtest.h>

TEST(iterHeaders, Empty) {
    std::vector<std::pair<std::string, std::string>> headers;
    std::string raw{};

    iterHeaders(raw, [&](std::string_view name, std::string_view value) { headers.emplace_back(name, value); });

    EXPECT_TRUE(headers.empty());
}

TEST(iterHeaders, SkipRequestLine) {
    std::vector<std::pair<std::string, std::string>> headers;
    std::string raw{"GET / HTTP/1.1\r\n"
                    "Host: localhost\r\n"
                    "User-Agent: test\r\n"
                    "\r\n"};

    iterHeaders(raw, [&](std::string_view name, std::string_view value) { headers.emplace_back(name, value); });

    ASSERT_EQ(headers.size(), 2);
    EXPECT_EQ(headers[0].first, "Host");
    EXPECT_EQ(headers[0].second, "localhost");

    EXPECT_EQ(headers[1].first, "User-Agent");
    EXPECT_EQ(headers[1].second, "test");
}

TEST(iterHeaders, SingleHeader) {
    std::vector<std::pair<std::string, std::string>> headers;
    std::string raw{"Content-Type: application/json\r\n\r\n"};

    iterHeaders(raw, [&](std::string_view name, std::string_view value) { headers.emplace_back(name, value); });

    ASSERT_EQ(headers.size(), 1);
    EXPECT_EQ(headers[0].first, "Content-Type");
    EXPECT_EQ(headers[0].second, "application/json");
}

TEST(iterHeaders, MultipleHeaders) {
    std::vector<std::pair<std::string, std::string>> headers;
    std::string raw{"Host: example.com\r\n"
                    "Accept: */*\r\n"
                    "Connection: keep-alive\r\n"
                    "\r\n"};

    iterHeaders(raw, [&](std::string_view name, std::string_view value) { headers.emplace_back(name, value); });

    ASSERT_EQ(headers.size(), 3);
    EXPECT_EQ(headers[0].first, "Host");
    EXPECT_EQ(headers[0].second, "example.com");

    EXPECT_EQ(headers[1].first, "Accept");
    EXPECT_EQ(headers[1].second, "*/*");

    EXPECT_EQ(headers[2].first, "Connection");
    EXPECT_EQ(headers[2].second, "keep-alive");
}

TEST(iterHeaders, MultipleSameHeaders) {
    std::vector<std::pair<std::string, std::string>> headers;
    std::string raw{"SameHeader: aaa\r\n"
                    "SameHeader: aaa\r\n"
                    "NotSameHeader: bbb\r\n"
                    "SameHeader: aaa\r\n"
                    "\r\n"};

    iterHeaders(raw, [&](std::string_view name, std::string_view value) { headers.emplace_back(name, value); });

    ASSERT_EQ(headers.size(), 4);
    EXPECT_EQ(headers[0].first, "SameHeader");
    EXPECT_EQ(headers[0].second, "aaa");

    EXPECT_EQ(headers[1].first, "SameHeader");
    EXPECT_EQ(headers[1].second, "aaa");

    EXPECT_EQ(headers[2].first, "NotSameHeader");
    EXPECT_EQ(headers[2].second, "bbb");

    EXPECT_EQ(headers[3].first, "SameHeader");
    EXPECT_EQ(headers[3].second, "aaa");
}

TEST(findHostPort, Simple) {
    auto [host, port] = findHostPort("Host: test.ru:8080\r\n");
    EXPECT_EQ(host, "test.ru");
    EXPECT_EQ(port, "8080");
}

TEST(findHostPort, NoHost) { EXPECT_THROW(findHostPort(""), std::runtime_error); }

TEST(findContentLength, Simple) {
    auto len{findContentLength("Content-Length: 1234\r\n")};
    EXPECT_TRUE(len.has_value());
    EXPECT_EQ(*len, 1234);
}

TEST(findContentLength, NoContentLength) {
    auto len{findContentLength("")};
    EXPECT_EQ(len, std::nullopt);
}
