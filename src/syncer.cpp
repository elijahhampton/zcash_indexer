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

size_t Syncer::CHUNK_SIZE = Config::getBlockChunkProcessingSize();
const uint8_t Syncer::MAX_CONCURRENT_THREADS = std::thread::hardware_concurrency();

Syncer::Syncer(CustomClient &httpClientIn, Database &databaseIn) : httpClient(httpClientIn), database(databaseIn), latestBlockSynced{0}, latestBlockCount{0}, isSyncing{false}, worker_pool{ThreadPool()}
{
}

Syncer::~Syncer() noexcept {}

void Syncer::DoConcurrentSyncOnChunk(const std::vector<size_t> &chunkToProcess)
{
    std::vector<Block> downloadedBlocks;
    downloadedBlocks.reserve(chunkToProcess.size());
    this->DownloadBlocksFromHeights(downloadedBlocks, chunkToProcess);

    worker_pool.SubmitTask([this, capturedDownloadedBlocks = std::move(downloadedBlocks)](uint64_t c, uint64_t d, uint64_t e) mutable
                           { 
                            this->database.BatchStoreBlocks(capturedDownloadedBlocks, c, d, e);
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
    std::vector<Block> downloadedBlocks;
    size_t segmentStartIndex{rangeStart};
    size_t segmentEndIndex{rangeEnd};

    if (isPreExistingCheckpoint)
    {
        std::optional<Database::Checkpoint> checkpointOpt = this->database.GetCheckpoint(rangeStart);
        if (!checkpointOpt.has_value())
        {
            __ERROR__(("Expected checkpoint for height: " + std::to_string(rangeStart)).c_str());
            __ERROR__("Invalid checkpoint where expected");

            throw std::runtime_error("Invalid checkpoint where expected.");
        }

        segmentStartIndex = checkpointOpt.value().lastCheckpoint;
        segmentEndIndex = checkpointOpt.value().chunkEndHeight;

        downloadedBlocks.reserve(segmentEndIndex - segmentStartIndex);
        this->DownloadBlocks(downloadedBlocks, segmentStartIndex, segmentEndIndex);

        __INFO__("Starting new thread to sync chunk for checkpoint.");
        this->worker_pool.SubmitTask([this, capturedDownloadedBlocks = std::move(downloadedBlocks), segmentStartIndex, segmentEndIndex, rangeStart] mutable
                                     { 
                                        this->database.BatchStoreBlocks(capturedDownloadedBlocks, segmentStartIndex, segmentEndIndex, rangeStart); 
                                        this->worker_pool.TaskCompleted(); });
    }
    else
    {

        segmentEndIndex = this->GetNextSegmentIndex(rangeEnd, segmentStartIndex);
        while (segmentStartIndex <= rangeEnd)
        {
            this->database.CreateCheckpointIfNonExistent(segmentStartIndex, segmentEndIndex);

            downloadedBlocks.reserve(segmentEndIndex - segmentStartIndex);
            this->DownloadBlocks(downloadedBlocks, segmentStartIndex, segmentEndIndex);

            __INFO__("Starting new thread to sync chunk.");
            this->worker_pool.SubmitTask([this, capturedDownloadedBlocks = std::move(downloadedBlocks), segmentStartIndex, segmentEndIndex] mutable
                                         { 
                                        this->database.BatchStoreBlocks(capturedDownloadedBlocks, segmentStartIndex, segmentEndIndex, segmentStartIndex); 
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
            __ERROR__(e.what());
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
            __ERROR__(e.what());
        }

        std::this_thread::sleep_for(std::chrono::hours(6));
    }
}

void Syncer::SyncUnfinishedCheckpoints(std::stack<Database::Checkpoint> &checkpoints)
{

    // Sync unfinished checkpoints
    Database::Checkpoint currentCheckpoint;

    __DEBUG__(("Syncing " + std::to_string(checkpoints.size()) + " checkpoints.").c_str());
    while (!checkpoints.empty())
    {
        currentCheckpoint = checkpoints.top();

        uint64_t startHeight = currentCheckpoint.chunkStartHeight;
        uint64_t endHeight = currentCheckpoint.chunkEndHeight;

        __DEBUG__(("Starting sync on checkpoint with start block height: " + std::to_string(startHeight)).c_str());
        this->DoConcurrentSyncOnRange(startHeight, endHeight, true);

        checkpoints.pop();
    }
}

void Syncer::Sync()
{
    try
    {
        this->isSyncing = true;

        if (!this->worker_pool.isEmpty())
        {
            this->worker_pool.RefreshThreadPool();
        }

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
        uint64_t num_blocks_to_index = this->latestBlockCount - this->latestBlockSynced;

        if (num_blocks_to_index == 0)
        {
            this->isSyncing = false;
            return;
        }
        else if (num_blocks_to_index >= CHUNK_SIZE)
        {
            __DEBUG__("Syncing path: By range");
            uint64_t startRangeChunk = this->latestBlockSynced == 0 ? this->latestBlockSynced : this->latestBlockSynced + 1;
            this->DoConcurrentSyncOnRange(true, startRangeChunk, this->latestBlockCount);
        }
        else
        {
            __DEBUG__("Syncing path: By chunk");
            std::vector<size_t> heights;
            heights.reserve(num_blocks_to_index);

            for (size_t i = 1; i <= num_blocks_to_index; ++i)
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
        __ERROR__(e.what());
    }
}

void Syncer::DownloadBlocksFromHeights(std::vector<Block> &downloadedBlocks, std::vector<size_t> heightsToDownload)
{
    __DEBUG__("Downloading blocks: DownloadBlocksFromHeights");
    auto numHeightsToDownload{heightsToDownload.size()};
    if (numHeightsToDownload > Syncer::CHUNK_SIZE)
    {
        throw std::runtime_error("Desired download size is greater than allowed per configuration");
    }

    Json::Value getblockParams;
    Json::Value blockResultSerialized;

    std::lock_guard<std::mutex> lock(this->httpClientMutex);
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
                throw std::exception();
            }

            downloadedBlocks.emplace_back(Block(blockResultSerialized));
        }
        catch (jsonrpc::JsonRpcException &e)
        {
            __ERROR__(e.what());
            this->database.AddMissedBlock(i);
            downloadedBlocks.emplace_back(Block());
        }

        blockResultSerialized.clear();
        getblockParams.clear();
        ++i;
    }
}

void Syncer::DownloadBlocks(std::vector<Block> &downloadedBlocks, uint64_t startRange, uint64_t endRange)
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

            downloadedBlocks.emplace_back(Block(blockResultSerialized));
        }
        catch (jsonrpc::JsonRpcException &e)
        {
            __ERROR__(e.what());
            this->database.AddMissedBlock(startRange);
            downloadedBlocks.emplace_back(Block());
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

    return this->latestBlockSynced < this->latestBlockCount;
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
    this->worker_pool.RefreshThreadPool();
    this->worker_pool.End();
}