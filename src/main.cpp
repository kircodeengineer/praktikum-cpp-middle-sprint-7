#include "headers.hpp"

#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <iostream>
#include <string_view>

using boost::asio::async_read_until;
using boost::asio::awaitable;
using boost::asio::buffer;
using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::dynamic_buffer;
using boost::asio::io_context;
using boost::asio::io_service;
using boost::asio::transfer_at_least;
using boost::asio::use_awaitable;
using boost::asio::ip::tcp;
using boost::system::error_code;

constexpr std::string_view delimiter = "\r\n\r\n";

awaitable<void> transfer(tcp::socket &from, tcp::socket &to) {
    std::array<char, 4096> buf;
    try {
        for (;;) {
            size_t n{co_await async_read(from, buffer(buf), transfer_at_least(1), use_awaitable)};
            co_await async_write(to, buffer(buf, n), use_awaitable);
        }
    } catch (...) {
    }
}

awaitable<void> session(tcp::socket client_socket, io_context &io) {
    std::string client_storage;
    auto client_buf{dynamic_buffer(client_storage)};

    try {
        size_t n{co_await async_read_until(client_socket, client_buf, delimiter, use_awaitable)};

        std::string_view headers_view{client_storage.data(), n};
        auto [host, port] = findHostPort(headers_view);

        tcp::resolver resolver(io);
        tcp::socket server_socket(io);

        auto endpoints{co_await resolver.async_resolve(host, port, use_awaitable)};
        co_await server_socket.async_connect(*endpoints.begin(), use_awaitable);

        co_await async_write(server_socket, buffer(client_storage), use_awaitable);

        co_spawn(client_socket.get_executor(), transfer(client_socket, server_socket), detached);
        co_await transfer(server_socket, client_socket);

    } catch (const std::exception &e) {
        error_code ec;
        std::string err{"HTTP/1.1 502 Bad Gateway\r\nConnection: close\r\n\r\n"};
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
