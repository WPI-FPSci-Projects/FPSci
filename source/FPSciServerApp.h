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

    Array <NetworkUtils::ConnectedClient> m_connectedClients;          //> List of all connected clients and all atributes needed to comunicate with them
    Table <uint32, uint16> m_clientLatestRTTs;                         //> Table mapping the latest RTT value recorded for each client host address
    int   playersReady;                                               ///> Numbers of player(s) that are ready.



public:
    FPSciServerApp(const GApp::Settings& settings);

    void onInit() override;
    void initExperiment() override;
    void onNetwork() override;
    void oneFrame() override;

};