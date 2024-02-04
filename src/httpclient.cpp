#include "httpclient.h"

#include <iostream>
#include "jsonrpccpp/client.h"
#include "jsonrpccpp/client/connectors/httpclient.h"

std::string CustomClient::base64Encode(const std::string &data) {
    static const std::string base64_chars = 
                 "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                 "abcdefghijklmnopqrstuvwxyz"
                 "0123456789+/";
    
    std::string encoded;
    unsigned char const* bytes_to_encode = reinterpret_cast<const unsigned char*>(data.data());
    int in_len = data.size();
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (in_len--) {
        char_array_3[i++] = *(bytes_to_encode++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for(i = 0; (i <4) ; i++)
                encoded += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for(j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

        for (j = 0; (j < i + 1); j++)
            encoded += base64_chars[char_array_4[j]];

        while((i++ < 3))
            encoded += '=';
    }

    return encoded;
}

CustomClient::CustomClient(const std::string &url, const std::string &username, const std::string &password)
    : httpClient(url), rpcClient(httpClient, jsonrpc::JSONRPC_CLIENT_V1)
{
    std::string authHeader = "Basic " + this->base64Encode(username + ":" + password);
   
    __DEBUG__(("Initializing HTTP client with username " + username).c_str());
    __DEBUG__(("Initializing HTTP client with password " + password).c_str());
    __DEBUG__(("Initializing HTTP Authorization header " + authHeader).c_str());

    httpClient.AddHeader("Authorization", authHeader);
}

Json::Value CustomClient::CallMethod(const std::string &method, const Json::Value &params)
{
    __DEBUG__(("RPC: method=" + method).c_str());
    return rpcClient.CallMethod(method, params);
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

Json::Value CustomClient::getpeerinfo()
{
    Json::Value p{Json::nullValue};
    return rpcClient.CallMethod("getpeerinfo", p);
}