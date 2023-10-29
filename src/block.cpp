#include "block.h"
#include "database.h"
#include <fstream>
#include <vector>
#include <sstream>
#include <algorithm>


void Block::Verify() {}
void Block::Parse() {}
void Block::ParseBlockHeader(const Json::Value& header) {}
void Block::ParseTransactions(const Json::Value& transactions) {}

std::string AddressUtils::Base58Encode(uint8_t const *bytes, size_t size)
{
    std::vector<unsigned char> input(size);
    std::copy(bytes, bytes + size, input.begin());

    std::string result;
    std::vector<uint8_t> temp(input.begin(), input.end());

    while (temp.size() > 0 && temp[0] == 0)
    {
        result.push_back(ALPHABET[0]);
        temp.erase(temp.begin());
    }

    std::vector<unsigned int> divmod;
    while (temp.size() > 0)
    {
        divmod.clear();
        unsigned int mod = 0;

        for (unsigned int byte : temp)
        {
            mod = (mod * 256) + byte;
            divmod.push_back(mod / 58);
            mod %= 58;
        }

        result.push_back(ALPHABET[mod]);
        temp.clear();

        for (unsigned int dm : divmod)
        {
            if (temp.size() != 0 || dm != 0)
            {
                temp.push_back(dm);
            }
        }
    }

    std::reverse(result.begin(), result.end());
    return result;
}

bool BlockValidator::ValidateBlock(const Json::Value& block)
{
    return true;
}

bool BlockValidator::ValidateTransaction(const Json::Value& transaction) {
    return true;
}

std::string AddressUtils::IdentifyAsmScriptType(const std::string &scriptSigAsm)
{
    try
    {
        if (scriptSigAsm.empty())
        {
            return "UNKNOWN";
        }

        // Remove any trailing [ALL] indicator
        std::string cleanedScriptSigAsm = scriptSigAsm;
        size_t allIndicatorPos = cleanedScriptSigAsm.find("[ALL]");
        if (allIndicatorPos != std::string::npos)
        {
            cleanedScriptSigAsm.erase(allIndicatorPos, std::string("[ALL]").length());
        }

        // Split the cleaned script assembly into parts
        std::istringstream iss(cleanedScriptSigAsm);
        std::vector<std::string> parts;
        std::string part;
        while (iss >> part)
        {
            parts.push_back(part);
        }

        // Identify based on P2PKH (Generally starts with '30' which is the DER prefix for a signature)
        if (!parts.empty() && parts[0].substr(0, 2) == "30")
        {
            return "P2PKH";
        }

        // Identify based on P2SH (Starts with a '0' and contains multiple parts)
        if (!parts.empty() && parts[0] == "0")
        {
            return "P2SH";
        }

        // Default case when the script type is unknown
        return "UNKNOWN";
    }
    catch (const std::exception &e)
    {
         std::cout << e.what() << std::endl;
        return "UNKNOWN";
    }
}

std::pair<std::string, std::string> AddressUtils::ParseP2PKHScript(const std::string &scriptSigAsm)
{
    // Split the asm into signature and public key.
    std::istringstream iss(scriptSigAsm);
    std::string signature, publicKey;
    iss >> signature >> publicKey;

    return {signature, publicKey};
}

std::string AddressUtils::ExtractAddressFromPubKey(const std::string &publicKeyHex)
{
    uint8_t pubKeyHash[SHA256_DIGEST_LENGTH];
    // Initialize SHA-256 context.
    SHA256_CTX sha256; 
    SHA256_Init(&sha256);

    // Update the SHA-256 hash with the data.
    SHA256_Update(&sha256, &publicKeyHex, sizeof(publicKeyHex) - 1); // -1 to not hash the null terminator.

    // Finalize and get the hash value.
    uint8_t pubKeyHash_2[SHA256_DIGEST_LENGTH];
    SHA256_Final(pubKeyHash, &sha256);

    uint8_t ripemd160Hash[20];
    RIPEMD160_CTX _ripemd160;
    RIPEMD160_Init(&_ripemd160);
    RIPEMD160_Update(&_ripemd160, pubKeyHash_2, SHA256_DIGEST_LENGTH);
    RIPEMD160_Final(ripemd160Hash, &_ripemd160);

    /**
     * MAINNET: 0x1CB8
     * TESTNET: 0x1D25
     */

    uint8_t extendedRIPEMD160Hash[21];
    extendedRIPEMD160Hash[0] = (true) ? 0x1C : 0x1D;
    extendedRIPEMD160Hash[1] = (true) ? 0xB8 : 0x25;
    memcpy(extendedRIPEMD160Hash + 2, ripemd160Hash, 21);

    uint8_t checksum[SHA256_CBLOCK];
    SHA256_CTX sha256_2;
    SHA256_Init(&sha256_2);
    SHA256_Update(&sha256_2, extendedRIPEMD160Hash, 21);
    SHA256_Final(checksum, &sha256_2);

    uint8_t addressBytes[25];
    memcpy(addressBytes, extendedRIPEMD160Hash, 21);
    memcpy(addressBytes + 21, checksum, 4);

    return AddressUtils::Base58Encode(addressBytes, 25);
}