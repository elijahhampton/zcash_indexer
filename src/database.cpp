#include "database.h"
#include <fstream>
#include <string>
#include <iostream>

std::mutex Database::databaseConnectionCloseMutex;
std::condition_variable Database::databaseConnectionCloseCondition;

Database::Database() {}

Database::~Database()
{
    this->ShutdownConnections();
}

bool Database::Connect(size_t poolSize, const std::string &conn_str)
{
    try
    {
        for (size_t i = 0; i < poolSize; ++i)
        {
            auto conn = std::make_unique<pqxx::connection>(conn_str);
            connectionPool.push(std::move(conn));
        }

        return true;
    }
    catch (std::exception &e)
    {
        std::cout << e.what() << std::endl;
        return false;
    }
    return false;
}

// TODO: Count number of databases used by each thread
std::unique_ptr<pqxx::connection> Database::GetConnection()
{
    try
    {
        std::unique_lock<std::mutex> lock(poolMutex);
        poolCondition.wait(lock, [this]
                           { return !connectionPool.empty(); });

        auto conn = std::move(connectionPool.front());
        connectionPool.pop();
        std::cout << "GetConnection(). poolSize=" << connectionPool.size() << std::endl;

        if (conn == nullptr || !conn->is_open())
        {
            throw std::runtime_error("------Invalid connection: connection is null or not open.--------");
        }

        return conn;
    }
    catch (std::exception &e)
    {
        std::cout << e.what() << std::endl;
        return nullptr;
    }

    return nullptr;
}

void Database::ReleaseConnection(std::unique_ptr<pqxx::connection> conn)
{
    try
    {
        if (conn == nullptr || !conn->is_open())
        {
            // Optionally handle invalid connection here
            std::cout << "ReleaseConnection: Invalid connection. Connection is null or not open." << std::endl;
            return;
        }

        std::lock_guard<std::mutex> lock(poolMutex);
        connectionPool.push(std::move(conn));
        poolCondition.notify_one();
        std::cout << "ReleaseConnection: Connection released. poolSize=" << connectionPool.size() << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cout << e.what() << std::endl;
    }
}

void Database::ShutdownConnections()
{
    // TODO
}

