#include "httpclient.h"

#include <iostream>
#include "jsonrpccpp/client.h"
#include "jsonrpccpp/client/connectors/httpclient.h"
#include "json/json.h"

CustomClient::CustomClient(const std::string &url, const std::string &username, const std::string &password)
    : httpClient(url), rpcClient(httpClient, jsonrpc::JSONRPC_CLIENT_V1)
{
    std::cout << "Initializing http client with url: " << url << std::endl
    std::cout << "Initializing http client with username: " << username << std::endl;
    std::cout << "Initializing http client with password: " << password << std::endl;

    std::string authHeader = "Basic " + base64Encode(username + ":" + password);
    httpClient.AddHeader("Authorization", authHeader);
}

CustomClient::~CustomClient() {
    std::cout << "Destroying CustomClient instance..." << std::endl;
}

Json::Value CustomClient::CallMethod(const std::string &method, const Json::Value &params)
{
    return rpcClient.CallMethod(method, params);
}

std::string CustomClient::base64Encode(const std::string &input)
{
    const std::string base64Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string encodedString;
    int padding = 0;

    // Calculate the number of padding characters needed based on input length
    if (input.length() % 3 == 1)
    {
        padding = 2;
    }
    else if (input.length() % 3 == 2)
    {
        padding = 1;
    }

    // Encode the input string in chunks of 3 bytes
    for (size_t i = 0; i < input.length(); i += 3)
    {
        uint32_t value = (input[i] << 16) | (i + 1 < input.length() ? input[i + 1] << 8 : 0) | (i + 2 < input.length() ? input[i + 2] : 0);

        // Split the 24-bit value into four 6-bit values
        std::vector<uint8_t> indices(4);
        indices[0] = (value >> 18) & 0x3F;
        indices[1] = (value >> 12) & 0x3F;
        indices[2] = (value >> 6) & 0x3F;
        indices[3] = value & 0x3F;

        // Append the corresponding base64 characters to the encoded string
        for (int j = 0; j < 4; j++)
        {
            encodedString += base64Chars[indices[j]];
        }
    }

    // Apply padding if necessary
    for (int i = 0; i < padding; i++)
    {
        encodedString[encodedString.length() - 1 - i] = '=';
    }

    return encodedString;
}

Json::Value CustomClient::getinfo()
{
    Json::Value p;
    p = Json::nullValue;
    return rpcClient.CallMethod("getinfo", p);
}

Json::Value CustomClient::getblockchaininfo()
{
    Json::Value p;
    p = Json::nullValue;
    return rpcClient.CallMethod("getblockchaininfo", p);
}

Json::Value CustomClient::getblockcount()
{
    Json::Value params;
    params = Json::nullValue;
    return rpcClient.CallMethod("getblockcount", params);
}

Json::Value CustomClient::getblockheader(const Json::Value &param01, const Json::Value &param02)
{
    Json::Value p;
    p.append(param01);
    p.append(param02);
    return rpcClient.CallMethod("getblockheader", p);
}

Json::Value CustomClient::getblock(const Json::Value &param01, const Json::Value &param02)
{
    Json::Value p;
    p.append(param01);
    p.append(param02);
    return rpcClient.CallMethod("getblock", p);
}