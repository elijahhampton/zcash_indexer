#include "syncer.h"
#include <algorithm>
#include "httpclient.h"
#include <iostream>
#include <string>
#include <optional>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <memory>
#include <chrono>
#include <vector>
#include <sstream>
#include <boost/process.hpp>
#include <fstream>
#include <queue>
#include "config.h"

size_t Syncer::CHUNK_SIZE = std::stoi(Config::getBlockChunkProcessingSize());
const uint8_t Syncer::MAX_CONCURRENT_THREADS = std::thread::hardware_concurrency();

Syncer::Syncer(CustomClient &httpClientIn, Database &databaseIn) : httpClient(httpClientIn), database(databaseIn), latestBlockSynced{0}, latestBlockCount{0}, isSyncing{false}, worker_pool{ThreadPool()}
{
}

Syncer::~Syncer() noexcept {}

void Syncer::CheckAndDeleteJoinableProcessingThreads(std::vector<std::thread> &processingThreads)
{
    processingThreads.erase(std::remove_if(processingThreads.begin(), processingThreads.end(),
                                           [](const std::thread &t)
                                           { return !t.joinable(); }),
                            processingThreads.end());
}

void Syncer::DoConcurrentSyncOnChunk(const std::vector<size_t> &chunkToProcess)
{
    std::vector<Json::Value> downloadedBlocks;
    this->DownloadBlocksFromHeights(downloadedBlocks, chunkToProcess);

    // Create processable chunk

    std::vector<Json::Value> chunk(downloadedBlocks.begin(), downloadedBlocks.end());

    // Launch a new thread for the current chunk
    worker_pool.SubmitTask([this](bool a, const std::vector<Json::Value> &b, uint64_t c, uint64_t d, uint64_t e)
                           { 
                            this->database.StoreChunk(a, b, c, d, e);
                            this->worker_pool.TaskCompleted();
                            },
                           false, chunk, Database::InvalidHeight, Database::InvalidHeight, Database::InvalidHeight);
}

void Syncer::DoConcurrentSyncOnRange(bool isTrackingCheckpointForChunks, uint64_t start, uint64_t end) {
    std::vector<Json::Value> downloadedBlocks;

    // Generate a checkpoint for the start point if found
    std::optional<Database::Checkpoint> checkpointOpt = this->database.GetCheckpoint(start);
    bool checkpointExist = checkpointOpt.has_value();
    Database::Checkpoint checkpoint;
    size_t chunkStartPoint{start};
    size_t chunkEndPoint;

    if (end - chunkStartPoint + 1 >= Syncer::CHUNK_SIZE)
    {
        chunkEndPoint = chunkStartPoint + Syncer::CHUNK_SIZE - 1;
    }
    else
    {
        chunkEndPoint = end;
    }

    if (checkpointExist)
    {
        checkpoint = checkpointOpt.value();

        // Set chunkStartPoint to the next block after the last check point
        chunkStartPoint = checkpoint.lastCheckpoint;
    }

    bool isExistingCheckpoint = false;
    while (chunkStartPoint <= end)
    {
        if (!checkpointExist && isTrackingCheckpointForChunks)
        {
            isExistingCheckpoint = false;
            this->database.CreateCheckpointIfNonExistent(chunkStartPoint, chunkEndPoint);
        }
        else
        {
            isExistingCheckpoint = true;
        }

        // Download blocks
        this->DownloadBlocks(downloadedBlocks, chunkStartPoint, chunkEndPoint);

        // Create processable chunk
        std::vector<Json::Value> chunk(downloadedBlocks.begin(), downloadedBlocks.end());

        // Launch a new thread for the current chunk
        this->worker_pool.SubmitTask([this](bool a, const std::vector<Json::Value> &b, uint64_t c, uint64_t d, uint64_t e)
                                     { 
                                        this->database.StoreChunk(a, b, c, d, e); 
                                        this->worker_pool.TaskCompleted();
                                     },
                                     isTrackingCheckpointForChunks, chunk, chunkStartPoint, chunkEndPoint, isExistingCheckpoint ? start : chunkStartPoint);

        // All blocks processed
        downloadedBlocks.clear();

        // Update chunkStartPoint to the next chunk
        chunkStartPoint = chunkEndPoint + 1;

        // Update chunkEndPoint for the next chunk, capped at 'end'
        if (end - chunkStartPoint + 1 >= Syncer::CHUNK_SIZE)
        {
            chunkEndPoint = chunkStartPoint + Syncer::CHUNK_SIZE - 1;
        }
        else
        {
            chunkEndPoint = end;
        }
    }
}