bool Database::CreateTables()
{
    std::unique_ptr<pqxx::connection> conn = this->GetConnection();
    std::vector<std::string> tableCreationQueries;

    std::string createBlocksTable = "CREATE TABLE blocks ("
                                    "hash TEXT PRIMARY KEY, "
                                    "height INTEGER, "
                                    "timestamp INTEGER, "
                                    "nonce TEXT,"
                                    "size INTEGER,"
                                    "num_transactions INTEGER,"
                                    "output INTEGER"
                                    ")";

    tableCreationQueries.push_back(createBlocksTable);

    std::string createTransactionsTable = "CREATE TABLE transactions ("
                                          "txid TEXT PRIMARY KEY, "
                                          "sender TEXT, "
                                          "public_output TEXT, "
                                          "fees TEXT, "
                                          "hash TEXT, "
                                          "timestamp TEXT, "
                                          "height TEXT"
                                          ")";

    tableCreationQueries.push_back(createTransactionsTable);

    std::string createChunkCheckpointTable = "CREATE TABLE checkpoints ("
                                             "chunk_start_height INTEGER PRIMARY KEY,"
                                             "chunk_end_height INTEGER,"
                                             "last_checkpoint INTEGER"
                                             ");";
    tableCreationQueries.push_back(createChunkCheckpointTable);

    std::string createTransparentInputsTableStmt = "CREATE TABLE transparent_inputs ("
                                                   "txid TEXT PRIMARY KEY, "
                                                   "vin_tx_id TEXT, "
                                                   "v_out_idx TEXT, "
                                                   "sender TEXT, "
                                                   "coinbase TEXT);";

    tableCreationQueries.push_back(createTransparentInputsTableStmt);

    std::string createTransparentOutputsTableStmt = "CREATE TABLE transparent_outputs ("
                                                    "recipients TEXT, "
                                                    "amount DOUBLE PRECISION, "
                                                    "fees DOUBLE PRECISION);";

    tableCreationQueries.push_back(createTransparentOutputsTableStmt);

    std::string createJoinsplitsTable = "CREATE TABLE joinsplits ("
                                        "txid TEXT PRIMARY KEY,"
                                        "nullifiers TEXT,"
                                        "commitments TEXT,"
                                        "vpub_old TEXT,"
                                        "vpub_new TEXT,"
                                        "anchor TEXT,"
                                        "oneTimePubKey TEXT,"
                                        "randomSeed TEXT,"
                                        "proof TEXT"
                                        ");";

    tableCreationQueries.push_back(createJoinsplitsTable);

    // TODO: batch inset
    try
    {
        for (const std::string &query : tableCreationQueries)
        {
            std::cout << query << std::endl;
            pqxx::work w(*conn.get());
            w.exec(query);
            w.commit();
        }
    }
    catch (const pqxx::sql_error &e)
    {
        std::cout << e.what() << std::endl;
        std::cout << "SQL Error code: " << e.sqlstate() << std::endl;
        uint errCode = 0;
        if (errCode != std::string::npos)
        {
            std::cerr << "Table already exists. Continuing execution." << std::endl;
            this->ReleaseConnection(std::move(conn));
            return false;
        }
        else
        {
            std::cerr << "SQL error: " << e.what() << std::endl;
            std::cerr << "Query was: " << e.query() << std::endl;
            this->ReleaseConnection(std::move(conn));
            return false;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "SQL error: " << e.what() << std::endl;
        this->ReleaseConnection(std::move(conn));
        return false;
    }

    this->ReleaseConnection(std::move(conn));

    std::cout << "Successfully created tables.." << std::endl;
    return true;
}

void Database::UpdateChunkCheckpoint(size_t chunkStartHeight, size_t currentProcessingChunkHeight)
{
    std::cout << "Updating check point: [" << chunkStartHeight << "," << currentProcessingChunkHeight << "]" << std::endl;

    // TODO: Check if checkpoint exist and throw a runtime error if it doesn't

    try
    {
        std::unique_ptr<pqxx::connection> conn = this->GetConnection();
        std::cout << "UpdateChunkPoint:After connection is captured" << std::endl;
        pqxx::work transaction(*conn.get());
        std::cout << "N" << std::endl;
        conn->prepare(
            "update_checkpoint",
            "UPDATE checkpoints "
            "SET last_checkpoint = $2 "
            "WHERE chunk_start_height = $1;");

        transaction.exec_prepared("update_checkpoint", chunkStartHeight, currentProcessingChunkHeight);
        std::cout << "Starting transaction commit" << std::endl;
        transaction.commit();
        std::cout << "Transaction committed" << std::endl;

        std::cout << "Starting unprepare" << std::endl;
        conn->unprepare("update_checkpoint");
        std::cout << "Unprepared" << std::endl;

        this->ReleaseConnection(std::move(conn));
    }
    catch (std::exception &e)
    {
        std::cout << e.what() << std::endl;
    }
}

std::optional<Database::Checkpoint> Database::GetCheckpoint(size_t chunkStartHeight)
{
            std::unique_ptr<pqxx::connection> conn = this->GetConnection();
            
    try
    {

        pqxx::work transaction(*conn.get());

        pqxx::result result = transaction.exec(
            "SELECT chunk_start_height, chunk_end_height, last_checkpoint "
            "FROM checkpoints WHERE chunk_start_height = " +
            transaction.quote(chunkStartHeight));

        transaction.commit();

        if (result.empty())
        {
            this->ReleaseConnection(std::move(conn));
             return std::nullopt;
        }
        else
        {
            pqxx::row row = result[0];

            Checkpoint checkpoint;

            checkpoint.chunkStartHeight = row["chunk_start_height"].as<size_t>();
            checkpoint.chunkEndHeight = row["chunk_end_height"].as<size_t>();
            checkpoint.lastCheckpoint = row["last_checkpoint"].as<size_t>();

            this->ReleaseConnection(std::move(conn));
             return checkpoint;
        }
    }
    catch (const pqxx::sql_error &e)
    {
        std::cerr << "SQL error: " << e.what() << std::endl;
        std::cerr << "Query was: " << e.query() << std::endl;
        this->ReleaseConnection(std::move(conn));
         throw;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        this->ReleaseConnection(std::move(conn));
         throw;
    }
}

void Database::CreateCheckpointIfNonExistent(size_t chunkStartHeight, size_t chunkEndHeight)
{
    try
    {
        std::cout << "Creating chunk checkpoint: [ " << chunkStartHeight << " - " << chunkEndHeight << " ]" << std::endl;

        std::string insertCheckpointStatement = R"(
    INSERT INTO checkpoints (
        chunk_start_height, 
        chunk_end_height, 
        last_checkpoint
    ) VALUES ($1, $2, $3);
)";

        std::unique_ptr<pqxx::connection> conn = this->GetConnection();
        conn->prepare("insert_checkpoint", insertCheckpointStatement);
        pqxx::work transaction(*conn.get());

        transaction.exec_prepared("insert_checkpoint", chunkStartHeight, chunkEndHeight, chunkStartHeight);
        transaction.commit();

        conn->unprepare("insert_checkpoint");

        this->ReleaseConnection(std::move(conn));
    }
    catch (std::exception &e)
    {
        std::cout << e.what() << std::endl;
    }
}

