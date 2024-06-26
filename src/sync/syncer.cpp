#include "../sync/syncer.h"
#include <algorithm>
#include "../http/httpclient.h"
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

const uint8_t Syncer::MAX_CONCURRENT_THREADS = std::thread::hardware_concurrency();

Syncer::Syncer(CustomClient &httpClientIn, Database &databaseIn, uint64_t chunk_size) : httpClient(httpClientIn), database(databaseIn), latestBlockSynced{0}, latestBlockCount{0}, isSyncing{false}, worker_pool{ThreadPool()}, maxBlocksToIndexSize{chunk_size}
{
}

Syncer::~Syncer() noexcept {}

void Syncer::SyncOnChunk(const std::vector<size_t> &chunk_to_process)
{
    std::vector<Block> downloadedBlocks(chunk_to_process.size());
    this->DownloadBlocksFromHeights(downloadedBlocks, chunk_to_process);

    worker_pool.SubmitTask([this, downloadedBlocks = std::move(downloadedBlocks)](uint64_t c, uint64_t d, uint64_t e) mutable
                           { 
                            this->database.BatchStoreBlocks(std::move(downloadedBlocks), c, d, e);
                            this->worker_pool.TaskCompleted(); },
                           Database::InvalidHeight, Database::InvalidHeight, Database::InvalidHeight);
}

size_t Syncer::GetNextSegmentIndex(size_t chunkEndpoint, size_t segmentStart)
{
    if (chunkEndpoint - segmentStart + 1 >= Syncer::maxBlocksToIndexSize)
    {
        return segmentStart + Syncer::maxBlocksToIndexSize - 1;
    }

    return chunkEndpoint;
}

void Syncer::SyncOnRange(uint64_t rangeStart, uint64_t rangeEnd, bool isPreExistingCheckpoint)
{
    std::vector<Block> downloadedBlocks;
    size_t segmentStartIndex{rangeStart};
    size_t segmentEndIndex{rangeEnd};

    if (isPreExistingCheckpoint)
    {
        std::optional<Database::Checkpoint> checkpointOpt = this->database.GetCheckpoint(rangeStart);
        if (!checkpointOpt.has_value())
        {
            spdlog::error(("Expected checkpoint for height: " + std::to_string(rangeStart)).c_str());
            spdlog::error("Invalid checkpoint where expected");
            exit(1);
            throw std::runtime_error("Invalid checkpoint where expected.");
        }

        segmentStartIndex = checkpointOpt.value().lastCheckpoint;
        segmentEndIndex = checkpointOpt.value().chunkEndHeight;

        downloadedBlocks.reserve(segmentEndIndex - segmentStartIndex);
        this->DownloadBlocks(downloadedBlocks, segmentStartIndex, segmentEndIndex);

        spdlog::info("Starting new thread to sync chunk for checkpoint.");
        this->worker_pool.SubmitTask([this, capturedDownloadedBlocks = std::move(downloadedBlocks), segmentStartIndex, segmentEndIndex, rangeStart] () mutable { 
                this->database.BatchStoreBlocks(std::move(capturedDownloadedBlocks), segmentStartIndex, segmentEndIndex, rangeStart); 
                this->worker_pool.TaskCompleted(); 
        });
    }
    else
    {

        segmentEndIndex = this->GetNextSegmentIndex(rangeEnd, segmentStartIndex);
        while (segmentStartIndex <= rangeEnd)
        {
            this->database.CreateCheckpointIfNonExistent(segmentStartIndex, segmentEndIndex);
        
            downloadedBlocks.clear();
            downloadedBlocks.reserve(segmentEndIndex - segmentStartIndex);
            this->DownloadBlocks(downloadedBlocks, segmentStartIndex, segmentEndIndex);

            spdlog::info("Starting new thread to sync chunk.");
            this->worker_pool.SubmitTask([this, capturedDownloadedBlocks = std::move(downloadedBlocks), segmentStartIndex, segmentEndIndex] () mutable
                                         { 
                                        this->database.BatchStoreBlocks(std::move(capturedDownloadedBlocks), segmentStartIndex, segmentEndIndex, segmentStartIndex); 
                                        this->worker_pool.TaskCompleted(); });

            segmentStartIndex = segmentEndIndex + 1;
            segmentEndIndex = this->GetNextSegmentIndex(rangeEnd, segmentStartIndex);
        }
    }
}

