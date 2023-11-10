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


// CustomClient::CustomClient(const std::string &url, const std::string &username, const std::string &password)
//     : httpClient(url), rpcClient(httpClient, jsonrpc::JSONRPC_CLIENT_V1)
// {
//     std::cout << "Initializing http client with url: " << url << std::endl;
//     std::cout << "Initializing http client with username: " << username << std::endl;
//     std::cout << "Initializing http client with password: " << password << std::endl;

//    std::string authHeader = "Basic " + base64Encode(username + ":" + password);
//    httpClient.AddHeader("Authorization", authHeader);
//     this->setInfo(url, username, password);
// }

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

// std::string CustomClient::base64Encode(const std::string &input)
// {
//     const std::string base64Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

//     std::string encodedString;
//     int padding = 0;

//     // Calculate the number of padding characters needed based on input length
//     if (input.length() % 3 == 1)
//     {
//         padding = 2;
//     }
//     else if (input.length() % 3 == 2)
//     {
//         padding = 1;
//     }

//     // Encode the input string in chunks of 3 bytes
//     for (size_t i = 0; i < input.length(); i += 3)
//     {
//         uint32_t value = (input[i] << 16) | (i + 1 < input.length() ? input[i + 1] << 8 : 0) | (i + 2 < input.length() ? input[i + 2] : 0);

//         // Split the 24-bit value into four 6-bit values
//         std::vector<uint8_t> indices(4);
//         indices[0] = (value >> 18) & 0x3F;
//         indices[1] = (value >> 12) & 0x3F;
//         indices[2] = (value >> 6) & 0x3F;
//         indices[3] = value & 0x3F;

//         // Append the corresponding base64 characters to the encoded string
//         for (int j = 0; j < 4; j++)
//         {
//             encodedString += base64Chars[indices[j]];
//         }
//     }

//     // Apply padding if necessary
//     for (int i = 0; i < padding; i++)
//     {
//         encodedString[encodedString.length() - 1 - i] = '=';
//     }

//     return encodedString;
// }

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