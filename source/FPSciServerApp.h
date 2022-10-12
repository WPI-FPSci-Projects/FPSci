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

    Array <NetworkUtils::ConnectedClient> m_connectedClients;          ///< List of all connected clients and all atributes needed to comunicate with them
    int   m_clientsReady;                                              ///< Numbers of clients(s) that are ready
    int   m_clientsTimedOut;                                           ///< Numbers of clients(s) that have timed out
    int   m_numberOfRoundsPlayed;                                      ///< Tracks the number of rounds played by the clients
    int   m_clientFirstRoundPeeker;                                    ///< Determines which client will be peeker in the first round
    Array <PlayerConfig> m_peekersRoundConfigs;                        ///< Keeps the round configs for the peekers
    Array <PlayerConfig> m_defendersRoundConfigs;                      ///< Keeps the round configs for the defenders
    Array <std::pair<int, int>> peekerDefenderConfigCombinationsIdx;   ///< Holds index of all possible combinations of matches between peekers and defenders

public:
    FPSciServerApp(const GApp::Settings& settings);

    void onInit() override;
    void initExperiment() override;
    void onNetwork() override;
    void oneFrame() override;
    void preparePerRoundConfigs();
};