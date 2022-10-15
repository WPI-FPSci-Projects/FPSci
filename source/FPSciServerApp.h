/**
  \file maxPerf/FPSciNetworkApp.h

 */
#pragma once
#include "FPSciApp.h"
#include "NoWindow.h"

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
    //shared_ptr<NetworkedSession> sess;		 ///< Pointer to the experiment

    NetworkUtils::ConnectedClient* getClientFromAddress(ENetAddress e);
    NetworkUtils::ConnectedClient* getClientFromGUID(GUniqueID ID);
    uint32 frameNumFromID(GUniqueID id) override;

    Array<NetworkUtils::ConnectedClient*> getConnectedClients() { return m_connectedClients; }
};