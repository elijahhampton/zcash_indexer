#include <jsonrpccpp/common/jsonparser.h>
#include <vector>
#include <string>
#include <pqxx/pqxx>
#include <variant>
#include <memory>
#include "spdlog/spdlog.h"

#ifndef CHAIN_RESOURCE
#define CHAIN_RESOURCE

class Block;
class Database;

using BlockData = std::variant<std::string, uint16_t, uint64_t, double>; 

class Storeable
{
public:
    virtual std::map<std::string, std::vector<std::vector<BlockData>>> DataToOrmStorageMap() = 0;
};

class Block : public Storeable
{
private:
    Json::Value block{Json::nullValue};
    std::string nonce{""};
    uint16_t version;
    std::string prev_block_hash{""};
    std::string next_block_hash{""};
    std::string merkle_root{""};
    uint64_t timestamp{0};
    double difficulty{0.0};
    Json::Value transactions{Json::nullValue};
    std::string hash{""};
    uint64_t height{0};
    uint64_t size{0};
    std::string chainwork{""};
    std::string bits{""};
    uint64_t num_transactions{0};

    double total_transparent_output{0.0};
    std::string transaction_ids_database_representation{""};
    uint64_t total_outputs{0};
    uint64_t total_inputs{0};
    double total_transparent_input{0.0};

public:
    Block();
    Block(const Json::Value &block);
    Block(const Block &rhs) = delete;
    Block &operator=(const Block &rhs) = delete;

    Block(Block &&rhs) = default;
    Block &operator=(Block &&rhs) = default;

    virtual ~Block() = default;

    bool isValid();
    Json::Value &GetRawJson();

    std::map<std::string, std::vector<std::vector<BlockData>>> DataToOrmStorageMap() override;

    void _storeTransparentInputs(const std::string &tx_id, const Json::Value &inputs, double &total_transparent_input, std::vector<std::vector<BlockData>> &transparent_transaction_input_values);
    void _storeTransparentOutputs(const std::string &tx_id, const Json::Value &outputs, double &total_public_output, std::vector<std::vector<BlockData>> &transparent_transaction_output_values);
    void ProcessBlockToStoreable(pqxx::work &blockTransaction, std::unique_ptr<pqxx::connection> &conn);
};

#endif // CHAIN_RESOURCE