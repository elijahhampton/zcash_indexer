#include "controller.h"
#include "logger.h"

#include <vector>
#include <string>
#include <iostream>
#include <chrono>
#include <thread>
#include <memory>
#include <mutex>

#include <boost/beast.hpp>
#include <boost/asio.hpp>

#include "config.h"

namespace net = boost::asio;
namespace beast = boost::beast;
using tcp = net::ip::tcp;
using http = beast::http;


Controller::Controller(std::unique_ptr<CustomClient> rpcClientIn, std::unique_ptr<Syncer> syncerIn,  std::unique_ptr<Database> databaseIn) : 
rpcClient(std::move(rpcClientIn)), syncer(std::move(syncerIn)), database(std::move(databaseIn))
{
    const std::string connection_string =
        "dbname=" + Config::getDatabaseName() +
        " user=" + Config::getDatabaseUser() +
        " password=" + Config::getDatabasePassword() +
        " host=" + Config::getDatabaseHost() +
        " port=" + Config::getDatabasePort();

    __DEBUG__(connection_string.c_str());

    // Five connections are assigned to each hardware thread
    size_t poolSize = std::thread::hardware_concurrency() * 5;
    this->database->Connect(poolSize, connection_string);

    __DEBUG__(("Initializing database with pool size: " + std::to_string(poolSize)).c_str());
}

Controller::~Controller()
{
    this->Shutdown();
}

void Controller::InitAndSetup()
{
    try
    {
        this->database->CreateTables();
    }
    catch (const std::exception &e)
    {
        throw std::runtime_error(e.what());
    }
}

void Controller::StartSyncLoop()
{
    __INFO__("Starting sync thread.");
    syncing_thread = std::thread{&Syncer::StartSyncLoop, this->syncer.get()};
}

void Controller::StartSync()
{
    this->syncer->Sync();
}

void Controller::StartMonitoringPeers()
{
    __INFO__("Starting peer monitoring thread.");
    peer_monitoring_thread = std::thread{&Syncer::InvokePeersListRefreshLoop, this->syncer.get()};
}

void Controller::StartMonitoringChainInfo()
{
    __INFO__("Starting chain info thread..");
    chain_info_monitoring_thread = std::thread{&Syncer::InvokeChainInfoRefreshLoop, this->syncer.get()};
}

void Controller::Shutdown()
{
    this->syncer->Stop();
}

void Controller::DetachSyncingOperations()
{
    if (syncing_thread.joinable())
    {
        syncing_thread.detach();
    }

    if (peer_monitoring_thread.joinable())
    {
        peer_monitoring_thread.detach();
    }

    if (chain_info_monitoring_thread.joinable())
    {
        chain_info_monitoring_thread.detach();
    }
}

int main()
{
    // Controller dependent modules
    __INFO__("INitializing database, RPC HTTP client and syncer");
    auto database = std::make_unique<Database>();
    auto rpcClient = std::make_unique<CustomClient>(Config::getRpcUrl(), Config::getRpcUsername(), Config::getRpcPassword());
    auto syncer = std::make_unique<Syncer>(*rpcClient, *database);
    
    Controller controller(std::move(rpcClient), std::move(syncer), std::move(database));

    controller.InitAndSetup();
    controller.StartSyncLoop();

    if (static_cast<bool>(Config::getMonitorPeers())) {
        controller.StartMonitoringPeers();
    }

    if (static_cast<bool>(Config::getMonitorChainInfo())) {
        controller.StartMonitoringChainInfo();
    }

    controller.DetachSyncingOperations();

    try {
    // Initializer SSE server
    net::io_context ioc{1};
    tcp::acceptor tcp_acceptor{ioc, {tcp::v4(), 8080}};
    tcp::socket tcp_v4_socket(ioc);
    tcp_acceptor.accept(tcp_v4_socket); // Wait for a session

    beast::error_code ec;
    beast::flat_buffer buffer; // This buffer is required to persist across reads
    
    while(true) {
        http::request<Body, http::basic_fields<Allocator>>&& req> req;
        http::read(session_socket, buffer, req, ec);
        
        if (ec == http::error::end_of_stream) {
            break;
        }

        if (req.target() == "/events") {
            // Create a response with the appropriate headers for SSE
            http::response<http::string_body> res{http::status::ok, req.version()};
            res.set(http::field::content_type, "text/event-stream");
            res.set(http::field::cache_control, "no-cache");
            res.keep_alive(true); // Keep the connection open

            // Instead of sending a complete body, you'll send chunks of data periodically
            // For demonstration, sending a single event and ending the connection
            // In practice, you'd keep the connection open and send data in a loop or based on some trigger
            res.body() = "data: Hello, SSE!\n\n"; // Format for SSE data
            res.prepare_payload();

            return res; // Return the response to be sent
        }

    }

    } catch(const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }


    return 0;
}