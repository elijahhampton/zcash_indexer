#include <jsonrpccpp/common/jsonparser.h>
#include <vector>
#include <string>
#include <pqxx/pqxx>
#include <variant>
#include <utility>
#include <memory>
#include "spdlog/spdlog.h"
#include "identifier.h"
#include "database/database.h"

#ifndef CHAIN_RESOURCE
#define CHAIN_RESOURCE

//class Block;
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
    uint16_t version;
    uint64_t timestamp{0};
    uint64_t num_transactions{0};
    uint64_t total_outputs{0};
    uint64_t total_inputs{0};
    uint64_t height{0};
    uint64_t size{0};
    double difficulty{0.0};
    double total_transparent_output{0.0};
    double total_transparent_input{0.0};
    std::string chainwork{""};
    std::string bits{""};
    std::string nonce{""};
    std::string prev_block_hash{""};
    std::string next_block_hash{""};
    std::string merkle_root{""};
    std::string hash{""};
    std::string transaction_ids_database_representation{""};
    Json::Value block{Json::nullValue};
    Json::Value transactions{Json::nullValue};

    void TransparentInputs(const std::string &tx_id, const Json::Value &inputs, double &total_transparent_input, std::vector<std::vector<BlockData>> &transparent_transaction_input_values);
    void TransparentOutputs(const std::string &tx_id, const Json::Value &outputs, double &total_public_output, std::vector<std::vector<BlockData>> &transparent_transaction_output_values);

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
};

#endif // CHAIN_RESOURCE