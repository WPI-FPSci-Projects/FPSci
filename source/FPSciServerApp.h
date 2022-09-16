/**
  \file maxPerf/FPSciNetworkApp.h

 */
#pragma once
#include "FPSciApp.h"
#include "NoWindow.h"
#include "NetworkUtils.h"

class FPSciServerApp : public FPSciApp {

public:
    

protected:

    Array <NetworkUtils::ConnectedClient*> m_connectedClients;          //> List of all connected clients and all atributes needed to comunicate with them
    int   playersReady;                                               ///> Numbers of player(s) that are ready.


public:
    FPSciServerApp(const GApp::Settings& settings);

    void onInit() override;
    void initExperiment() override;
    void onNetwork() override;
    void oneFrame() override;

    NetworkUtils::ConnectedClient* getClientFromAddress(ENetAddress e);
    NetworkUtils::ConnectedClient* getClientFromGUID(GUniqueID ID);
    int frameNumFromID(GUniqueID id) override;
};