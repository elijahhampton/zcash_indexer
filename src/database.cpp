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

bool Database::Connect(size_t poolSize, std::string conn_str)
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

std::unique_ptr<pqxx::connection> Database::GetConnection()
{
    std::lock_guard<std::mutex> lock(poolMutex);
    if (connectionPool.empty())
    {
        return nullptr;
    }
    auto conn = std::move(connectionPool.front());
    connectionPool.pop();
    return conn;
}

std::string Database::LoadConfig(const std::string &path)
{

    // std::ifstream cfg_file(path);

    // if (!cfg_file.is_open()) {
    //     std::cerr << "Failed to open the config file at: " << path << std::endl;
    //     return "";
    // }

    // std::string conn_str;
    // std::getline(cfg_file, conn_str);

    // cfg_file.close();

    // if (conn_str.empty()) {
    //     std::cerr << "Config file is empty or doesn't contain any valid lines." << std::endl;
    //     return "";
    // }
    return "dbname=postgres user=postgres password=mysecretpassword host=127.0.0.1 port=5432";
}

bool Database::ReleaseConnection(std::unique_ptr<pqxx::connection> conn)
{
    std::lock_guard<std::mutex> lock(poolMutex);
    connectionPool.push(std::move(conn));
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
                                    "id TEXT PRIMARY KEY, "
                                    "block_number INTEGER, "
                                    "time INTEGER, "
                                    "nonce TEXT"
                                    ")";

    tableCreationQueries.push_back(createBlocksTable);

    std::string createTransactionsTable = "CREATE TABLE transactions ("
                                          "txid TEXT PRIMARY KEY, "
                                          "block_height TEXT, "
                                          "sender TEXT, "
                                          "amount TEXT, "
                                          "fees TEXT, "
                                          "hash TEXT, "
                                          "prevBlockHash TEXT, "
                                          "nextBlockHash TEXT, "
                                          "version TEXT, "
                                          "merkleRoot TEXT, "
                                          "timestamp TEXT, "
                                          "difficulty TEXT, "
                                          "nonce TEXT, "
                                          "height TEXT"
                                          ")";

    tableCreationQueries.push_back(createTransactionsTable);

    std::string createTransparentInputsTableStmt = "CREATE TABLE transparent_inputs ("
                                                   "txid TEXT, "
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
                                        "txid TEXT,"
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

    // Open a connection to the PostgreSQL database
    try
    {
        for (const std::string &query : tableCreationQueries)
        {
            pqxx::work w(*conn.get());
            w.exec(query);
            w.commit();
        }
    }
    catch (const pqxx::sql_error &e)
    {
        std::cerr << "SQL Error code: " << e.sqlstate() << std::endl;
        uint errCode = 0;
        if (errCode != std::string::npos)
        {
            std::cerr << "Table already exists. Continuing execution." << std::endl;
        }
        else
        {
            std::cerr << "SQL error: " << e.what() << std::endl;
            std::cerr << "Query was: " << e.query() << std::endl;
            return false;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "SQL error: " << e.what() << std::endl;
        return false;
    }

    this->ReleaseConnection(std::move(conn));

    std::cout << "Successfully created tables.." << std::endl;
    return true;
}

void Database::StoreChunk(const std::vector<Json::Value> &chunk)
{
    std::cout << "Starting new chunk with size: " << chunk.size() << std::endl;
    try
    {
        std::lock_guard<std::mutex> lock(db_mutex);
        std::unique_ptr<pqxx::connection> conn = this->GetConnection();

        pqxx::work insertBlockWork(*conn.get());
        conn->prepare("insert_block",
                      "INSERT INTO blocks (id, block_number, time, nonce) VALUES ($1, $2, $3, $4)");

        for (const auto &item : chunk)
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

            insertBlockWork.exec_prepared("insert_block", hash, height, timestamp, nonce);

            this->StoreTransactions(item);
        }

        insertBlockWork.commit();

        conn->unprepare("insert_block");
    }
    catch (std::exception &e)
    {
        std::cout << e.what() << std::endl;
    }

}

void Database::StoreBlock(const Json::Value &block)
{
    this->StoreTransactions(block);
}

