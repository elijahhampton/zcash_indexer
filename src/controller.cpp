#include "controller.h"
#include "logger.h"

#include <vector>
#include <string>
#include <iostream>
#include <chrono>
#include <thread>
#include <memory>
#include <mutex>
#include "config.h"

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

void Controller::JoinJoinableSyncingOperations()
{
    if (syncing_thread.joinable())
    {
        syncing_thread.join();
    }

    if (peer_monitoring_thread.joinable())
    {
        peer_monitoring_thread.join();
    }

    if (chain_info_monitoring_thread.joinable())
    {
        chain_info_monitoring_thread.join();
    }
}

int main()
{
    auto database = std::make_unique<Database>();
    auto rpcClient = std::make_unique<CustomClient>(Config::getRpcUrl(), Config::getRpcUsername(), Config::getRpcPassword());
    auto syncer = std::make_unique<Syncer>(*rpcClient, *database);
    
    Controller controller(std::move(rpcClient), std::move(syncer), std::move(database));
    controller.InitAndSetup();
    controller.StartSyncLoop();
   // controller.StartMonitoringPeers();
   // controller.StartMonitoringChainInfo();
    controller.JoinJoinableSyncingOperations();
    controller.Shutdown();

    return 0;
}