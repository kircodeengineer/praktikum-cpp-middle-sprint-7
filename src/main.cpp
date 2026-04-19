#include "headers.hpp"

#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/completion_condition.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <iostream>
#include <print>
#include <string_view>

using boost::asio::async_read_until;
using boost::asio::awaitable;
using boost::asio::buffer;
using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::dynamic_buffer;
using boost::asio::io_context;
using boost::asio::io_service;
using boost::asio::streambuf;
using boost::asio::transfer_at_least;
using boost::asio::transfer_exactly;
using boost::asio::use_awaitable;
using boost::asio::ip::tcp;
using boost::system::error_code;

using namespace std::literals;

constexpr std::string_view delimiter = "\r\n\r\n"sv;

awaitable<std::string> read_headers(boost::asio::ip::tcp::socket &socket, boost::asio::streambuf &buf) {
    std::string_view data;
    static const std::size_t MAX_HEADER_SIZE{4096};
    static const std::size_t CHUNK_SIZE{1024};
    while (buf.size() <= MAX_HEADER_SIZE) {
        try {
            size_t n = co_await socket.async_read_some(buf.prepare(CHUNK_SIZE), use_awaitable);
            if (n == 0)
                throw std::runtime_error{"Client closed connection"};

            buf.commit(n);

            data = {boost::asio::buffer_cast<const char *>(buf.data()), buf.size()};

            if (auto pos = data.find(delimiter); pos != std::string::npos) {
                auto headers{data.substr(0, pos + delimiter.size())};
                buf.consume(pos + delimiter.size());
                co_return headers;
            }
        } catch (const boost::system::system_error &e) {
            auto code = e.code();
            if (code == boost::asio::error::connection_reset || code == boost::asio::error::timed_out)
                throw std::runtime_error{"Connection lost: " + code.message()};

            throw;
        }
    }

    throw std::runtime_error{"Headers too large: no \\r\\n\\r\\n within limit"};
}

awaitable<void> transfer_limited(boost::asio::ip::tcp::socket &from, boost::asio::ip::tcp::socket &to,
                                 std::optional<size_t> limit) {
    static const size_t BUFFER_SIZE{4096};
    std::array<char, BUFFER_SIZE> piece;
    size_t transferred{};
    size_t chunk_size{};

    while (true) {
        if (limit.has_value()) {
            chunk_size = std::min(piece.size(), limit.value() - transferred);
            if (chunk_size == 0)
                break;
        } else
            chunk_size = piece.size();

        try {
            auto n{co_await from.async_read_some(boost::asio::buffer(piece, chunk_size), use_awaitable)};
            if (n == 0)
                break;

            co_await boost::asio::async_write(to, boost::asio::buffer(piece, n), use_awaitable);
            transferred += n;

            if (limit.has_value() && transferred >= limit.value())
                break;

        } catch (const boost::system::system_error &e) {
            auto code = e.code();
            if (code == boost::asio::error::connection_reset || code == boost::asio::error::timed_out) {
                break;
            }
            throw;
        }
    }
}

awaitable<void> session(tcp::socket client_socket, io_context &io) {
    streambuf client_buf;
    streambuf server_buf;

    try {
        std::string req_headers_str{co_await read_headers(client_socket, client_buf)};
        std::string_view req_headers{req_headers_str};

        auto [host, port] = findHostPort(req_headers);
        auto content_length = findContentLength(req_headers);

        tcp::resolver resolver{io};
        tcp::socket server_socket{io};
        auto endpoints = co_await resolver.async_resolve(host, port, use_awaitable);
        co_await server_socket.async_connect(*endpoints.begin(), use_awaitable);

        co_await async_write(server_socket, buffer(req_headers_str), use_awaitable);

        if (content_length.has_value()) {
            auto already_read{client_buf.size()};
            auto remaining{content_length.value() > already_read ? content_length.value() - already_read : 0};

            if (remaining > 0) {
                co_await async_read(client_socket, client_buf, transfer_exactly(remaining), use_awaitable);
                co_await async_write(server_socket, client_buf.data(), use_awaitable);
            }
            client_buf.consume(content_length.value());
        }

        std::string resp_headers_str{co_await read_headers(server_socket, server_buf)};
        co_await async_write(client_socket, buffer(resp_headers_str), use_awaitable);

        auto resp_content_length{findContentLength(std::string_view{resp_headers_str})};
        if (resp_content_length.has_value()) {
            auto body_in_buf{server_buf.size()};  // часть тела уже в буфере
            auto remaining{resp_content_length.value() > body_in_buf ? resp_content_length.value() - body_in_buf : 0};

            if (body_in_buf > 0)
                co_await async_write(client_socket, server_buf.data(), use_awaitable);

            server_buf.consume(body_in_buf);  // очищаем

            if (remaining > 0)
                co_await transfer_limited(server_socket, client_socket, remaining);
        } else
            co_await transfer_limited(server_socket, client_socket, std::nullopt);

    } catch (const std::exception &e) {
        error_code ec;
        std::string err{"HTTP/1.1 502 Bad Gateway\r\nConnection: close\r\n\r\n"s};
        boost::asio::write(client_socket, buffer(err), ec);
    }

    error_code ec;
    client_socket.shutdown(tcp::socket::shutdown_both, ec);
    client_socket.close(ec);
}

class Server {
public:
    Server(io_service &io_service, short port)
        : io_service_(io_service), acceptor_(io_service, tcp::endpoint(tcp::v4(), port)), socket_(io_service) {
        do_accept();
    }

private:
    void do_accept() {
        acceptor_.async_accept(socket_, [this](boost::system::error_code ec) {
            if (!ec) {
                co_spawn(io_service_, session(std::move(socket_), std::ref(io_service_)), boost::asio::detached);
            }
            do_accept();
        });
    }

    io_service &io_service_;
    tcp::acceptor acceptor_;
    tcp::socket socket_;
};

int main(int argc, char *argv[]) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: proxy_server";
            std::cerr << " <listen_port>\n";
            return 1;
        }
        io_service io_service(1);
        Server server(io_service, std::atoi(argv[1]));
        io_service.run();

    } catch (const std::exception &e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}