std::stack<Database::Checkpoint> Database::GetUnfinishedCheckpoints()
{
    std::unique_ptr<pqxx::connection> conn = this->GetConnection();
    
    try
    {
        // Obtain checkpoints
        pqxx::work transaction(*conn.get());

        std::string query = R"(
            SELECT chunk_start_height, chunk_end_height, last_checkpoint
            FROM checkpoints
            WHERE chunk_end_height != last_checkpoint
        )";

        // Execute query
        pqxx::result result = transaction.exec(query);
        transaction.commit();

        // Process the sql rows for each checkpoint
        std::stack<Checkpoint> checkpoints;
        Checkpoint currentCheckpoint;

        for (auto row : result)
        {
            currentCheckpoint.chunkStartHeight = row["chunk_start_height"].as<size_t>();
            currentCheckpoint.chunkEndHeight = row["chunk_end_height"].as<size_t>();
            currentCheckpoint.lastCheckpoint = row["last_checkpoint"].as<size_t>();
            checkpoints.push(currentCheckpoint);
        }

        this->ReleaseConnection(std::move(conn));

        // Return the found checkpoints
        return checkpoints;
    }
    catch (const pqxx::sql_error &e)
    {
        std::cerr << "SQL error: " << e.what() << std::endl;
        std::cerr << "SQLSTATE: " << e.sqlstate() << std::endl;
        this->ReleaseConnection(std::move(conn));
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << std::endl;
        this->ReleaseConnection(std::move(conn));
    }
}

void Database::AddMissedBlockToChunkCheckpoint(size_t chunkStartHeight, uint64_t blockHeight)
{
    // try
    // {
    //     std::unique_ptr<pqxx::connection> conn = this->GetConnection();
    //     pqxx::work transaction(*conn.get());

    //     std::string query =
    //         "UPDATE checkpoints "
    //         "SET missed_blocks = array_append(missed_blocks, $1) "
    //         "WHERE chunk_start_height = $2;";

    //     transaction.exec_params(query, blockHeight, chunkStartHeight);
    //     transaction.commit();
    // }
    // catch (const pqxx::sql_error &e)
    // {
    //     std::cerr << "SQL error: " << e.what() << std::endl;
    //     std::cerr << "SQLSTATE: " << e.sqlstate() << std::endl;
    //     throw std::runtime_error("Unable to add missed block to chunk checkpoint");
    // }
    // catch (const std::exception &e)
    // {
    //     std::cerr << e.what() << std::endl;
    //     throw std::runtime_error("Unable to add missed block to chunk checkpoint");
    // }
}

void Database::RemoveMissedBlockFromChunkCheckpoint(size_t chunkStartHeight, uint64_t blockHeight)
{
    // try
    // {
    //     std::unique_ptr<pqxx::connection> conn = this->GetConnection();
    //     pqxx::work transaction(*conn.get());

    //     // Prepare a statement to remove the block height from the missed_blocks array
    //     std::string stmt =
    //         R"(WITH expanded AS (
    //                 SELECT chunk_start_height, unnest(missed_blocks) AS missed_block
    //                 FROM checkpoints
    //                 WHERE chunk_start_height = $1
    //             ), filtered AS (
    //                 SELECT chunk_start_height, array_agg(missed_block) AS filtered_missed_blocks
    //                 FROM expanded
    //                 WHERE missed_block <> $2
    //                 GROUP BY chunk_start_height
    //             )
    //             UPDATE checkpoints
    //             SET missed_blocks = filtered.filtered_missed_blocks
    //             FROM filtered
    //             WHERE checkpoints.chunk_start_height = filtered.chunk_start_height
    //             AND checkpoints.chunk_start_height = $1
    //         )";

    //     transaction.exec_params(stmt, chunkStartHeight, blockHeight);
    //     transaction.commit();
    // }
    // catch (const pqxx::sql_error &e)
    // {
    //     std::cerr << "SQL error: " << e.what() << std::endl;
    //     std::cerr << "SQLSTATE: " << e.sqlstate() << std::endl;
    //     throw std::runtime_error("Unable to add missed block to chunk checkpoint");
    // }
    // catch (const std::exception &e)
    // {
    //     std::cerr << e.what() << std::endl;
    //     throw std::runtime_error("Unable to add missed block to chunk checkpoint");
    // }
}