void Syncer::StartSyncLoop()
{
    const std::chrono::minutes syncInterval(30);

    while (this->run_syncing)
    {
        std::lock_guard<std::mutex> syncLock(cs_sync);

        if (this->ShouldSyncWallet())
        {
            this->Sync();
        }

        std::this_thread::sleep_for(syncInterval);
    }
}

void Syncer::InvokePeersListRefreshLoop() noexcept
{
    while (this->run_peer_monitoring)
    {
        try
        {
            std::lock_guard<std::mutex> lock(httpClientMutex);
            Json::Value peer_info = this->httpClient.getpeerinfo();
            this->database.StorePeers(peer_info);
        }
        catch (const std::exception &e)
        {
            std::cout << e.what() << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::hours(24));
    }
}

void Syncer::InvokeChainInfoRefreshLoop() noexcept
{
    while (this->run_chain_info_monitoring)
    {
        try
        {
            std::lock_guard<std::mutex> lock(httpClientMutex);
            Json::Value chain_info = this->httpClient.getblockchaininfo();
            this->database.StoreChainInfo(chain_info);
        }
        catch (const std::exception &e)
        {
            std::cout << e.what() << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::minutes(30));
    }
}

void Syncer::SyncUnfinishedCheckpoints(std::stack<Database::Checkpoint>& checkpoints) {

        // Sync unfinished checkpoints
        Database::Checkpoint currentCheckpoint;

        while (!checkpoints.empty())
        {
            currentCheckpoint = checkpoints.top();

            uint64_t startHeight = currentCheckpoint.chunkStartHeight;
            uint64_t endHeight = currentCheckpoint.chunkEndHeight;

            this->DoConcurrentSyncOnRange(true, startHeight, endHeight);

            checkpoints.pop();
        }
}

void Syncer::Sync()
{
    try
    {
        this->isSyncing = true;
        this->worker_pool.RefreshThreadPool();

        std::stack<Database::Checkpoint> checkpoints = this->database.GetUnfinishedCheckpoints();
        if (!checkpoints.empty()) {
            this->SyncUnfinishedCheckpoints(checkpoints);
            this->worker_pool.RefreshThreadPool();
        }
        
        // Sync new blocks
        this->LoadTotalBlockCountFromChain();
        this->LoadSyncedBlockCountFromDB();
        uint64_t numNewBlocks = this->latestBlockCount - this->latestBlockSynced;

        if (numNewBlocks == 0)
        {
            this->isSyncing = false;
            return;
        }
        else if (numNewBlocks >= CHUNK_SIZE)
        {
            std::cout << "Syncing path: SyncOnRange" << std::endl;
            uint64_t startRangeChunk = this->latestBlockSynced == 0 ? this->latestBlockSynced : this->latestBlockSynced + 1;  
            this->DoConcurrentSyncOnRange(true, startRangeChunk, this->latestBlockCount);
        }
        else
        { 
            std::cout << "Syncing path: SyncOnChunk" << std::endl;
            std::vector<size_t> heights;
            size_t totalBlocksToSync = this->latestBlockCount - this->latestBlockSynced;
            heights.reserve(totalBlocksToSync);

            for (size_t i = 1; i <= totalBlocksToSync; i++)
            {
                heights.push_back(this->latestBlockSynced + i);
            }

            this->DoConcurrentSyncOnChunk(heights);
        }

        this->worker_pool.RefreshThreadPool();
        this->isSyncing = false;
    }
    catch (std::exception &e)
    {
        std::cout << e.what() << std::endl;
    }
}

