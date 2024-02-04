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

void Syncer::DoConcurrentSyncOnChunk(const std::vector<size_t> &chunkToProcess)
{
    std::vector<Json::Value> downloadedBlocks;
    downloadedBlocks.reserve(chunkToProcess.size());
    this->DownloadBlocksFromHeights(downloadedBlocks, chunkToProcess);

    worker_pool.SubmitTask([this, capturedDownloadedBlocks = std::move(downloadedBlocks)](uint64_t c, uint64_t d, uint64_t e)
                           { 
                            this->database.StoreChunk(capturedDownloadedBlocks, c, d, e);
                            this->worker_pool.TaskCompleted(); },
                           Database::InvalidHeight, Database::InvalidHeight, Database::InvalidHeight);
}

size_t Syncer::GetNextSegmentIndex(size_t chunkEndpoint, size_t segmentStart)
{
    if (chunkEndpoint - segmentStart + 1 >= Syncer::CHUNK_SIZE)
    {
        return segmentStart + Syncer::CHUNK_SIZE - 1;
    }

    return chunkEndpoint;
}

void Syncer::DoConcurrentSyncOnRange(uint64_t rangeStart, uint64_t rangeEnd, bool isPreExistingCheckpoint)
{
    std::vector<Json::Value> downloadedBlocks;
    size_t segmentStartIndex{rangeStart}; 
    size_t segmentEndIndex{rangeEnd};   

    if (isPreExistingCheckpoint)
    {
        std::optional<Database::Checkpoint> checkpointOpt = this->database.GetCheckpoint(rangeStart);
        if (!checkpointOpt.has_value())
        {
            throw std::runtime_error("Invalid checkpoint where expected.");
        }

        segmentStartIndex = checkpointOpt.value().lastCheckpoint;
        segmentEndIndex = checkpointOpt.value().chunkEndHeight;

        downloadedBlocks.reserve(segmentEndIndex - segmentStartIndex);
        this->DownloadBlocks(downloadedBlocks, segmentStartIndex, segmentEndIndex);

        this->worker_pool.SubmitTask([this, capturedDownloadedBlocks = std::move(downloadedBlocks), segmentStartIndex, segmentEndIndex, rangeStart]
                                     { 
                                        this->database.StoreChunk(capturedDownloadedBlocks, segmentStartIndex, segmentEndIndex, rangeStart); 
                                        this->worker_pool.TaskCompleted(); });
    }
    else
    {
    
        segmentEndIndex = this->GetNextSegmentIndex(rangeEnd, segmentStartIndex);
        while (segmentStartIndex <= rangeEnd)
        {
            std::cout << "Not preexisting" << std::endl;
            std::cout << "Start: " << segmentStartIndex << std::endl;
            std::cout << "end: " << segmentEndIndex << std::endl;

            this->database.CreateCheckpointIfNonExistent(segmentStartIndex, segmentEndIndex);

            downloadedBlocks.reserve(segmentEndIndex - segmentStartIndex);
            this->DownloadBlocks(downloadedBlocks, segmentStartIndex, segmentEndIndex);

            this->worker_pool.SubmitTask([this, capturedDownloadedBlocks = std::move(downloadedBlocks), segmentStartIndex, segmentEndIndex]
                                         { 
                                        this->database.StoreChunk(capturedDownloadedBlocks, segmentStartIndex, segmentEndIndex, segmentStartIndex); 
                                        this->worker_pool.TaskCompleted(); });

            segmentStartIndex = segmentEndIndex + 1;
            segmentEndIndex = this->GetNextSegmentIndex(rangeEnd, segmentStartIndex);
        }
    }
}

void Syncer::StartSyncLoop()
{
    const std::chrono::hours syncInterval(6);

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

        std::this_thread::sleep_for(std::chrono::hours(6));
    }
}

void Syncer::SyncUnfinishedCheckpoints(std::stack<Database::Checkpoint> &checkpoints)
{

    // Sync unfinished checkpoints
    Database::Checkpoint currentCheckpoint;

    while (!checkpoints.empty())
    {
        currentCheckpoint = checkpoints.top();

        uint64_t startHeight = currentCheckpoint.chunkStartHeight;
        uint64_t endHeight = currentCheckpoint.chunkEndHeight;

        this->DoConcurrentSyncOnRange(startHeight, endHeight, true);

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
        if (!checkpoints.empty())
        {
            __DEBUG__("Syncing path: Unfinished checkpoints");
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
            __DEBUG__("Syncing path: By range");
            uint64_t startRangeChunk = this->latestBlockSynced == 0 ? this->latestBlockSynced : this->latestBlockSynced + 1;
            this->DoConcurrentSyncOnRange(true, startRangeChunk, this->latestBlockCount);
        }
        else
        {
            __DEBUG__("Syncing path: By chunk");
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
    __DEBUG__("Downloading blocks: DownloadBlocksFromHeights");
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
            __ERROR__(e.what());
            ++i;
            getblockParams.clear();
            continue;
        }
        catch (std::exception &e)
        {
            __ERROR__(e.what());
            ++i;
            getblockParams.clear();
            continue;
        }

        ++i;
    }
}

void Syncer::DownloadBlocks(std::vector<Json::Value> &downloadBlocks, uint64_t startRange, uint64_t endRange) 
{
    __DEBUG__("Downloading blocks: DownloadBlocks");

    std::lock_guard<std::mutex> lock(this->httpClientMutex);
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
            __ERROR__(e.what());
            this->database.AddMissedBlock(startRange);
            downloadBlocks.push_back(Json::nullValue);
        }
        catch (std::exception &e)
        {
            __ERROR__(e.what());
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
                __INFO__("JSON RPC starting...");
            }

            this->LoadTotalBlockCountFromChain();
        }
    }
    catch (std::exception &e)
    {
        __ERROR__(e.what());
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