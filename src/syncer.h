/**
 * Syncer
 * The sycner class always downloads a batch of blocks from the last block stored in the database to
 * the current height of the blockchain. While syncing, i.e. processing the current batch, another syncing
 * attempt will not happen.
 */

#include <string>
#include "httpclient.h"
#include "database.h"
#include "json/json.h"
#include <condition_variable>
#include <mutex>
#include <algorithm>
#include <chrono>
#include <queue>

class Syncer
{
private:
    CustomClient &httpClient;
    Database &database;

    std::mutex db_mutex;
    std::mutex httpClientMutex;

    std::queue<int> missedBlocksQueue;
    std::queue<std::string> missedTransactionsQueue;

    unsigned int latestBlockSynced;
    unsigned int latestBlockCount;
    bool isSyncing;

    void CheckAndDeleteJoinableProcessingThreads(std::vector<std::thread>& processingThreads);

public:
    bool GetSyncingStatus() const;
    bool IsUpgradeBlockHeight(int blockHeight);
    bool ShouldSyncWallet();
    void Sync();
    static void StartSync();
    void DownloadBlocks(std::vector<Json::Value>& downloadBlocks, uint64_t startRange, uint64_t endRange);
    void LoadSyncedBlockCountFromDB();
    void LoadTotalBlockCountFromChain();
    void ProcessBlockChunk(const std::vector<Json::Value>& jsonBlock, size_t startBlockNumber);
    Syncer(CustomClient &httpClientIn, Database &databaseIn);
    ~Syncer();
};