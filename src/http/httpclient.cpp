#include "httpclient.h"

#include <iostream>
#include "jsonrpccpp/client.h"
#include "jsonrpccpp/client/connectors/httpclient.h"

#include "../utils/encode.hpp"

CustomClient::CustomClient(const std::string &url, const std::string &username, const std::string &password)
    : httpClient(url), rpcClient(httpClient, jsonrpc::JSONRPC_CLIENT_V1)
{
    const std::string authHeader = "Basic " + base64Encode(username + ":" + password);
   
    spdlog::debug(("Initializing HTTP client with username " + username).c_str());
    spdlog::debug(("Initializing HTTP client with password " + password).c_str());
    spdlog::debug(("Initializing HTTP Authorization header " + authHeader).c_str());

    httpClient.AddHeader("Authorization", authHeader);
}

Json::Value CustomClient::CallMethod(const std::string &method, const Json::Value &params)
{
    spdlog::debug(("RPC: method=" + method).c_str());
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