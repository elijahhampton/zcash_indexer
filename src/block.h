#ifndef BLOCK_H
#define BLOCK_H

#include <map>
#include <mutex>
#include <memory>
#include "json/json.h"

#include <sstream>
#include <algorithm>
#include <iostream>
#include <string>
#include <sstream>

#include <openssl/sha.h>
#include <openssl/ripemd.h>

#include <boost/filesystem.hpp>
#include <boost/asio.hpp>

class Database;

enum P2PKH_T_ADDRESS_PREFIX
{
    P2PKH_MAINNET = 0x1CB8,
    P2PKH_TESTNET = 0x1D25
};

enum NETWORK
{
    MAINNET = 0,
    TESTNET = 1
};

// Static variables for upgrade block heights
constexpr int OVERWINTER_BLOCK_HEIGHT = 347500;
constexpr int SAPLING_BLOCK_HEIGHT = 419200;
constexpr int BLOSSOM_BLOCK_HEIGHT = 653600;
constexpr int HEARTWOOD_BLOCK_HEIGHT = 903000;
constexpr int CANOPY_BLOCK_HEIGHT = 1046400;
constexpr int NU5_BLOCK_HEIGHT = 1687104;
constexpr const char* _delimiter = " ";

const char *const ALPHABET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

class Block {
    public:
    private:
        void Parse() {}
        void ParseBlockHeader() {}
        void ParseTransactions() {}
}

class AddressUtils {
    public:
        static std::string IdentifyAsmScriptType(const std::string& script);
        static std::string ExtractAddressFromPubKey(const std::string& publicKeyHex);
        static std::string Base58Encode(uint8_t const *bytes, size_t size);
        static std::pair<std::string, std::string> ParseP2PKHScript(const std::string &scriptSigAsm);
};

#endif BLOCK_H