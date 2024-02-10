#include <jsonrpccpp/common/jsonparser.h>
#include <vector>
#include <string>
#include <pqxx/pqxx>
#include <memory>
#include "logger.h"

#ifndef CHAIN_RESOURCE
#define CHAIN_RESOURCE

class Block;
class Database;

struct StoreableBlockData
{
    friend class Block;

    bool isValid{false};

    const char *nonce{""};
    uint8_t version;
    const char *prev_block_hash{""};
    const char *next_block_hash{""};
    const char *merkle_root{""};
    uint64_t timestamp{0};
    uint64_t difficulty{0};
    Json::Value transactions{Json::nullValue};
    const char *hash{""};
    uint64_t height{0};
    uint64_t size{0};
    const char *chainwork{""};
    const char *bits{""};
    uint64_t num_transactions{0};

    double total_transparent_output{0.0};
    std::string transaction_ids_database_representation{""};
    uint64_t total_outputs{0};
    uint64_t total_inputs{0};
    double total_transparent_input{0.0};

    StoreableBlockData();
    StoreableBlockData(const Block &block);
    StoreableBlockData(Json::Value rawBlock);
    void _storeTransparentInputs(const std::string &tx_id, const Json::Value &inputs, std::unique_ptr<pqxx::connection> &conn, const std::string &prepared_statement, pqxx::work &blockTransaction, double &total_transparent_input);
    void _storeTransparentOutputs(const std::string &tx_id, const Json::Value &outputs, std::unique_ptr<pqxx::connection> &conn, const std::string &prepared_statement, pqxx::work &blockTransaction, double &total_public_output);
    void ProcessBlockToStoreable(pqxx::work &blockTransaction, std::unique_ptr<pqxx::connection> &conn);
    
    StoreableBlockData &GetStoreableBlockData();
};

class Block
{
private:
    Json::Value block{Json::nullValue};
    StoreableBlockData storeableData;

public:
    Block();
    Block(const Json::Value &block);
    Block(const Block &rhs) = delete;
    Block &operator=(const Block &rhs) = delete;

    Block(Block &&rhs) = default;
    Block &operator=(Block &&rhs) = default;

    const bool isValid() const;
    StoreableBlockData &GetStoreableBlockData();
    const Json::Value &GetRawJson() const;
};

#endif // CHAIN_RESOURCE