void Database::StoreTransactions(const Json::Value &block)
{
    if (!block["tx"].isArray())
    {
        throw std::runtime_error("Database::StoreTransactions: Invalid input for transactions. Value is not an array.");
    }

    std::unique_ptr<pqxx::connection> conn = this->GetConnection();
    pqxx::work work(*conn.get());

    std::stringstream ss;
    ss << std::this_thread::get_id();
    std::string curr_thread_id = ss.str();

    std::string insert_transactions_prepare =  curr_thread_id + "_insert_transactions";
    std::string insert_transparent_inputs_prepare = curr_thread_id + "_insert_transparent_inputs";
    std::string insert_transparent_outputs_prepare = curr_thread_id + "_insert_transparent_outputs";
    std::string insert_joinsplits_prepare = curr_thread_id + "_insert_joinsplits";

 
        conn->prepare(insert_transactions_prepare,
                      R"(
        INSERT INTO transactions 
        (txid, block_height, sender, amount, fees, hash, prevBlockHash, nextBlockHash, version, merkleRoot, timestamp, difficulty, nonce, height)
        VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14)
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
              )");

        conn->prepare(insert_joinsplits_prepare,
                      R"(
                 INSERT INTO joinsplits 
                 (txid, nullifiers, commitments, vpub_old, vpub_new, anchor, oneTimePubKey, randomSeed, proof)
                 VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9)
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
                if (!BlockValidator::ValidateTransaction(tx)) {}

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
                            } else {
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

                                sender = "0x"; //Temp
                            }

                            work.exec_prepared(insert_transparent_inputs_prepare, txid, vin_tx_id, v_out_idx, sender, coinbase);
                        }
                        catch (const pqxx::sql_error &e)
                        {
                            std::cout << e.what() << std::endl;
                            std::cout << e.sqlstate() << std::endl;
                        }
                        catch (const std::exception &e)
                        {
                            // Handle other exceptions here
                            std::cout << e.what() << std::endl;
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
                            work.exec_prepared(insert_transparent_outputs_prepare, recipients, amount, fees);
                        }
                        catch (const pqxx::sql_error &e)
                        {
                            // Handle SQL errors here
                            std::cout << e.what() << std::endl;
                        }
                        catch (const std::exception &e)
                        {
                            // Handle other exceptions here
                            std::cout << e.what() << std::endl;
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

                            work.exec_prepared(insert_joinsplits_prepare, txid, nullifiers, commitments, vpub_old, vpub_new, anchor, oneTimePubKey, randomSeed, proof);
                        }
                        catch (const pqxx::sql_error &e)
                        {
                            // Handle SQL errors here
                            std::cout << e.what() << std::endl;
                        }
                        catch (const std::exception &e)
                        {
                            // Handle other exceptions here
                            std::cout << e.what() << std::endl;
                        }
                    }
                }

                work.exec_prepared(insert_transactions_prepare, txid, height, sender, amount, fees, hash, prevBlockHash, nextBlockHash, version, merkleRoot, timestamp, difficulty, nonce, height);
            }
            catch (const pqxx::sql_error &e)
            {
                std::cout << e.what() << std::endl;
            }
            catch (const std::exception &e)
            {
                std::cout << e.what() << std::endl;
            }

            // Reset local variables for the next transaction
            isCoinbaseTransaction = false;
        }

        work.commit();
    }

    conn->unprepare(insert_joinsplits_prepare);
    conn->unprepare(insert_transactions_prepare);
    conn->unprepare(insert_transparent_inputs_prepare);
    conn->unprepare(insert_transparent_outputs_prepare);

    this->ReleaseConnection(std::move(conn));
}

unsigned int Database::GetSyncedBlockCountFromDB()
{
    std::unique_ptr<pqxx::connection> conn = this->GetConnection();

    try
    {
        pqxx::work tx(*conn.get());
        std::optional<pqxx::row> row = tx.exec1("SELECT block_number FROM blocks ORDER BY block_Number DESC LIMIT 1;");

        if (row.has_value())
        {
            return row.value()[0].as<int>();
        }
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

    this->ReleaseConnection(std::move(conn));
}