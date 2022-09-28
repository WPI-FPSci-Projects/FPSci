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
    Table <uint32, Array<uint16>> m_clientLatestRTTs;                  //> Table mapping RTT statistics values recorded for each client host address (0: latest RTT, 1: SMA, 2: minimum RTT, 3: maximum RTT)
    int   playersReady;                                               ///> Numbers of player(s) that are ready.



public:
    FPSciServerApp(const GApp::Settings& settings);

    void onInit() override;
    void initExperiment() override;
    void onNetwork() override;
    void oneFrame() override;

};