#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "database.h"
#include "syncer.h"
#include "httpclient.h"


#include <memory>
#include <iostream>
#include <string>
#include <vector>

class Controller
{
    
private:
    std::unique_ptr<CustomClient> rpcClient{nullptr};
    std::unique_ptr<Syncer> syncer{nullptr};
    std::shared_ptr<Database> database{nullptr};

    std::thread syncing_thread;
    std::thread peer_monitoring_thread;
    std::thread chain_info_monitoring_thread;


public:
    Controller(const Controller&) noexcept = delete;
    Controller& operator=(const Controller&) noexcept = delete;

    Controller(Controller&&) noexcept = default;
    Controller& operator=(Controller&&) noexcept = default;

    Controller(std::unique_ptr<CustomClient>, std::unique_ptr<Syncer>, std::unique_ptr<Database>);
    ~Controller() noexcept;
    void InitAndSetup();
    void Shutdown();
    void StartSyncLoop();
    void StartSync();
    void StartMonitoringPeers();
    void StartMonitoringChainInfo();
    void DetachSyncingOperations();
    void do_accept(tcp_acceptor &tcp_acceptor, boost::asio::io_context& ioc);
    void do_session(tcp::socket session_socket, net::io_context& ioc)
};

#endif // CONTROLLER_H