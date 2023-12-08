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

CustomClient::CustomClient(): httpClient(""), rpcClient(httpClient, jsonrpc::JSONRPC_CLIENT_V1) {}

CustomClient::CustomClient(const std::string &url, const std::string &username, const std::string &password)
    : httpClient(url), rpcClient(httpClient, jsonrpc::JSONRPC_CLIENT_V1)
{
    std::cout << "Initializing http client with url: " << url << std::endl;
    std::cout << "Initializing http client with username: " << username << std::endl;
    std::cout << "Initializing http client with password: " << password << std::endl;

    // Encode username and password in base64 for the Authorization header
    std::string authHeader = "Basic " + this->base64Encode(username + ":" + password);
    httpClient.AddHeader("Authorization", authHeader);
    this->setInfo(url, username, password); // Assuming setInfo is a valid method within your class
}

void CustomClient::getInfo() {
    std::cout << this->url << " .... " << this->username << " ..... " << this->password << std::endl;
}

void CustomClient::setInfo(std::string url, std::string username, std::string password) {
    this->url = url;
    this->username = username;
    this->password = password;
}

CustomClient::~CustomClient() {
    std::cout << "Destroying CustomClient instance..." << std::endl;
}

Json::Value CustomClient::CallMethod(const std::string &method, const Json::Value &params)
{
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