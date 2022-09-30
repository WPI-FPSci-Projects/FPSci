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
    int   playersReady;                                               ///> Numbers of player(s) that are ready.
    Array<shared_ptr<RemotePlayer>> m_remotePlayers;                 ///> List of all remote players, not including self(server)

    InputHandler* m_inputHandler = new InputHandler();


public:
    FPSciServerApp(const GApp::Settings& settings);

    void onInit() override;
    void initExperiment() override;
    void onNetwork() override;
    void oneFrame() override;

    void onASBroadcast();
};