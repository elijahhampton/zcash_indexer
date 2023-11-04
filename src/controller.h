#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "syncer.h"
#include "httpclient.h"

#include <iostream>
#include <string>
#include <vector>

struct InitConfig {
    std::string db_name;
    std::string db_password;
    std::string db_user;
    std::string db_host;
    int db_port = 5432; 
    std::string rpc_username;
    std::string rpc_password;
    std::string rpc_url;
};

class Controller
{
    
private:
    CustomClient rpcClient;
    Database database; 
    Syncer syncer;

public:
    Controller(const InitConfig &config);
    ~Controller();
    void InitAndSetup();
    void Shutdown();
    void StartSyncLoop();
    void StartSync();
};

#endif