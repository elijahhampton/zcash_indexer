/**
 * Syncer
 * The sycner class always downloads a batch of blocks from the last block stored in the database to
 * the current height of the blockchain. While syncing, i.e. processing the current batch, another syncing
 * attempt will not happen.
 */

#include "../database/database.h"
#include "../http/httpclient.h"
#include "../threading/thread_pool.h"
#include <iostream>
#include <string>
#include <optional>
#include <condition_variable>
#include <mutex>
#include <algorithm>
#include <thread>
#include <memory>
#include <utility>
#include <chrono>
#include <atomic>
#include <vector>
#include <sstream>
#include <boost/process.hpp>
#include <fstream>
#include <queue>
#include "../chain_resource.h"
#include <jsonrpccpp/common/jsonparser.h>

#include "spdlog/spdlog.h"

#ifndef SYNCER_H
#define SYNCER_H

class Syncer
{

    friend class Controller;

private:
    std::atomic<uint64_t> latestBlockSynced{0};
    std::atomic<uint64_t> latestBlockCount{0};
    uint64_t maxBlocksToIndexSize{0};

    CustomClient &httpClient;
    Database &database;

    ThreadPool worker_pool;

    std::mutex http_client_mutex;
    std::mutex db_mutex;
    std::mutex cs_sync;

    std::atomic<bool> run_syncing{true};
    std::atomic<bool> run_peer_monitoring{true};
    std::atomic<bool> run_chain_info_monitoring{true};

    std::atomic<bool> isSyncing{false};


    /**
     * @brief Synchronizes a specified chunk range.
     *
     * The function performs a synchronization on a specified range. If the range
     * has not been completed during a previous sync, it will recognize its state
     * through checkpoints and continue from where it left off. Although 'start' and
     * 'end' represent the true start and end of the range, the 'chunkStart' and
     * 'chunkEnd' reflect the actual portion left to be synchronized. This methodology
     * ensures synchronization occurs in fixed ranges, defined by CHUNK_SIZE(s).
     *
     * @param start The true starting point of the range.
     * @param end The true ending point of the range.
     *
     * @return Void.
     *
     * @note The synchronization only operates in fixed ranges as defined by CHUNK_SIZE(s).
     */
    void SyncOnRange(uint64_t rangeStart, uint64_t rangeEnd, bool isPreExistingCheckpoint);
    void StartSyncLoop();

    /**
     * @brief Sync unfinished checkpoints
    */
    void SyncUnfinishedCheckpoints(std::stack<Database::Checkpoint>&);

    /**
     * @brief Syncs blocks based on a list of heights.
     *
     * @param chunkToProcess A list of block heights to process.
     */
    void SyncOnChunk(const std::vector<size_t> &chunkToProcess);

    /**
     * @brief Downloads blockchain blocks based on a list of block heights.
     *
     * This function attempts to download blocks from the blockchain for each height specified in the input vector.
     * It places the resulting blocks (or placeholders in case of failure) into the downloadedBlocks vector.
     * TODO: Implement network robustness features such as retries and backoff strategies for handling network-related issues.
     *
     * @param downloadedBlocks A reference to a vector where the downloaded blocks will be stored.
     * @param heightsToDownload A vector containing the heights of the blocks to be downloaded.
     */
    void DownloadBlocksFromHeights(std::vector<Block> &downloadedBlocks, std::vector<size_t> heightsToDownload);

    /**
     * @brief Downloads a range of blocks from the blockchain.
     *
     * Attempts to download blocks within the specified start and end range. The downloaded blocks are added to the downloadBlocks vector.
     * TODO: Implement network robustness features such as retries and backoff strategies for handling network-related issues.
     *
     * @param downloadedBlocks A reference to a vector where the downloaded blocks will be stored.
     * @param startRange The starting block height for the download.
     * @param endRange The ending block height for the download.
     */
    void DownloadBlocks(std::vector<Block> &downloadedBlocks, uint64_t startRange, uint64_t endRange);

    /**
     * @brief Loads the count of blocks that have been synced from the database.
     *
     * Retrieves the count of the latest block that has been successfully synced and stored in the database. The count is stored in the member variable latestBlockSynced.
     */
    void LoadSyncedBlockCountFromDB();

    /**
     * @brief Loads the total block count from the blockchain.
     *
     * Queries the blockchain to find the current total number of blocks and updates the latestBlockCount member variable with this value.
     */
    void LoadTotalBlockCountFromChain();

    /**
     * @brief Continuously monitors peer information.
     *
     * Periodically queries the blockchain network for peer information and stores this data in the database. The loop runs continuously until signalled to stop.
     */
    void InvokePeersListRefreshLoop() noexcept;

    /**
     * @brief Continuously monitors chain info
    */
    void InvokeChainInfoRefreshLoop() noexcept;

    /**
     * @brief Signals to stop the peer monitoring loop.
     *
     * Sets the run_peer_monitoring atomic flag to false, which will cause the peer monitoring loop to exit on its next iteration.
     */
    void StopPeerMonitoring();

    /**
     * @brief Signals to stop the syncing process.
     *
     * Sets the run_syncing atomic flag to false, which will cause the syncing loop to exit on its next iteration.
     */
    void StopSyncing();

    /**
     * @brief Stops all ongoing Syncer operations.
     *
     * Signals to stop both peer monitoring and syncing processes by calling StopPeerMonitoring and StopSyncing respectively.
     */
    void Stop();

    size_t GetNextSegmentIndex(size_t chunkEndpoint, size_t segmentStartpoint);

public:
    static constexpr uint8_t BLOCK_DOWNLOAD_VERBOSE_LEVEL = 2;
    static const uint8_t MAX_CONCURRENT_THREADS;

    /**
     * @brief Checks if the Syncer is currently in the process of syncing.
     *
     * @return True if the Syncer is currently syncing, False otherwise.
     */
    bool GetSyncingStatus() const;

    /**
     * @brief Determines if the wallet should initiate a syncing process.
     *
     * This function checks various conditions to decide whether a new syncing process should be started.
     *
     * @return True if syncing should be initiated, False otherwise.
     */
    bool ShouldSyncWallet();

    /**
     * @brief Initiates the syncing process.
     *
     * This function handles the logic to start and manage the syncing of blockchain data.
     */
    void Sync();

    /**
     * @brief Static method to initiate the syncing process.
     *
     * This static method serves as an entry point to start the syncing process, typically used when invoking from a separate thread.
     */
    static void StartSync();

    Syncer() = default;

    /**
     * @brief Constructs a Syncer object.
     *
     * Initializes a Syncer instance with the provided HTTP client and database references, setting up necessary resources for syncing operations.
     *
     * @param httpClientIn Reference to the CustomClient object for HTTP requests.
     * @param databaseIn Reference to the Database object for data storage and retrieval.
     */
    Syncer(CustomClient &httpClientIn, Database &databaseIn, uint64_t chunk_size);

    Syncer(const Syncer& syncer) noexcept = delete;
    Syncer& operator=(const Syncer& syncer) noexcept = delete;

    Syncer(Syncer&& syncer) noexcept = default;
    Syncer& operator=(Syncer&& syncer) noexcept = default;

    /**
     * @brief Destructor for the Syncer class.
     *
     * Handles any cleanup required when a Syncer object is destroyed, ensuring all resources are released properly.
     */
    ~Syncer() noexcept;
};

#endif // SYNCER_H