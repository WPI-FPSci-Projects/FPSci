/**
  \file maxPerf/FPSciNetworkApp.h

 */
#pragma once
#include "FPSciApp.h"
#include "NoWindow.h"
#include "NetworkUtils.h"

class FPSciServerApp : public FPSciApp {

protected:

    Table <uint32, Array<uint16>> m_clientLatestRTTs;                  //> Table mapping RTT statistics values recorded for each client host address (0: latest RTT, 1: SMA, 2: minimum RTT, 3: maximum RTT)
    Array <NetworkUtils::ConnectedClient*> m_connectedClients;          //> List of all connected clients and all atributes needed to comunicate with them
    int   playersReady;                                               ///> Numbers of player(s) that are ready.


public:
    FPSciServerApp(const GApp::Settings& settings);

    void onInit() override;
    void initExperiment() override;
    void onNetwork() override;
    void oneFrame() override;
    //shared_ptr<NetworkedSession> sess;		 ///< Pointer to the experiment

    NetworkUtils::ConnectedClient* getClientFromAddress(ENetAddress e);
    NetworkUtils::ConnectedClient* getClientFromGUID(GUniqueID ID);
    uint32 frameNumFromID(GUniqueID id) override;

    Array<NetworkUtils::ConnectedClient*> getConnectedClients() { return m_connectedClients; }
    Table <uint32, Array<uint16>> getClientLatestRTTs() { return m_clientLatestRTTs; }
};