void Syncer::DownloadBlocksFromHeights(std::vector<Json::Value> &downloadedBlocks, std::vector<size_t> heightsToDownload)
{
    auto numHeightsToDownload{heightsToDownload.size()};
    if (numHeightsToDownload > Syncer::CHUNK_SIZE)
    {
        throw std::runtime_error("Desired download size is greater than allowed per configuration");
    }

    Json::Value getblockParams;
    Json::Value blockResultSerialized;

    std::lock_guard<std::mutex> lock(httpClientMutex);
    size_t i{0};

    while (i < numHeightsToDownload)
    {
        getblockParams.append(Json::Value(std::to_string(heightsToDownload.at(i))));
        getblockParams.append(Json::Value(2));

        try
        {
            blockResultSerialized = httpClient.CallMethod("getblock", getblockParams);

            if (blockResultSerialized.isNull())
            {
                ++i;
                continue;
            }

            downloadedBlocks.push_back(blockResultSerialized);
            blockResultSerialized.clear();
            getblockParams.clear();
        }
        catch (jsonrpc::JsonRpcException &e)
        {
            ++i;
            getblockParams.clear();
            continue;
        }
        catch (std::exception &e)
        {
            ++i;
            getblockParams.clear();
            continue;
        }

        ++i;
    }
}

void Syncer::DownloadBlocks(std::vector<Json::Value> &downloadBlocks, uint64_t startRange, uint64_t endRange)
{
    std::lock_guard<std::mutex> lock(httpClientMutex);
    Json::Value getblockParams{Json::nullValue};
    Json::Value blockResultSerialized{Json::nullValue};

    while (startRange <= endRange)
    {
        getblockParams.append(Json::Value(std::to_string(startRange)));
        getblockParams.append(Json::Value(Syncer::BLOCK_DOWNLOAD_VERBOSE_LEVEL));

        try
        {
            blockResultSerialized = httpClient.CallMethod("getblock", getblockParams);

            if (blockResultSerialized.isNull())
            {
                throw new std::exception();
            }

            downloadBlocks.push_back(blockResultSerialized);
        }
        catch (jsonrpc::JsonRpcException &e)
        {
            this->database.AddMissedBlock(startRange);
            downloadBlocks.push_back(Json::nullValue);
        }
        catch (std::exception &e)
        {
            this->database.AddMissedBlock(startRange);
            downloadBlocks.push_back(Json::nullValue);
        }

        blockResultSerialized.clear();
        getblockParams.clear();
        startRange++;
    }
}

void Syncer::LoadSyncedBlockCountFromDB()
{
    this->latestBlockSynced = this->database.GetSyncedBlockCountFromDB();
}

void Syncer::LoadTotalBlockCountFromChain()
{
    try
    {
        std::lock_guard<std::mutex> lock(httpClientMutex);
        this->latestBlockCount = httpClient.getblockcount().asLargestUInt();
    }
    catch (jsonrpc::JsonRpcException &e)
    {
        if (std::string(e.what()).find("Loading block index") != std::string::npos)
        {
            while (std::string(e.what()).find("Loading block index") != std::string::npos && std::string(e.what()).find("Verifying blocks") != std::string::npos)
            {
                std::cout << "Loading block index." << std::endl;
            }

            this->LoadTotalBlockCountFromChain();
        }
    }
    catch (std::exception &e)
    {
        std::cout << e.what() << std::endl;
        exit(1);
    }
}

bool Syncer::ShouldSyncWallet()
{
    if (this->isSyncing)
    {
        return false;
    }

    this->LoadTotalBlockCountFromChain();
    this->LoadSyncedBlockCountFromDB();

    if (!this->isSyncing && (this->latestBlockSynced < this->latestBlockCount))
    {
        return true;
    }

    return false;
}

bool Syncer::GetSyncingStatus() const
{
    return this->isSyncing;
}

void Syncer::StopPeerMonitoring()
{
    this->run_peer_monitoring = false;
}

void Syncer::StopSyncing()
{
    this->run_syncing = false;
}

void Syncer::Stop()
{
    this->StopPeerMonitoring();
    this->StopSyncing();
    this->worker_pool.End();
}