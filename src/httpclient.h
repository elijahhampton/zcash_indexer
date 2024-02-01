#ifndef HTTPCLIENT_H
#define HTTPCLIENT_H

#include "jsonrpccpp/client.h"
#include "jsonrpccpp/client/connectors/httpclient.h"
#include "logger.h"

class CustomClient
{
private:
    jsonrpc::HttpClient httpClient;
    jsonrpc::Client rpcClient;

    std::string url;
    std::string username;
    std::string password;

public:
    CustomClient(const CustomClient& rhs) noexcept = delete;
    CustomClient& operator=(const CustomClient& rhs) noexcept = delete;

    CustomClient(CustomClient&& rhs) noexcept = default;
    CustomClient& operator=(CustomClient&& rhs) noexcept = default;

    CustomClient(const std::string &url, const std::string &username, const std::string &password);
    ~CustomClient() noexcept = default;

    Json::Value CallMethod(const std::string &method, const Json::Value &params);
    Json::Value getinfo();
    Json::Value getblockchaininfo();
    Json::Value getblockcount();
    Json::Value getblockheader(const Json::Value &param01, const Json::Value &param02);
    Json::Value getblock(const Json::Value &param01, const Json::Value &param02);
    std::string base64Encode(const std::string &input);
    Json::Value getpeerinfo();
};

#endif