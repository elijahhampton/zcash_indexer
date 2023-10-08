#include "syncer.h"

#include "httpclient.h"
#include "json/json.h"
#include <iostream>
#include <string>
#include <thread>
#include <memory>
#include <chrono>
#include <mutex>
#include <vector>
#include <sstream>
#include <boost/process.hpp>
#include <fstream>
#include "block.h"

Syncer::Syncer(CustomClient &httpClientIn, Database &databaseIn) : httpClient(httpClientIn), database(databaseIn), latestBlockSynced{0}, latestBlockCount{0}, isSyncing{false}
{
}

Syncer::~Syncer()
{
}

void Syncer::CheckAndDeleteJoinableProcessingThreads(std::vector<std::thread>& processingThreads) {
    processingThreads.erase(std::remove_if(processingThreads.begin(), processingThreads.end(),
        [](const std::thread &t) { return !t.joinable(); }), processingThreads.end());
}

void Syncer::Sync()
{
    try
    {
        this->isSyncing = true;

        // Download and process new blocks
        uint64_t numBlocks = this->latestBlockCount - this->latestBlockSynced;

        if (numBlocks == 0)
        {
            this->isSyncing = false;
            return;
        }

        std::vector<std::thread> processingThreads; 
        std::vector<Json::Value> downloadedBlocks;

        uint startRangeChunk = this->latestBlockSynced == 0 ? this->latestBlockSynced : this->latestBlockSynced + 1;
        unsigned int MAX_CONCURRENT_THREADS = std::thread::hardware_concurrency();
        constexpr size_t CHUNK_SIZE = 10000;      
        uint joinableThreadCoolOffTimeInSeconds = 10;   

        while (startRangeChunk <= this->latestBlockCount)
        {
            // Download blocks
            this->DownloadBlocks(downloadedBlocks, startRangeChunk, startRangeChunk + CHUNK_SIZE - 1);
            // Create processable chunk
            std::vector<Json::Value> chunk(downloadedBlocks.begin(), downloadedBlocks.end());

            // Max number of threads have been met
            while (processingThreads.size() >= MAX_CONCURRENT_THREADS)
            {
                bool isAtleastOneThreadJoined = false;
                while (!isAtleastOneThreadJoined)
                {
                    for (auto &thread : processingThreads)
                    {
                        if (thread.joinable())
                        {
                            thread.join();
                            isAtleastOneThreadJoined = true;
                            break;
                        }
                    }

                    if (!isAtleastOneThreadJoined)
                    {
                        std::cout << "Couldn't find a joinable thread. Waiting for " << joinableThreadCoolOffTimeInSeconds << " seconds." << std::endl;
                        std::this_thread::sleep_for(std::chrono::seconds(joinableThreadCoolOffTimeInSeconds)); 
                    } else {
                        std::cout << "Erasing processing thread. Size is now: " << processingThreads.size() << std::endl;
                        this->CheckAndDeleteJoinableProcessingThreads(processingThreads);
                    }
                }
            }

            // Launch a new thread for the current chunk
            std::cout << "Processing new block chunk starting at range: " << startRangeChunk << std::endl;
            processingThreads.emplace_back(&Syncer::ProcessBlockChunk, this, chunk, startRangeChunk);

            // All blocks processed
            downloadedBlocks.clear();

            // Don't go over the latest block count
            if ((startRangeChunk + CHUNK_SIZE) > this->latestBlockCount)
            {
                startRangeChunk = this->latestBlockCount;
            }
            else
            {
                startRangeChunk += CHUNK_SIZE;
            }
        }

        // All blocks have been synced since last sync() call. 
        // Wait for all remaining threads to complete
        for (auto &thread : processingThreads)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }

        // Erase the rest of the processing threads
        this->CheckAndDeleteJoinableProcessingThreads(processingThreads);

        // Make sure no threads exist
        if (processingThreads.size() == 0)
        {
            throw std::runtime_error("Dangling processing threads still running...");
        }

        this->isSyncing = false;
        this->LoadSyncedBlockCountFromDB();

        std::cout << "Syncing complete!\n";
    }
    catch (std::exception &e)
    {
        std::cout << e.what() << std::endl;
    }
}

void Syncer::DownloadBlocks(std::vector<Json::Value> &downloadBlocks, uint64_t startRange, uint64_t endRange)
{

    std::cout << "Downloading blocks starting at " << startRange << " and ending at " << endRange << std::endl;
    Json::Value getblockParams;
    Json::Value blockResultSerialized;
    bool success{false};

    std::lock_guard<std::mutex> lock(httpClientMutex);
    while (startRange <= endRange)
    {
        getblockParams.append(std::to_string(startRange));
        getblockParams.append(2);

        try
        {
            blockResultSerialized = httpClient.CallMethod("getblock", getblockParams);

            if (!blockResultSerialized.isNull())
            {
                downloadBlocks.push_back(blockResultSerialized);
                success = true;
            }

            blockResultSerialized.clear();
            getblockParams.clear();
        }
        catch (jsonrpc::JsonRpcException &e)
        {
            success = false;
            std::cout << e.what() << std::endl;
        }
        catch (std::exception &e)
        {
            success = false;
            std::cout << e.what() << std::endl;
        }

        if (!success)
        {
            // Queue block index for later processing
        }

        if (success != false)
        {
            success = false;
        }

        startRange++;
    }
}

void Syncer::LoadSyncedBlockCountFromDB()
{
    this->latestBlockSynced = this->database.GetSyncedBlockCountFromDB();
    std::cout << "New latest block synced: " << this->latestBlockSynced << std::endl;
}

void Syncer::LoadTotalBlockCountFromChain()
{
    try
    {
        std::lock_guard<std::mutex> lock(httpClientMutex);
        Json::Value p;
        p = Json::nullValue;
        Json::Value response = httpClient.CallMethod("getblockcount", p);
        this->latestBlockCount = response.asLargestUInt();
        std::cout << "Latest block count: " << response.asLargestInt() << std::endl;
    }
    catch (jsonrpc::JsonRpcException &e)
    {
        std::cout << e.what() << std::endl;
        if (std::string(e.what()).find("Loading block index") != std::string::npos)
        { 
            std::cout << "Still starting node..." << std::endl;
        }
    }
}

bool Syncer::ShouldSyncWallet()
{
    if (this->isSyncing)
    {
        std::cout << "Program already syncing." << std::endl;
        return false;
    }

    this->LoadTotalBlockCountFromChain();
    this->LoadSyncedBlockCountFromDB();

    if (!this->isSyncing && (this->latestBlockSynced < this->latestBlockCount))
    {
        std::cout << "Sync required.." << std::endl;
        return true;
    }

    std::cout << "No sync is required." << std::endl;
    return false;
}

void Syncer::ProcessBlockChunk(const std::vector<Json::Value> &jsonBlocks, size_t startBlockNumber)
{
    try
    {
        this->database.StoreChunk(jsonBlocks);
    }
    catch (std::exception &e)
    {
        std::cout << e.what() << std::endl;
    }
}

bool Syncer::GetSyncingStatus() const
{
    return this->isSyncing;
}