void Syncer::StartSyncLoop()
{
    const std::chrono::hours syncInterval(6);

    while (run_syncing)
    {
        std::lock_guard<std::mutex> syncLock(cs_sync);

        if (ShouldSyncWallet())
        {
            Sync();
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
            std::lock_guard<std::mutex> lock(http_client_mutex);
            Json::Value peer_info = this->httpClient.getpeerinfo();
            this->database.StorePeers(peer_info);
        }
        catch (const std::exception &e)
        {
            spdlog::error(e.what());
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
            std::lock_guard<std::mutex> lock(http_client_mutex);
            Json::Value chain_info = this->httpClient.getblockchaininfo();
            this->database.StoreChainInfo(chain_info);
        }
        catch (const std::exception &e)
        {
            spdlog::error(e.what());
        }

        std::this_thread::sleep_for(std::chrono::hours(6));
    }
}

void Syncer::SyncUnfinishedCheckpoints(std::stack<Database::Checkpoint> &checkpoints)
{

    // Sync unfinished checkpoints
    Database::Checkpoint currentCheckpoint;

    spdlog::debug(("Syncing " + std::to_string(checkpoints.size()) + " checkpoints.").c_str());
    while (!checkpoints.empty())
    {
        currentCheckpoint = checkpoints.top();

        uint64_t startHeight = currentCheckpoint.chunkStartHeight;
        uint64_t endHeight = currentCheckpoint.chunkEndHeight;

        spdlog::debug(("Starting sync on checkpoint with start block height: " + std::to_string(startHeight)).c_str());
        this->SyncOnRange(startHeight, endHeight, true);

        checkpoints.pop();
    }
}

void Syncer::Sync()
{
    try
    {
        this->isSyncing = true;

        Allow any threads in the thread pool to complete before starting a new syncing session.
        if (!this->worker_pool.isEmpty()) {
            this->worker_pool.RefreshThreadPool();
        }

        Complete any previous attempts to sync chunks of blocks before starting the new session
        std::stack<Database::Checkpoint> checkpoints = this->database.GetUnfinishedCheckpoints();
        if (!checkpoints.empty())
        {
            spdlog::debug("Syncing path: Unfinished checkpoints");
            this->SyncUnfinishedCheckpoints(checkpoints);
            this->worker_pool.RefreshThreadPool();
        }
        
        // Set the current total block count in the chain
        this->LoadTotalBlockCountFromChain();
        // Set the latest synced block
        this->LoadSyncedBlockCountFromDB();

        uint64_t latestBlockCountVal = this->latestBlockCount.load();
        uint64_t latestSyncedBlockHeightVal = this->latestBlockSynced.load();
        uint64_t numNewBlocks = latestBlockCountVal - latestSyncedBlockHeightVal;

        if (numNewBlocks == 0)
        {
            isSyncing = false;
            return;
        }
        else if (numNewBlocks >= maxBlocksToIndexSize)
        {
            spdlog::debug("Syncing path: By range");

            uint64_t startRangeChunk = latestSyncedBlockHeightVal == 0 ? latestSyncedBlockHeightVal : latestSyncedBlockHeightVal + 1;
            this->SyncOnRange(startRangeChunk, latestBlockCountVal, false);
        }
        else
        {
            spdlog::debug("Syncing path: By chunk");
            
            size_t block_chunk = latestBlockCountVal - latestSyncedBlockHeightVal;
            std::vector<size_t> heights(block_chunk);
            
            // Populate the vector with the appropriate heights starting at the latest block synced
            std::iota(heights.begin(), heights.end(), latestSyncedBlockHeightVal + 1);
            this->SyncOnChunk(std::move(heights));
        }

        // Refresh the worker thread pool and allow all work to complete before ending the sync
        this->worker_pool.RefreshThreadPool();
        this->isSyncing = false;
    }
    catch (std::exception &e)
    {
        // Throw runtime error if syncing operation fails
        throw std::runtime_error(e.what());
    }
}

