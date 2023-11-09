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
                                    "output DOUBLE PRECISION"
                                    ")";

    tableCreationQueries.push_back(createBlocksTable);

    std::string createTransactionsTable = "CREATE TABLE transactions ("
                                          "tx_id TEXT PRIMARY KEY, "
                                          "public_output TEXT, "
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
                                                   "tx_id TEXT PRIMARY KEY, "
                                                   "vin_tx_id TEXT, "
                                                   "v_out_idx INTEGER, "
                                                   "coinbase TEXT);";

    tableCreationQueries.push_back(createTransparentInputsTableStmt);

    std::string createTransparentOutputsTableStmt = "CREATE TABLE transparent_outputs ("
                                                    "tx_id TEXT, "
                                                    "output_index INTEGER, "
                                                    "recipients TEXT[], "
                                                    "value DOUBLE PRECISION);";

    tableCreationQueries.push_back(createTransparentOutputsTableStmt);

    // TODO: batch inset
    try
    {
        for (const std::string &query : tableCreationQueries)
        {
            try {
                pqxx::work w(*conn.get());
                w.exec(query);
                w.commit();
            } catch(const pqxx::sql_error &e) {
                if (e.sqlstate() == "42P07") {
                    std::cerr << "Table already exists: " << e.what() << std::endl;
                    continue;
                } else {
                    std::cout << "A@@@" << std::endl;
                    throw;
                }
            }
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

std::optional<Database::Checkpoint> Database::GetCheckpoint(signed int chunkStartHeight) 
{
    if (chunkStartHeight == -1) {
        return std::nullopt;
    }

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
        std::cout << e.what() << std::endl;
        this->ReleaseConnection(std::move(conn));
    }
    catch (const std::exception &e)
    {
        std::cout << e.what() << std::endl;
        this->ReleaseConnection(std::move(conn));
    }
}

void Database::AddMissedBlock(size_t blockHeight)
{
}

void Database::RemoveMissedBlock(size_t blockHeight)
{
}

void Database::StoreChunk(bool isTrackingCheckpointForChunk, const std::vector<Json::Value> &chunk, signed int chunkStartHeight, signed int chunkEndHeight, signed int lastCheckpoint, signed int trueRangeStartHeight)
{
    std::cout << "StoreChunk("
              << "ChunkStart=" << chunkStartHeight << " "
              << "ChunkEnd=" << chunkEndHeight << " "
              << "LastCheck=" << lastCheckpoint << " "
              << "TrueRange=" << trueRangeStartHeight << std::endl;
    // size_t chunkCurrentProcessingIndex{lastCheckpoint == 0 ? chunkStartHeight : lastCheckpoint + 1};
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
    size_t chunkCurrentProcessingIndex{static_cast<size_t>(chunkStartHeight)};
    for (const auto &item : chunk)
    {

        // Check for null Json::Value in block chunk
        if (item == Json::nullValue)
        {
            // Mark as missed and add to missed blocks
            this->AddMissedBlock(item["height"].asLargestInt());
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

            double blockOutputAccumulator = 0;

            for (const Json::Value tx : transactions)
            {
                for (const Json::Value &voutItem : tx["vout"])
                {
                    blockOutputAccumulator += voutItem["value"].asDouble();
                }
            }
 
           // insertBlockWork.exec_prepared("insert_block", hash, height, timestamp, nonce, size, numTxs, blockOutputAccumulator);
            this->StoreTransactions(item, conn, insertBlockWork);
        }
        catch (const pqxx::sql_error &e)
        {
            std::cout << e.what() << std::endl;

            if (e.sqlstate().find("duplicate key value violates unique constraint") != std::string::npos)
            {
            }
            else
            {
            }

            this->AddMissedBlock(item["height"].asLargestInt());
            shouldCommitBlock = false;
        }
        catch (const std::exception &e)
        {
            std::cout << e.what() << std::endl;
            this->AddMissedBlock(item["height"].asLargestInt());
            shouldCommitBlock = false;
        }

        // Commit the block before taking a checkpoint
        if (shouldCommitBlock)
        {

            insertBlockWork.commit();
        }
        if (isTrackingCheckpointForChunk)
        {
            std::cout << "Handling checkpoint tracking." << std::endl;
            // TODO: If the checkpont doesn't exist the update chunk checkpoint function shouldn't update it.. throw error
            auto now = std::chrono::steady_clock::now();
            auto elapsedTimeSinceLastCheckpoint = now - timeSinceLastCheckpoint;

            Database::Checkpoint checkpoint;
            if (checkpointExist)
            {
                checkpoint = checkpointOpt.value();

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

        }

        ++chunkCurrentProcessingIndex;
        shouldCommitBlock = true;
    }

    conn->unprepare("insert_block");
    this->ReleaseConnection(std::move(conn));
}

void Database::StoreTransactions(const Json::Value &block, const std::unique_ptr<pqxx::connection> &conn, pqxx::work &blockTransaction)
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

    conn->prepare(insert_transactions_prepare,
                  R"(
        INSERT INTO transactions 
        (tx_id, public_output, hash, timestamp, height)
        VALUES ($1, $2, $3, $4, $5)
        ON CONFLICT (tx_id) 
        DO NOTHING
        )");

    conn->prepare(insert_transparent_outputs_prepare,
                  R"(
                 INSERT INTO transparent_outputs 
                 (tx_id, output_index, recipients, value)
                 VALUES ($1, $2, $3, $4)
              )");

    conn->prepare(insert_transparent_inputs_prepare,
                  R"(
                 INSERT INTO transparent_inputs 
                 (tx_id, vin_tx_id, v_out_idx, coinbase)
                 VALUES ($1, $2, $3, $4)
                 ON CONFLICT (tx_id) 
                 DO NOTHING
              )");

    std::cout << "StoreTransactions: " << block["tx"].size() << std::endl;
    // Save transactions
    if (block["tx"].size() > 0)
    {
        bool isCoinbaseTransaction = false;
        std::string txid{""};
        int version;
        std::string prevBlockHash;
        std::string nextBlockHash;
        std::string merkleRoot;
        int timestamp;
        int difficulty;
        std::string nonce;
        std::string hash;
        int height;
        Json::Value transactions;

        std::cout << "Starting transactions" << std::endl;
        for (const Json::Value &tx : block["tx"])
        {
            try
            {
                version = block["version"].asInt();
                prevBlockHash = block["previousblockhash"].asString();
                nextBlockHash = block["previousblockhash"].asString();
                merkleRoot = block["merkleroot"].asString();
                timestamp = block["time"].asInt();
                difficulty = block["difficulty"].asInt();
                std::string nonce = block["nonce"].asString();
                Json::Value transactions = block["tx"];
                std::string hash = block["hash"].asString();
                height = block["height"].asLargestInt();

                std::cout << txid << std::endl;
                // Transaction id
                txid = tx["txid"].asString();

                // Transaction inputs
                if (tx["vin"].size() > 0)
                {
                    std::string vin_tx_id;
                    uint32_t v_out_idx;
                    std::string coinbase{""};

                    for (const Json::Value &input : tx["vin"])
                    {
                        try
                        {
                            // Check if input is a coinbase tx
                            if (input.isMember("coinbase"))
                            {
                                isCoinbaseTransaction = true;
                                coinbase = input["coinbase"].asString();
                                vin_tx_id = "-1";
                                v_out_idx = 0; // TODO: Find a better way to represent v_out_idx for coinbase transactions.
                            }
                            else
                            {
                                coinbase = "-1";
                                vin_tx_id = input["txid"].asString();
                                v_out_idx = input["vout"].asInt();
                            }

                            blockTransaction.exec_prepared(insert_transparent_inputs_prepare, txid, vin_tx_id, v_out_idx, coinbase);
                        }
                        catch (const pqxx::sql_error &e)
                        {
                            std::cout << e.what() << std::endl;
                            throw;
                        }
                        catch (const std::exception &e)
                        {
                            std::cout << e.what() << std::endl;
                            throw;
                        }
                    }
                }

                double currentOutputValue{0};
                double totalTransactionOutput{0};

                // Transaction outputs
                if (tx["vout"].size() > 0)
                {
                    size_t outputIndex{0};
                    std::vector<std::string> recipients;
                    for (const Json::Value &vOutEntry : tx["vout"])
                    {
                        outputIndex = vOutEntry["n"].asLargestInt();
                        currentOutputValue = vOutEntry["value"].asDouble();
                        totalTransactionOutput += currentOutputValue;

                        try
                        {
                            Json::Value vOutAddresses = vOutEntry["scriptPubKey"]["addresses"];
                            std::string recipientList = "{";
                            if (vOutAddresses.isArray() && vOutAddresses.size() > 0)
                            {
                                for (const Json::Value &vOutAddress : vOutAddresses)
                                {
                                    recipients.push_back(vOutAddress.asString());
                                }

                                
                                if (!recipients.empty()) {
                                    recipientList += "\"" + recipients[0] + "\"";
                                    for (size_t i = 1; i < recipients.size(); ++i) {
                                        recipientList += ",\"" + recipients[i] + "\"";
                                    }
                                }
                                

                            }
                            recipientList += "}";
                            blockTransaction.exec_prepared(insert_transparent_outputs_prepare, txid, outputIndex, recipientList, currentOutputValue);
                        }
                        catch (const pqxx::sql_error &e)
                        {
                            std::cout << e.what() << std::endl;
                            throw;
                        }
                        catch (const std::exception &e)
                        {
                            std::cout << e.what() << std::endl;
                            throw;
                        }
                    }
                }

                blockTransaction.exec_prepared(insert_transactions_prepare, txid, currentOutputValue, hash, timestamp, height);
            }
            catch (const pqxx::sql_error &e)
            {
                std::cout << e.what() << std::endl;
                conn->unprepare(insert_transactions_prepare);
                conn->unprepare(insert_transparent_inputs_prepare);
                conn->unprepare(insert_transparent_outputs_prepare);
                throw;
            }
            catch (const std::exception &e)
            {
                 std::cout << e.what() << std::endl;
                conn->unprepare(insert_transactions_prepare);
                conn->unprepare(insert_transparent_inputs_prepare);
                conn->unprepare(insert_transparent_outputs_prepare);
                throw;
            }

            // Reset local variables for the next transaction
            isCoinbaseTransaction = false;
        }
    }

    std::cout << "d" << std::endl;
    conn->unprepare(insert_transactions_prepare);
    conn->unprepare(insert_transparent_inputs_prepare);
    conn->unprepare(insert_transparent_outputs_prepare);
    std::cout << "o" << std::endl;
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
