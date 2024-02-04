#include <jsonrpccpp/common/jsonparser.h> 
#include <vector>
#include <string>
#include <pqxx/pqxx>
#include <memory>

#ifndef CHAIN_RESOURCE
#define CHAIN_RESOURCE

class Block;
class Database;

struct StoreableBlockData
{
    bool isValid{false};
    
    const char * nonce{""};
    uint8_t version;
    const char * prev_block_hash{""};
    const char * next_block_hash{""};
    const char * merkle_root{""};
    uint64_t timestamp{0};
    uint64_t difficulty{0};
    Json::Value transactions{Json::nullValue};
    const char * hash{""};
    uint64_t height{0};
    uint64_t size{0};
    const char * chainwork{""};
    const char * bits{""};
    uint64_t num_transactions{0};

    uint64_t total_transparent_output;
    std::string transaction_ids_database_representation{""};
    uint64_t total_outputs;
    uint64_t total_inputs;
    uint64_t total_transparent_input;

    StoreableBlockData();
    StoreableBlockData(const Block& block);
    StoreableBlockData(Json::Value rawBlock);
    void ProcessBlockToStoreable(const Block& rawBlock);
    const StoreableBlockData& GetStoreableBlockData() const;
};

class TransactionGroup {
    private:
        Json::Value transactions;
        
    public:
        TransactionGroup();
        TransactionGroup(const Json::Value& transactions);
        TransactionGroup(const Block& block);

        TransactionGroup(const TransactionGroup& rhs) = delete;
        TransactionGroup& operator=(const TransactionGroup& rhs) = delete;

        TransactionGroup(TransactionGroup&& rhs) = default;
        TransactionGroup& operator=(TransactionGroup&& rhs) = default;

        const Json::Value& GetRawTransactions() const;
};


class Block {
    private:
        Json::Value block{Json::nullValue};
        StoreableBlockData storeableData;
        TransactionGroup transactionGroup;

    public:
        Block();
        Block(const Json::Value& block);
        Block(const Block& rhs) = delete;
        Block& operator=(const Block& rhs) = delete;

        Block(Block&& rhs) = default;
        Block& operator=(Block&& rhs) = default;
        
        const bool isValid() const;
        const TransactionGroup& GetTransactionGroup() const;
        const StoreableBlockData& GetStoreableBlockData() const;
        const Json::Value& GetRawJson() const;
};



#endif // CHAIN_RESOURCE