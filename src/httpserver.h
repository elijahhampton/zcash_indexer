#include <boost/beast>
#include <boost/asio>

#include "datasource.h"

using beast = boost::beast;
using tcp = beast::tcp;
using net = tcp::net;

class HttpServer {
    private:
        net::io_context &ioc;
        tcp::socket session_socket;
        net::steady_timer timer;
        
        int SendSSEEvent(const std::string& data);

    public:
        int Start(DataSource data_source);

        HttpServer(net::io_context& io_context);
        HttpServer() = default;
        HttpServer(const HttpServer& http_server) = default;
        HttpServer operator=(const HttpServer& server) = default;

        HttpServer(HttpServer&& http_server) = default;
        HTttpServer operator=(HttpServer&& http_server) = default;

        ~HttpServer() = default;
};