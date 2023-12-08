#include "controller.h"
// #include "json/json.h"

#include <vector>
#include <string>
#include <iostream>
#include <chrono>
#include <thread>
#include <memory>
#include <mutex>
#include "config.h"

Controller::Controller()
try : rpcClient(std::make_unique<CustomClient>(Config::getRpcUrl(), Config::getRpcUsername(), Config::getRpcPassword())),
    syncer(std::make_unique<Syncer>(*rpcClient, database))
{
    std::cout << Config::getRpcUrl() << std::endl;
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
    chain_info_monitoring_thread  = std::thread{&Syncer::InvokeChainInfoRefreshLoop, this->syncer.get()};
}

void Controller::Shutdown()
{
    std::cout << "Calling Shutdown on controller" << std::endl;
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
    try
    {
        Controller controller;
        controller.InitAndSetup();
        controller.StartSyncLoop();
        controller.StartMonitoringPeers();
        controller.StartMonitoringChainInfo();
        controller.JoinJoinableSyncingOperations();

        // Now shutdown the controller, which will join the threads
        controller.Shutdown();
    }
    catch (const std::exception &e)
    {
        std::cout << e.what() << std::endl;
    }

    return 0;
}