#include "controller.h"

#include <vector>
#include <string>
#include <iostream>
#include <chrono>
#include <thread>
#include <memory>
#include <mutex>
#include "config.h"

Controller::Controller(std::unique_ptr<CustomClient> rpcClientIn, std::unique_ptr<Syncer> syncerIn)
try : rpcClient(rpcClientIn), syncer(syncerIn)
{
    const std::string connection_string =
        "dbname=" + Config::getDatabaseName() +
        " user=" + Config::getDatabaseUser() +
        " password=" + Config::getDatabasePassword() +
        " host=" + Config::getDatabaseHost() +
        " port=" + Config::getDatabasePort();

    // Five connections are assigned to each hardware thread
    unsigned int poolSize = std::thread::hardware_concurrency() * 5;
    if (!this->database.Connect(poolSize, connection_string))
    {
        throw std::runtime_error("Database failed to open.");
    }

    if (this->rpcClient == nullptr) {
        throw std::runtime_error("Database failed to open.");
    }

    if (this->syncer == nullptr) {
        throw std::runtime_error("Database failed to open.");
    }
}
catch (const std::exception &e)
{
    std::cerr << "Error: " << e.what() << std::endl;
    throw;
}

Controller::~Controller()
{
    this->Shutdown();
}

void Controller::InitAndSetup()
{
    try
    {
        this->database.CreateTables();
    }
    catch (const std::exception &e)
    {
        throw std::runtime_error("Database failed to create tables.");
    }
}

void Controller::StartSyncLoop()
{
    syncing_thread = std::thread{&Syncer::StartSyncLoop, this->syncer.get()};
}

void Controller::StartSync()
{
    this->syncer->Sync();
}

void Controller::StartMonitoringPeers()
{
    peer_monitoring_thread = std::thread{&Syncer::InvokePeersListRefreshLoop, this->syncer.get()};
}

void Controller::StartMonitoringChainInfo()
{
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
    Database database;
    CustomClient rpcClient(Config::getRpcUrl(), Config::getRpcUsername(), Config::getRpcPassword());
    Syncer syncer(rpcClient, database);
    Controller controller(rpcClient, syncer, database);
    controller.InitAndSetup();
    controller.StartSyncLoop();
    controller.StartMonitoringPeers();
    controller.StartMonitoringChainInfo();
    controller.JoinJoinableSyncingOperations();
    controller.Shutdown();

    return 0;
}