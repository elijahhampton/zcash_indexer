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
    std::unique_ptr<CustomClient> rpcClient;
    std::unique_ptr<Syncer> syncer;
    Database database; 

    std::thread syncing_thread;
    std::thread peer_monitoring_thread;
    std::thread chain_info_monitoring_thread;


public:
    Controller();
    ~Controller() noexcept;
    void InitAndSetup();
    void Shutdown();
    void StartSyncLoop();
    void StartSync();
    void StartMonitoringPeers();
    void StartMonitoringChainInfo();
    void JoinJoinableSyncingOperations();
};

#endif