/**
 * Syncer
 * The sycner class always downloads a batch of blocks from the last block stored in the database to
 * the current height of the blockchain. While syncing, i.e. processing the current batch, another syncing
 * attempt will not happen.
 */

#include "database.h"
#include "block.h"
#include "httpclient.h"
#include "json/json.h"
#include <iostream>
#include <string>
#include <optional>
#include <condition_variable>
#include <mutex>
#include <algorithm>
#include <thread>
#include <memory>
#include <chrono>
#include <vector>
#include <sstream>
#include <boost/process.hpp>
#include <fstream>
#include <queue>

class Syncer
{
private:
    CustomClient &httpClient;
    Database &database;

    std::mutex db_mutex;
    std::mutex httpClientMutex;

    unsigned int latestBlockSynced;
    unsigned int latestBlockCount;
    bool isSyncing;

    /**
     * @brief Syncs blocks based on a given range of heights.
     * 
     * @param start The start height of the block in the range
     * @param end The end height of the block in the range
    */
    void DoConcurrentSyncOnRange(bool isTrackingCheckpointForChunks, uint start, uint end);
    
    /**
     * @brief Syncs blocks based on a list of heights.
     * 
     * @param chunkToProcess A list of block heights to process.
    */
    void DoConcurrentSyncOnChunk(std::vector<size_t> chunkToProcess);

    void DownloadBlocksFromHeights(std::vector<Json::Value> &downloadedBlocks, std::vector<size_t> heightsToDownload);
    void CheckAndDeleteJoinableProcessingThreads(std::vector<std::thread>& processingThreads);
    void DownloadBlocks(std::vector<Json::Value>& downloadBlocks, uint64_t startRange, uint64_t endRange);
    void LoadSyncedBlockCountFromDB();
    void LoadTotalBlockCountFromChain();

public:
    static size_t CHUNK_SIZE;
    bool GetSyncingStatus() const;
    bool IsUpgradeBlockHeight(int blockHeight);
    bool ShouldSyncWallet();
    void Sync();
    static void StartSync();


    Syncer(CustomClient &httpClientIn, Database &databaseIn);
    ~Syncer();
};