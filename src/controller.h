#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "syncer.h"
#include "httpclient.h"

#include <iostream>
#include <string>
#include <vector>

class Controller
{
    
private:
    CustomClient rpcClient;
    Database database; 
    Syncer syncer;

public:
    Controller();
    ~Controller();
    void InitAndSetup();
    void Shutdown();
    void StartSyncLoop();
    void StartSync();
};

#endif