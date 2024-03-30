#include "controller.h"
#include <vector>
#include <string>
#include <iostream>
#include <chrono>
#include <thread>
#include <memory>
#include <mutex>
#include "spdlog/spdlog.h"


Controller::Controller(std::unique_ptr<CustomClient> rpcClientIn, std::unique_ptr<Syncer> syncerIn, std::unique_ptr<Database> databaseIn) : 
rpcClient(std::move(rpcClientIn)), syncer(std::move(syncerIn)), database(std::move(databaseIn))
{}

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
    spdlog::info("Starting sync thread.");
    syncing_thread = std::thread{&Syncer::StartSyncLoop, this->syncer.get()};
}

void Controller::StartSync()
{
    this->syncer->Sync();
}

void Controller::StartMonitoringPeers()
{
    spdlog::info("Starting peer monitoring thread.");
    peer_monitoring_thread = std::thread{&Syncer::InvokePeersListRefreshLoop, this->syncer.get()};
}

void Controller::StartMonitoringChainInfo()
{
    spdlog::info("Starting chain info thread..");
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