void Syncer::DownloadBlocksFromHeights(std::vector<Block> &downloadedBlocks, std::vector<size_t> heightsToDownload)
{
    size_t numHeightsToDownload(heightsToDownload.size());
    if (numHeightsToDownload > Syncer::maxBlocksToIndexSize)
    {
        throw std::runtime_error("Desired download size is greater than allowed per configuration");
    }

    Json::Value get_block_params;
    Json::Value block_result_serialized;

    std::lock_guard<std::mutex> lock(http_client_mutex);
    size_t currentHeightToDownload{0};
    while (currentHeightToDownload < numHeightsToDownload)
    {
        get_block_params.append(Json::Value(std::to_string(heightsToDownload.at(currentHeightToDownload))));
        get_block_params.append(Json::Value(2));

        try
        {
            block_result_serialized = httpClient.CallMethod("getblock", get_block_params);

            if (block_result_serialized.isNull())
            {
                downloadedBlocks.push_back(Block());
                throw new std::exception();
            } 
    
            downloadedBlocks.push_back(Block{block_result_serialized});
        }
        catch (std::exception &e)
        {
            spdlog::error(e.what());
            this->database.AddMissedBlock(currentHeightToDownload);
        }
        
        ++currentHeightToDownload;
        get_block_params.clear();
        block_result_serialized.clear();
    }
}

void Syncer::DownloadBlocks(std::vector<Block> &downloadedBlocks, uint64_t startRange, uint64_t endRange)
{
    std::lock_guard<std::mutex> lock(http_client_mutex);
    Json::Value get_block_params{Json::nullValue};
    Json::Value block_result_serialized{Json::nullValue};

    while (startRange <= endRange)
    {
        get_block_params.append(Json::Value(std::to_string(startRange)));
        get_block_params.append(Json::Value(Syncer::BLOCK_DOWNLOAD_VERBOSE_LEVEL));

        try
        {
            block_result_serialized = httpClient.CallMethod("getblock", get_block_params);

            if (block_result_serialized.isNull())
            {
                downloadedBlocks.push_back(Block());
                throw new std::exception();
            }

            downloadedBlocks.push_back(Block{block_result_serialized});
        }
        catch (std::exception &e)
        {
            spdlog::error(e.what());
            this->database.AddMissedBlock(startRange);
        }

        block_result_serialized.clear();
        get_block_params.clear();
        ++startRange;
    }
}

void Syncer::LoadSyncedBlockCountFromDB()
{
    try 
    {
        this->latestBlockSynced.store(this->database.GetSyncedBlockCountFromDB());
        spdlog::debug(("New synced block count " + std::to_string(this->latestBlockSynced)).c_str());
    }
    catch (std::exception &e)
    {
        spdlog::error(e.what());
        exit(1);
    }
}

void Syncer::LoadTotalBlockCountFromChain()
{
    try
    {
        std::lock_guard<std::mutex> lock(http_client_mutex);
        this->latestBlockCount.store(httpClient.getblockcount().asLargestUInt());
    }
    catch (jsonrpc::JsonRpcException &e)
    {
        const std::string errorMessage = e.what();
        if (errorMessage.find("Loading block index") != std::string::npos
            || errorMessage.find("Verifying blocks") != std::string::npos
            || errorMessage.find("Rewinding blocks if needed") != std::string::npos
            || errorMessage.find("Starting network threads") != std::string::npos
            || errorMessage.find("Done loading") != std::string::npos)
        {
            spdlog::info("JSON RPC starting...");

            // Wait for 10 seconds before retrying
            std::this_thread::sleep_for(std::chrono::seconds(10)); 

            // Retry
            this->LoadTotalBlockCountFromChain(); 
        } else {
            throw e;
        }
    }
    catch (std::exception &e)
    {
        spdlog::error(e.what());
        exit(1);
    }
}

bool Syncer::ShouldSyncWallet()
{
    if (isSyncing)
    {
        return false;
    }

    LoadTotalBlockCountFromChain();
    LoadSyncedBlockCountFromDB();

    if (!isSyncing && (latestBlockSynced.load() < latestBlockCount.load()))
    {
        return true;
    }

    return false;
}

bool Syncer::GetSyncingStatus() const
{
    return isSyncing;
}

void Syncer::StopPeerMonitoring()
{
    run_peer_monitoring = false;
}

void Syncer::StopSyncing()
{
    run_syncing = false;
}

void Syncer::Stop()
{
    StopPeerMonitoring();
    StopSyncing();
    worker_pool.End();
}