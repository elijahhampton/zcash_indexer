#include <jsonrpccpp/common/jsonparser.h> 
#include <vector>

#include "database.h"

#ifndef CHAIN_RESOURCE
#define CHAIN_RESOURCE

class Block;

class Storeable {
    public:
        virtual void Store(const Database& database) const = 0;
        virtual ~Storeable() {}
};

class TransactionGroup: public Storeable {
    private:
        Json::Value transactions;
        
    public:
        TransactionGroup() = delete;
        TransactionGroup(const Json::Value& transactions);
        TransactionGroup(const Block& block);

        TransactionGroup(const TransactionGroup& rhs) = delete;
        TransactionGroup& operator=(const TransactionGroup& rhs) = delete;

        TransactionGroup(TransactionGroup&& rhs) = default;
        TransactionGroup& operator=(TransactionGroup&& rhs) = default;

        void Store(const Database& database) const;
};

class Block: public Storeable {
    private:
        Json::Value block;
        TransactionGroup transactionGroup;

    public:
        Block(const Json::Value& block);

        Block() = delete;
        Block(const Block& rhs) = delete;
        Block& operator=(const Block& rhs) = delete;

        Block(Block&& rhs) = default;
        Block& operator=(Block&& rhs) = default;
        
        const TransactionGroup& GetTransactionGroup() const;
        void Store(const Database& database) const;
};


#endif // CHAIN_RESOURCE