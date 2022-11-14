/**
  \file maxPerf/FPSciNetworkApp.h

 */
#pragma once
#include "FPSciApp.h"
#include "NoWindow.h"

class FPSciServerApp : public FPSciApp {

protected:

    int   m_clientsReady;                                              ///< Numbers of clients(s) that are ready
    int   m_clientsTimedOut;                                           ///< Numbers of clients(s) that have timed out
    int   m_clientFirstRoundPeeker;                                    ///< Determines which client will be peeker in the first round
    int   m_clientFeedbackSubmitted;                                   ///< Numbers of clients(s) that have submitted feedback
    Array <PlayerConfig> m_peekersRoundConfigs;                        ///< Keeps the round configs for the peekers
    Array <PlayerConfig> m_defendersRoundConfigs;                      ///< Keeps the round configs for the defenders
    Array <std::pair<int, int>> peekerDefenderConfigCombinationsIdx;   ///< Holds index of all possible combinations of matches between peekers and defenders
    Table <uint32, Array<uint16>> m_clientRTTStatistics;                //> Table mapping RTT statistics values recorded for each client host address (0: latest RTT, 1: SMA, 2: minimum RTT, 3: maximum RTT)
    Array <NetworkUtils::ConnectedClient*> m_connectedClients;          //> List of all connected clients and all atributes needed to comunicate with them

    uint8 m_historicalPlayerCount = 0;
    ///> Number of players that have ever connected to the server, used for assigning playerIDs

    ServerDataHandler* m_dataHandler = new ServerDataHandler();

public:
    FPSciServerApp(const GApp::Settings& settings);

    void onInit() override;
    void initExperiment() override;
    void onNetwork() override;
    void oneFrame() override;
    void preparePerRoundConfigs();
    //shared_ptr<NetworkedSession> sess;		 ///< Pointer to the experiment

    NetworkUtils::ConnectedClient* getClientFromAddress(ENetAddress e);
    NetworkUtils::ConnectedClient* getClientFromGUID(GUniqueID ID);
    uint32 frameNumFromID(GUniqueID id) override;

    Array<NetworkUtils::ConnectedClient*> getConnectedClients() { return m_connectedClients; }
    void updateSession(const String& id, bool forceReload) override;
    Table <uint32, Array<uint16>> getClientRTTStatistics() { return m_clientRTTStatistics; }

    void onASBroadcast();

    // Utility functions
    void snapBackPlayer(uint8 playerID);                               ///< Snaps back a player to the position they were at last frame
    uint8 GUIDtoPlayerID(GUniqueID guid);
    GUniqueID playerIDtoGUID(uint8 playerID);
};