// IMMEDIATE: In StoreChunk you have to know what is the true "range" start height
// TODO: Handle Rollback or error -> entire block is invalidated -> How to handle if one block is missed? (Solution: Add blocks to checkpoints "missed_blocks: column"
void Database::StoreChunk(bool isTrackingCheckpointForChunk, const std::vector<Json::Value> &chunk, size_t chunkStartHeight, size_t chunkEndHeight, size_t lastCheckpoint, size_t trueRangeStartHeight)
{
    std::cout << "StoreChunk("
              << "ChunkStart=" << chunkStartHeight << " "
              << "ChunkEnd=" << chunkEndHeight << " "
              << "LastCheck=" << lastCheckpoint << " "
              << "TrueRange=" << trueRangeStartHeight << std::endl;
    // size_t chunkCurrentProcessingIndex{lastCheckpoint == 0 ? chunkStartHeight : lastCheckpoint + 1};
    size_t chunkCurrentProcessingIndex{chunkStartHeight};
    std::optional<Database::Checkpoint> checkpointOpt = this->GetCheckpoint(trueRangeStartHeight);
    bool checkpointExist = checkpointOpt.has_value();
    std::unique_ptr<pqxx::connection> conn = this->GetConnection();

    conn->prepare("insert_block",
                  "INSERT INTO blocks (hash, height, timestamp, nonce, size, num_transactions, output) "
                  "VALUES ($1, $2, $3, $4, $5, $6, $7) "
                  "ON CONFLICT (hash) "
                  "DO NOTHING;");

    pqxx::work insertBlockWork(*conn.get());

    bool shouldCommitBlock{true};

    auto timeSinceLastCheckpoint = std::chrono::steady_clock::now();
    for (const auto &item : chunk)
    {

        // Check for null Json::Value in block chunk
        if (item == Json::nullValue)
        {
            // Mark as missed and add to missed blocks
            // this->AddMissedBlockToChunkCheckpoint();
            ++chunkCurrentProcessingIndex;
            continue;
        }

        try
        {
            // Parse Block Header
            const std::string nonce = item["nonce"].asString();
            const int version = item["version"].asInt();
            const std::string prevBlockHash = item["previousblockhash"].asString();
            const std::string nextBlockHash = item["nextblockhash"].asString();
            std::string merkleRoot = item["merkleroot"].asString();
            const int timestamp = item["time"].asInt();
            const int difficulty = item["difficulty"].asInt();
            const Json::Value transactions = item["tx"];
            const std::string hash = item["hash"].asString();
            const int height = item["height"].asLargestInt();
            const int size = item["size"].asInt();
            const int numTxs = item["tx"].size();

            unsigned int blockOutputAccumulator = 0;

            for (const Json::Value tx : transactions)
            {
                for (const Json::Value &voutItem : tx["vout"])
                {
                    blockOutputAccumulator += voutItem["value"].asInt();
                }
            }

            insertBlockWork.exec_prepared("insert_block", hash, height, timestamp, nonce, size, numTxs, blockOutputAccumulator);
            this->StoreTransactions(item, conn, insertBlockWork);
        }
        catch (const pqxx::sql_error &e)
        {

            if (e.sqlstate().find("duplicate key value violates unique constraint") != std::string::npos)
            {
            }
            else
            {
                std::cout << e.what() << std::endl;
                std::cout << e.sqlstate() << std::endl;
                // this->AddMissedBlockToChunkCheckpoint(chunkStartHeight, chunkCurrentProcessingIndex);
            }

            shouldCommitBlock = false;
        }
        catch (const std::exception &e)
        {
            std::cout << e.what() << std::endl;
            // this->AddMissedBlockToChunkCheckpoint(chunkStartHeight, chunkCurrentProcessingIndex);
            shouldCommitBlock = false;
        }

        // Commit the block before taking a checkpoint
        if (shouldCommitBlock)
        {
            insertBlockWork.commit();
        }
        if (isTrackingCheckpointForChunk)
        {
            // TODO: If the checkpont doesn't exist the update chunk checkpoint function shouldn't update it.. throw error
            auto now = std::chrono::steady_clock::now();
            auto elapsedTimeSinceLastCheckpoint = now - timeSinceLastCheckpoint;

            Database::Checkpoint checkpoint;
            if (checkpointExist)
            {
                checkpoint = checkpointOpt.value();
            }

            if (elapsedTimeSinceLastCheckpoint >= std::chrono::seconds(5))
            {

                this->UpdateChunkCheckpoint(checkpointExist ? checkpoint.chunkStartHeight : chunkStartHeight, chunkCurrentProcessingIndex);
                timeSinceLastCheckpoint = now;
            }

            // Check one index before the end height to account for how the vector is indexed
            if (chunkCurrentProcessingIndex == chunkEndHeight)
            {
                this->UpdateChunkCheckpoint(checkpointExist ? checkpoint.chunkStartHeight : chunkStartHeight, chunkCurrentProcessingIndex);
                break;
            }
        }

        ++chunkCurrentProcessingIndex;
        shouldCommitBlock = true;
    }

    conn->unprepare("insert_block");
    this->ReleaseConnection(std::move(conn));
}

bool Database::StoreTransactions(const Json::Value &block, const std::unique_ptr<pqxx::connection> &conn, pqxx::work &blockTransaction)
{
    if (!block["tx"].isArray())
    {
        throw std::runtime_error("Database::StoreTransactions: Invalid input for transactions. Value is not an array.");
    }

    std::stringstream ss;
    ss << std::this_thread::get_id();
    std::string curr_thread_id = ss.str();

    std::string insert_transactions_prepare = curr_thread_id + "_insert_transactions";
    std::string insert_transparent_inputs_prepare = curr_thread_id + "_insert_transparent_inputs";
    std::string insert_transparent_outputs_prepare = curr_thread_id + "_insert_transparent_outputs";
    std::string insert_joinsplits_prepare = curr_thread_id + "_insert_joinsplits";

    conn->prepare(insert_transactions_prepare,
                  R"(
        INSERT INTO transactions 
        (txid, sender, public_output, fees, hash, timestamp, height)
        VALUES ($1, $2, $3, $4, $5, $6, $7)
        ON CONFLICT (txid) 
        DO NOTHING
        )");

    conn->prepare(insert_transparent_outputs_prepare,
                  R"(
                 INSERT INTO transparent_outputs 
                 (recipients, amount, fees)
                 VALUES ($1, $2, $3)
              )");

    conn->prepare(insert_transparent_inputs_prepare,
                  R"(
                 INSERT INTO transparent_inputs 
                 (txid, vin_tx_id, v_out_idx, sender, coinbase)
                 VALUES ($1, $2, $3, $4, $5)
                 ON CONFLICT (txid) 
                 DO NOTHING
              )");

    conn->prepare(insert_joinsplits_prepare,
                  R"(
                 INSERT INTO joinsplits 
                 (txid, nullifiers, commitments, vpub_old, vpub_new, anchor, oneTimePubKey, randomSeed, proof)
                 VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9)
                 ON CONFLICT (txid) 
                 DO NOTHING
              )");

    std::string sender;
    std::string txid;
    std::string transactionData;

    // Save transactions
    if (block["tx"].size() > 0)
    {
        bool isCoinbaseTransaction = false;
        for (const Json::Value &tx : block["tx"])
        {
            try
            {
                if (!BlockValidator::ValidateTransaction(tx))
                {
                }

                const int version = block["version"].asInt();
                const std::string prevBlockHash = block["previousblockhash"].asString();
                const std::string nextBlockHash = block["previousblockhash"].asString();
                std::string merkleRoot = block["merkleroot"].asString();
                const int timestamp = block["time"].asInt();
                const int difficulty = block["difficulty"].asInt();
                const std::string nonce = block["nonce"].asString();
                const Json::Value transactions = block["tx"];
                const std::string hash = block["hash"].asString();
                const int height = block["height"].asLargestInt();

                // Transaction id
                txid = tx["txid"].asString();

                // Transaction inputs
                if (tx["vin"].size() > 0)
                {
                    std::string vin_tx_id;
                    std::string v_out_idx;
                    std::string sender{""};
                    std::string coinbase{""};

                    for (const Json::Value &input : tx["vin"])
                    {
                        try
                        {
                            // Check if input is a coinbase tx
                            if (!input["coinbase"].isNull())
                            {
                                isCoinbaseTransaction = true;
                                coinbase = input["coinbase"].asString();
                                vin_tx_id = "-1";
                                v_out_idx = "-1";
                                sender = "0x";
                            }
                            else
                            {
                                coinbase = "-1";
                                vin_tx_id = input["txid"].asString();
                                v_out_idx = input["vout"].asString();

                                std::string scriptSigAsm = "";  // input["scriptSig"]["asm"].asString();
                                std::string asmScriptType = ""; // this->identifyAsmScriptType(scriptSigAsm);

                                std::pair<std::string, std::string> signatureAndPublicKey;

                                // TODO: Handle default case and script types
                                if (asmScriptType == "P2PKH")
                                {
                                    // signatureAndPublicKey = parseP2PKHScript(input["scriptSig"]["asm"].asString());
                                    // sender = this->extractAddressFromPubKey(signatureAndPublicKey.second());
                                }
                                else if (asmScriptType == "P2PK")
                                {
                                    // signatureAndPublicKey = parseP2PKHScript(input["scriptSig"]["asm"].asString());
                                    // sender = this->extractAddressFromPubKey(signatureAndPublicKey.second());
                                }
                                else
                                {
                                    //  throw std::runtime_error("Unsupported or Unknown ASM Script type.");
                                }

                                sender = "0x"; // Temp
                            }

                            blockTransaction.exec_prepared(insert_transparent_inputs_prepare, txid, vin_tx_id, v_out_idx, sender, coinbase);
                        }
                        catch (const pqxx::sql_error &e)
                        {
                            std::cout << e.what() << std::endl;
                            std::cout << e.sqlstate() << std::endl;
                            std::cout << "Attempted to store input" << std::endl;
                            throw;
                        }
                        catch (const std::exception &e)
                        {
                            // Handle other exceptions here
                            std::cout << e.what() << std::endl;
                            throw;
                        }
                    }
                }

                size_t amount{0};
                size_t fees{0};

                // Transaction outputs
                if (tx["vout"].size() > 0)
                {
                    std::string recipients;
                    for (const Json::Value &vOutEntry : tx["vout"])
                    {
                        // TODO: Fix
                        // Value is not convertible to Int.
                        // amount = vOutEntry["value"].asInt()
                        // fees = amount - vOutEntry.asInt();

                        try
                        {
                            Json::Value vOutAddresses = vOutEntry["scriptPubKey"]["addresses"];
                            if (vOutAddresses.isArray())
                            {
                                for (const Json::Value &vOutAddress : vOutAddresses)
                                {
                                    recipients += vOutAddress.asString() + ","; // join
                                }

                                // Remove the trailing comma from the recipients list
                                if (!recipients.empty())
                                {
                                    recipients.pop_back();
                                }
                            }
                            blockTransaction.exec_prepared(insert_transparent_outputs_prepare, recipients, amount, fees);
                        }
                        catch (const pqxx::sql_error &e)
                        {
                            // Handle SQL errors here
                            std::cout << e.what() << std::endl;
                            std::cout << e.sqlstate() << std::endl;
                            throw;
                        }
                        catch (const std::exception &e)
                        {
                            // Handle other exceptions here
                            std::cout << e.what() << std::endl;
                            throw;
                        }
                    }
                }

                std::string insertJoinsplitsStmt;
                std::vector<std::string> nullifierVec;
                std::vector<std::string> commitmentsVec;
                std::string nullifiers;
                std::string commitments;
                std::string vpub_old;
                std::string vpub_new;
                std::string anchor;
                std::string oneTimePubKey;
                std::string randomSeed;
                std::string proof;

                // Transaction joinsplits
                if (tx["vjoinsplit"].isArray() && tx["vjoinsplit"].size() > 0)
                {
                    for (const Json::Value &joinsplitEntry : tx["vjoinsplit"])
                    {
                        try
                        {
                            if (joinsplitEntry["nullifiers"].isArray())
                            {
                                for (const Json::Value &nullifier : joinsplitEntry["nullifiers"])
                                {
                                    if (nullifier.isString())
                                    {
                                        nullifierVec.push_back(nullifier.asString());
                                    }
                                }
                            }

                            std::stringstream nullifiersStream;
                            copy(nullifierVec.begin(), nullifierVec.end(), std::ostream_iterator<std::string>(nullifiersStream, _delimiter));
                            nullifiers = nullifiersStream.str();

                            if (joinsplitEntry["commitments"].isArray())
                            {
                                for (const Json::Value &commitment : joinsplitEntry["commitments"])
                                {
                                    if (commitment.isString())
                                    {
                                        commitmentsVec.push_back(commitment.asString());
                                    }
                                }
                            }

                            std::ostringstream commitmentsStream;
                            std::copy(commitmentsVec.begin(), commitmentsVec.end(), std::ostream_iterator<std::string>(commitmentsStream, _delimiter));

                            vpub_old = joinsplitEntry["vpub_old"].asString();
                            vpub_new = joinsplitEntry["vpub_new"].asString();
                            anchor = joinsplitEntry["anchor"].asString();
                            oneTimePubKey = joinsplitEntry["oneTimePubKey"].asString();
                            randomSeed = joinsplitEntry["randomSeed"].asString();
                            proof = joinsplitEntry["proof"].asString();

                            blockTransaction.exec_prepared(insert_joinsplits_prepare, txid, nullifiers, commitments, vpub_old, vpub_new, anchor, oneTimePubKey, randomSeed, proof);
                        }
                        catch (const pqxx::sql_error &e)
                        {
                            // Handle SQL errors here
                            std::cout << e.what() << std::endl;
                            std::cout << e.sqlstate() << std::endl;
                            throw;
                        }
                        catch (const std::exception &e)
                        {
                            // Handle other exceptions here
                            std::cout << e.what() << std::endl;
                            throw;
                        }
                    }
                }

                blockTransaction.exec_prepared(insert_transactions_prepare, txid, sender, amount, fees, hash, timestamp, height);
            }
            catch (const pqxx::sql_error &e)
            {
                std::cout << e.what() << std::endl;
                std::cout << e.sqlstate() << std::endl;
                conn->unprepare(insert_joinsplits_prepare);
                conn->unprepare(insert_transactions_prepare);
                conn->unprepare(insert_transparent_inputs_prepare);
                conn->unprepare(insert_transparent_outputs_prepare);
                throw;
            }
            catch (const std::exception &e)
            {
                std::cout << e.what() << std::endl;
                conn->unprepare(insert_joinsplits_prepare);
                conn->unprepare(insert_transactions_prepare);
                conn->unprepare(insert_transparent_inputs_prepare);
                conn->unprepare(insert_transparent_outputs_prepare);
                throw;
            }

            // Reset local variables for the next transaction
            isCoinbaseTransaction = false;
        }
    }

    conn->unprepare(insert_joinsplits_prepare);
    conn->unprepare(insert_transactions_prepare);
    conn->unprepare(insert_transparent_inputs_prepare);
    conn->unprepare(insert_transparent_outputs_prepare);
}

unsigned int Database::GetSyncedBlockCountFromDB()
{
    std::unique_ptr<pqxx::connection> conn = this->GetConnection();

    try
    {
        pqxx::work tx(*conn.get());
        std::optional<pqxx::row> row = tx.exec1("SELECT height FROM blocks ORDER BY height DESC LIMIT 1;");
        size_t syncedBlockCount{0};
        if (row.has_value())
        {
            syncedBlockCount = row.value()[0].as<int>();
        }
        else
        {
           
            syncedBlockCount = 0;
        }

        tx.commit();
        this->ReleaseConnection(std::move(conn));
        return syncedBlockCount;
    }
    catch (std::exception &e)
    {
        std::cout << e.what() << std::endl;
        this->ReleaseConnection(std::move(conn));
        return 0;
    }
    catch (pqxx::unexpected_rows &e)
    {
        std::cout << e.what() << std::endl;
        this->ReleaseConnection(std::move(conn));
        return 0;
    }
}