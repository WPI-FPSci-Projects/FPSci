/** \file FPSciServerApp.cpp */
#include "FPSciServerApp.h"
#include "PhysicsScene.h"
#include "WaypointManager.h"
#include "NetworkedSession.h"
#include <Windows.h>
#include <string>

FPSciServerApp::FPSciServerApp(const GApp::Settings& settings) : FPSciApp(settings) {}


void FPSciServerApp::initExperiment() {
    m_clientsReady = 0;
    m_clientsTimedOut = 0;
    // Load config from files
    loadConfigs(startupConfig.experimentList[experimentIdx]);
    m_lastSavedUser = *currentUser();			// Copy over the startup user for saves

    // Setup the display mode
    setSubmitToDisplayMode(
        //SubmitToDisplayMode::EXPLICIT);
    SubmitToDisplayMode::MINIMIZE_LATENCY);
    //SubmitToDisplayMode::BALANCE);
    //SubmitToDisplayMode::MAXIMIZE_THROUGHPUT);

    // Set the initial simulation timestep to REAL_TIME. The desired timestep is set later.
    setFrameDuration(frameDuration(), REAL_TIME);
    m_lastOnSimulationRealTime = 0.0;

    // Setup/update waypoint manager
    if (startupConfig.developerMode && startupConfig.waypointEditorMode) {
        FPSciApp::waypointManager = WaypointManager::create(this);
    }

    // Setup the scene
    setScene(PhysicsScene::create(m_ambientOcclusion));
    scene()->registerEntitySubclass("PlayerEntity", &PlayerEntity::create);			// Register the player entity for creation
    scene()->registerEntitySubclass("FlyingEntity", &FlyingEntity::create);			// Register the target entity for creation

    weapon = Weapon::create(&experimentConfig.weapon, scene(), activeCamera());
    weapon->setHitCallback(std::bind(&FPSciServerApp::hitTarget, this, std::placeholders::_1));
    weapon->setMissCallback(std::bind(&FPSciServerApp::missEvent, this));

    // Load models and set the reticle
    loadModels();
    setReticle(reticleConfig.index);

    // Load fonts and images
    outputFont = GFont::fromFile(System::findDataFile("arial.fnt"));
    hudTextures.set("scoreBannerBackdrop", Texture::fromFile(System::findDataFile("gui/scoreBannerBackdrop.png")));

    // Setup the GUI
    showRenderingStats = false;
    makeGUI();

    updateMouseSensitivity();				// Update (apply) mouse sensitivity
    const Array<String> sessions = m_userSettingsWindow->updateSessionDropDown();	// Update the session drop down to remove already completed sessions
    updateSession(sessions[0], true);		// Update session to create results file/start collection

    /* This is where added code begins */
    
    //Setup dataHandler
    m_dataHandler = new ServerDataHandler();
    //m_dataHandler->SetParameters(experimentConfig.pastFrame);
    
    // Setup the network and start listening for clients
    ENetAddress localAddress;

    localAddress.host = ENET_HOST_ANY;
    localAddress.port = experimentConfig.serverPort;
    m_localHost = enet_host_create(&localAddress, 32, 2, 0, 0);            // create the reliable connection
    if (m_localHost == nullptr) {
        throw std::runtime_error("Could not create a local host for the clients to connect to");
    }

    m_unreliableSocket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);         // create the unreliable socket
    enet_socket_set_option(m_unreliableSocket, ENET_SOCKOPT_NONBLOCK, 1);       // Set socket to non-blocking
    localAddress.port += 1;                                                 // We use the reliable connection port + 1 for the unreliable connections (instead of another value in the experiment config)
    if (enet_socket_bind(m_unreliableSocket, &localAddress)) {
        debugPrintf("bind failed with error: %d\n", WSAGetLastError());
        throw std::runtime_error("Could not bind to the local address");
    }

    debugPrintf("Began listening\n");

    // If we are pinging
    if (startupConfig.pingEnabled) {

        // Add separate socket for ping
        m_pingSocket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
        enet_socket_set_option(m_pingSocket, ENET_SOCKOPT_NONBLOCK, 1);
        // Check if the ping port is not already used
        if (experimentConfig.pingPort != experimentConfig.serverPort &&
            experimentConfig.pingPort != experimentConfig.serverPort + 1) {
            localAddress.port = experimentConfig.pingPort;
        }
        else {
            experimentConfig.pingPort == experimentConfig.serverPort + 1 ?
                localAddress.port = experimentConfig.pingPort + 1 :
                localAddress.port = experimentConfig.pingPort + 2;
            logPrintf("Ping: port %d is already in use, opening up port %d for pinging\n", experimentConfig.pingPort, localAddress.port);
        }

        if (enet_socket_bind(m_pingSocket, &localAddress)) {
            debugPrintf("bind failed with error: %d\n", WSAGetLastError());
            throw std::runtime_error("Could not bind ping to the local address");
        }

        // Initialize lambdas and threads for listening for pings
        auto s2cPing = [](ENetSocket socket) {

            while (true) {
                shared_ptr<GenericPacket> inPacket = NetworkUtils::receivePing(&socket);               
                while (inPacket != nullptr) {                   
                    ENetAddress srcAddr = inPacket->srcAddr();
                    char ip[16];
                    enet_address_get_host_ip(&srcAddr, ip, 16);
                    if (inPacket->type() == PacketType::PING) {                       
                        PingPacket* c2sPacket = static_cast<PingPacket*>(inPacket.get());
                        shared_ptr<PingPacket> outPacket = GenericPacket::createUnreliable<PingPacket>(&socket, &srcAddr);
                        outPacket->populate(c2sPacket->m_rttStart);
                        NetworkUtils::send(outPacket);
                    }
                    inPacket = NetworkUtils::receivePing(&socket);
                }

            }
        };
        shared_ptr<PlayerEntity> player = scene()->typedEntity<PlayerEntity>("player");
        player->setPlayerMovement(true);     

        std::thread s2cPing_Th(s2cPing, m_pingSocket);
        s2cPing_Th.detach();      

        // Initialize dummy ping statistics
        m_pingStats.pingQueue.pushBack(0);
        experimentConfig.pingSMASize > 0 ? m_pingStats.smaRTTSize = experimentConfig.pingSMASize : m_pingStats.smaRTTSize = 5;
    }

    isServer = true;
    m_clientFirstRoundPeeker = true;
    shared_ptr<PlayerEntity> player = scene()->typedEntity<PlayerEntity>("player");
    player->setPlayerMovement(true);

    sessConfig->numberOfRoundsPlayed = 0;
    sessConfig->isNetworked = &experimentConfig.isNetworked;
}

void FPSciServerApp::onNetwork()
{
    /* None of this is from the upsteam project */

    //if (!static_cast<NetworkedSession*>(sess.get())->currentState == PresentationState::networkedSessionRoundStart) {
    m_networkFrameNum++;
    if (sess->currentState == networkedSessionRoundStart) {
        m_dataHandler->NewCurrentFrame(m_networkFrameNum);
    }
    //}
    
    /* First we receive on the unreliable connection */

    shared_ptr<GenericPacket> inPacket = NetworkUtils::receivePacket(m_localHost, &m_unreliableSocket);
    while (inPacket != nullptr) {
        char ip[16];        
        ENetAddress srcAddr = inPacket->srcAddr();
        enet_address_get_host_ip(&srcAddr, ip, 16);       
        NetworkUtils::ConnectedClient* client = getClientFromAddress(inPacket->srcAddr());
        if (!inPacket->isReliable()) {
            switch (inPacket->type()) {
            case HANDSHAKE: {
                    shared_ptr<HandshakeReplyPacket> outPacket = GenericPacket::createUnreliable<HandshakeReplyPacket>(&m_unreliableSocket, &srcAddr);
                    NetworkUtils::send(outPacket);
                    /*if (outPacket->send() <= 0) {
                        debugPrintf("Failed to send the handshke reply\n");
                    }*/
                    break;
            }
            case BATCH_ENTITY_UPDATE: {
                    // Client currently only update self position, needs check for AS if changed
                    BatchEntityUpdatePacket* typedPacket = static_cast<BatchEntityUpdatePacket*> (inPacket.get());
                    for (BatchEntityUpdatePacket::EntityUpdate e : typedPacket->m_updates) {
                        shared_ptr<NetworkedEntity> entity = (*scene()).typedEntity<NetworkedEntity>(e.name);
                        if (entity == nullptr) {
                            debugPrintf("Recieved update for entity %s, but it doesn't exist\n", e.name.c_str());
                        }
                        else {
                            switch (typedPacket->m_updateType) {
                            case BatchEntityUpdatePacket::NetworkUpdateType::NOOP:
                                // Do nothing (No-Op)
                                break;
                            case BatchEntityUpdatePacket::NetworkUpdateType::REPLACE_FRAME:
                                entity->setFrame(e.frame);
                                if (m_dataHandler != nullptr) {
                                    m_dataHandler->UpdateCframe(e.name, e.frame, e.frameNumber, true);
                                }
                                break;
                            }
                        }
                    }
                    break;
            }
            case PLAYER_INTERACT: {
                    PlayerInteractPacket* typedPacket = static_cast<PlayerInteractPacket*> (inPacket.get());
                    shared_ptr<NetworkedEntity> clientEntity = scene()->typedEntity<NetworkedEntity>(typedPacket->m_actorID.toString16());
                    RemotePlayerAction rpa = RemotePlayerAction();
                    rpa.time = sess->logger->getFileTime();
                    rpa.viewDirection = clientEntity->getLookAzEl();
                    rpa.position = clientEntity->frame().translation;
                    rpa.state = sess->currentState;
                    rpa.action = (PlayerActionType)typedPacket->m_remoteAction;
                    rpa.actorID = typedPacket->m_actorID.toString16();
                    sess->logger->logRemotePlayerAction(rpa);
                    break;
            }
            case PING_DATA: {
                    PingDataPacket* pingDataPacket = static_cast<PingDataPacket*>(inPacket.get());

                    uint16 cappedRTT = pingDataPacket->m_latestRTT;
                    uint16 cappedSMARTT = pingDataPacket->m_smaRTT;
                    uint16 cappedMinRTT = pingDataPacket->m_minRTT;
                    uint16 cappedMaxRTT = pingDataPacket->m_maxRTT;
                    Array<uint16> rttStatsArray = { cappedRTT, cappedSMARTT, cappedMinRTT, cappedMaxRTT };
                    m_clientRTTStatistics.set(inPacket->srcAddr().host, rttStatsArray);
                    break;
            }
            default:
                debugPrintf("WARNING: unhandled packet receved on the unreliable channel of type: %d\n", inPacket->type());
                break;
            }
        }
        // This is a reliable packet and should be handled as such
        else {
            switch (inPacket->type()) {
            case RELIABLE_CONNECT: {
                    debugPrintf("connection recieved...\n");
                    logPrintf("made connection to %s in response to input\n", ip);
                    break;
            }
            case RELIABLE_DISCONNECT: {
                    debugPrintf("disconnection recieved...\n");
                    logPrintf("%s disconnected.\n", ip);
                    /* Removes the client from the list of connected clients and orders all other clients to delete that entity */
                    shared_ptr<NetworkedEntity> entity = scene()->typedEntity<NetworkedEntity>(client->guid.toString16());
                    if (entity != nullptr) {
                        scene()->remove(entity);
                    }
                    for (int i = 0; i < m_connectedClients.length(); i++) {
                        if (m_connectedClients[i]->guid == client->guid) {
                            m_connectedClients.remove(i, 1);
                        }
                    }

                    shared_ptr<DestroyEntityPacket> outPacket = GenericPacket::createForBroadcast<DestroyEntityPacket>();
                    outPacket->populate(m_networkFrameNum, client->guid);
                    NetworkUtils::broadcastReliable(outPacket, m_localHost);
                    //NetworkUtils::send(outPacket);
                    //outPacket->send();

                    //remove from datahandler
                    m_dataHandler->DeleteClient(client->guid.toString16());
                    break;
            }
            case REGISTER_CLIENT: {
                    RegisterClientPacket* typedPacket = static_cast<RegisterClientPacket*> (inPacket.get());
                    debugPrintf("Registering client...\n");
                    NetworkUtils::ConnectedClient* newClient = new NetworkUtils::ConnectedClient();
                    newClient->peer = typedPacket->m_peer;
                    newClient->guid = typedPacket->m_guid;
                    newClient->playerID = m_historicalPlayerCount;
                    ENetAddress tempAddr; // stole from register client function
                    tempAddr.host = typedPacket->m_peer->address.host;
                    tempAddr.port = typedPacket->m_portNum;
                    newClient->unreliableAddress = tempAddr;
                    debugPrintf("\tPort: %i\n", tempAddr.port);
                    debugPrintf("\tHost: %i\n", tempAddr.host);
                    
                    m_connectedClients.append(newClient);
                    /* Reply to the registration */
                    shared_ptr<RegistrationReplyPacket> registrationReply = GenericPacket::createReliable<RegistrationReplyPacket>(newClient->peer);
                    registrationReply->populate(newClient->guid, 0, newClient->playerID);
                    NetworkUtils::send(registrationReply);      
                    m_historicalPlayerCount++;
                    ENetAddress addr = typedPacket->srcAddr();
                    addr.port = typedPacket->m_portNum;
                    /* Set the amount of latency to add */
                    NetworkUtils::setAddressLatency(addr, sessConfig->networkLatency);                
                    NetworkUtils::setAddressLatency(typedPacket->srcAddr(), sessConfig->networkLatency);
                    //registrationReply->send();
                    debugPrintf("\tRegistered client: %s\n", newClient->guid.toString16());
                    debugPrintf("\tPlayer ID: %i\n", newClient->playerID);

                    Any modelSpec = PARSE_ANY(ArticulatedModel::Specification{			///< Basic model spec for target
                        filename = "model/target/mid_poly_sphere_no_outline.obj";
                        cleanGeometrySettings = ArticulatedModel::CleanGeometrySettings{
                        allowVertexMerging = true;
                        forceComputeNormals = false;
                        forceComputeTangents = false;
                        forceVertexMerging = true;
                        maxEdgeLength = inf;
                        maxNormalWeldAngleDegrees = 0;
                        maxSmoothAngleDegrees = 0;
                        };
                        });
                    shared_ptr<Model> model = ArticulatedModel::create(modelSpec);
                    /* Create a new entity for the client */
                    const shared_ptr<NetworkedEntity>& target = NetworkedEntity::create(newClient->guid.toString16(), &(*scene()), model, CFrame());
                    // add entity to ConnectedClient
                    m_connectedClients.last()->entity = target;

                    target->setWorldSpace(true);
                    target->setColor(G3D::Color3(20.0, 20.0, 200.0));
                    target->setPlayerID(newClient->playerID);

                    /* Add the new target to the scene */
                    (*scene()).insert(target);

                    //add camera and weapon
                    const String* name = new String("camera" + std::to_string(m_connectedClients.size()));
                    m_connectedClients.last()->camera = Camera::create(*name);
                    (*scene()).insert(m_connectedClients.last()->camera);
                    m_connectedClients.last()->weapon = Weapon::create(&experimentConfig.weapon, scene(), m_connectedClients.last()->camera);
                    m_connectedClients.last()->weapon->setHitCallback(std::bind(&FPSciServerApp::hitTarget, this, std::placeholders::_1));
                    m_connectedClients.last()->weapon->setMissCallback(std::bind(&FPSciServerApp::missEvent, this));
                    m_connectedClients.last()->weapon->setScene(scene());
                    m_connectedClients.last()->weapon->setCamera(m_connectedClients.last()->camera);

                    

                    /* ADD NEW CLIENT TO OTHER CLIENTS, ADD OTHER CLIENTS TO NEW CLIENT */
                    shared_ptr<CreateEntityPacket> createEntityPacket = GenericPacket::createForBroadcast<CreateEntityPacket>();
                    createEntityPacket->populate(m_networkFrameNum, newClient->guid, newClient->playerID);
                    NetworkUtils::broadcastReliable(createEntityPacket, m_localHost);
                    debugPrintf("Sent a broadcast packet to all connected peers\n");

                    for (int i = 0; i < m_connectedClients.length(); i++) {
                        // Create entitys on the new client for all other clients
                        if (newClient->guid != m_connectedClients[i]->guid) {
                            createEntityPacket = GenericPacket::createReliable<CreateEntityPacket>(newClient->peer);
                            createEntityPacket->populate(m_networkFrameNum, m_connectedClients[i]->guid, m_connectedClients[i]->playerID);
                            NetworkUtils::send(createEntityPacket);
                            //createEntityPacket->send();
                            debugPrintf("Sent add to %s to add %s\n", newClient->guid.toString16(), m_connectedClients[i]->guid.toString16());
                        }
                    }
                    // move the client to a different location
                    // TODO: Make this smart not just some test code
                    if (m_connectedClients.length() % 2 == 0) {
                        Point3 position = Point3(-46, -2.3, 0);
                        float heading = 90;
                        shared_ptr<SetSpawnPacket> setSpawnPacket = GenericPacket::createReliable<SetSpawnPacket>(newClient->peer);
                        setSpawnPacket->populate(position, heading);
                        NetworkUtils::send(setSpawnPacket);
                        //setSpawnPacket->send();
                        shared_ptr<RespawnClientPacket> respawnPacket = GenericPacket::createReliable<RespawnClientPacket>(newClient->peer);
                        respawnPacket->populate();
                        NetworkUtils::send(respawnPacket);
                        //respawnPacket->send();
                        // Set the target translation
                        target->setFrame(CFrame(position));
                    }

                    //Add to Datahandler
                    m_dataHandler->AddNewClient(newClient->guid.toString16());


                    break;
            }
            case PacketType::REPORT_HIT: {
                // Any changes to this might also need to be reflected in Server side Sim (FPSciServerApp::simulateWeapons())
                    // This just causes everyone to respawn
                    ReportHitPacket* typedPacket = static_cast<ReportHitPacket*> (inPacket.get());
                    shared_ptr<NetworkedEntity> hitEntity = scene()->typedEntity<NetworkedEntity>(typedPacket->m_shotID.toString16());
                    //NetworkUtils::ConnectedClient* hitClient = getClientFromGUID(hitID);

                    // Log the hit on the server
                    const shared_ptr<NetworkedEntity> shooterEntity = scene()->typedEntity<NetworkedEntity>(typedPacket->m_shooterID.toString16());
                    RemotePlayerAction rpa = RemotePlayerAction();
                    rpa.time = sess->logger->getFileTime();
                    rpa.viewDirection = shooterEntity->getLookAzEl();
                    rpa.position = shooterEntity->frame().translation;
                    rpa.state = sess->currentState;
                    rpa.action = PlayerActionType::Hit;
                    rpa.actorID = typedPacket->m_shooterID.toString16();
                    rpa.affectedID = typedPacket->m_shooterID.toString16();
                    sess->logger->logRemotePlayerAction(rpa);

                    float damage = 1.001 / sessConfig->hitsToKill;

                    if (hitEntity->doDamage(damage)) { //TODO: PARAMETERIZE THIS DAMAGE VALUE SOME HOW! DO IT! DON'T FORGET!  DON'T DO IT!
                        debugPrintf("A player died! Resetting game...\n");
                        m_clientsReady = 0;
                        //static_cast<NetworkedSession*>(sess.get())->resetSession();
                        netSess->resetRound();
                        scene()->typedEntity<PlayerEntity>("player")->setPlayerMovement(true); //Allow the server to move freely

                        Array<shared_ptr<NetworkedEntity>> entities;
                        scene()->getTypedEntityArray<NetworkedEntity>(entities);
                        for (shared_ptr<NetworkedEntity> entity : entities) {
                            entity->respawn();
                        }

                        /* Send a respawn packet to everyone */
                        shared_ptr<RespawnClientPacket> respawnPacket = GenericPacket::createForBroadcast<RespawnClientPacket>();
                        respawnPacket->populate(m_networkFrameNum);
                        NetworkUtils::broadcastReliable(respawnPacket, m_localHost);
                        shared_ptr<AddPointPacket> pointPacket = GenericPacket::createReliable<AddPointPacket>(client->peer);
                        NetworkUtils::send(pointPacket);
                    }
                    // Notify every player of the hit
                    shared_ptr<PlayerInteractPacket> interactPacket = GenericPacket::createForBroadcast<PlayerInteractPacket>();
                    interactPacket->populate(m_networkFrameNum, PlayerActionType::Hit, typedPacket->m_shooterID);
                    Array<ENetAddress*> clientAddresses;
                    for (NetworkUtils::ConnectedClient* c : m_connectedClients) {
                        clientAddresses.append(&c->unreliableAddress);
                    }
                    /* Send this as a player interact packet so that clients log it */
                    NetworkUtils::broadcastUnreliable(interactPacket, &m_unreliableSocket, clientAddresses);
                    break;
            }
            case REPORT_FIRE:
                {
                    ReportFirePacket* typedPacket = static_cast<ReportFirePacket*> (inPacket.get());
                    m_dataHandler->UpdateFired(typedPacket->m_shooterID.toString16(), typedPacket->m_frameNumber);
                    break;
                }
            case READY_UP_CLIENT: {
                    m_clientsReady++;
                    debugPrintf("Connected Number of Clients: %d\nReady Clients: %d\n", m_connectedClients.length(), m_clientsReady);
                    if (m_clientsReady >= experimentConfig.numPlayers)
                    {
                        if (m_clientsReady >= experimentConfig.numPlayers)
                        {
                            if (sessConfig->numberOfRoundsPlayed % 2 == 0) {

                                m_clientFirstRoundPeeker = rand() % 2;
                                // Make them instantly spawn to the new location
                                m_peekersRoundConfigs[peekerDefenderConfigCombinationsIdx[sessConfig->numberOfRoundsPlayed / 2].first].respawnToPos = true;
                                m_defendersRoundConfigs[peekerDefenderConfigCombinationsIdx[sessConfig->numberOfRoundsPlayed / 2].second].respawnToPos = true;

                                shared_ptr<SendPlayerConfigPacket> outPacket = GenericPacket::createReliable<SendPlayerConfigPacket>(m_connectedClients[m_clientFirstRoundPeeker]->peer);
                                outPacket->populate(m_peekersRoundConfigs[peekerDefenderConfigCombinationsIdx[sessConfig->numberOfRoundsPlayed / 2].first], sessConfig->networkedSessionProgress);
                                NetworkUtils::send(outPacket);
                                outPacket = GenericPacket::createReliable<SendPlayerConfigPacket>(m_connectedClients[!m_clientFirstRoundPeeker]->peer);
                                outPacket->populate(m_defendersRoundConfigs[peekerDefenderConfigCombinationsIdx[sessConfig->numberOfRoundsPlayed / 2].second], sessConfig->networkedSessionProgress);
                                NetworkUtils::send(outPacket);

                                // Set Latency 
                                NetworkUtils::setAddressLatency(m_connectedClients[m_clientFirstRoundPeeker]->peer->address, m_peekersRoundConfigs[peekerDefenderConfigCombinationsIdx[sessConfig->numberOfRoundsPlayed / 2].first].clientLatency);
                                NetworkUtils::setAddressLatency(m_connectedClients[m_clientFirstRoundPeeker]->unreliableAddress, m_peekersRoundConfigs[peekerDefenderConfigCombinationsIdx[sessConfig->numberOfRoundsPlayed / 2].first].clientLatency);

                                NetworkUtils::setAddressLatency(m_connectedClients[!m_clientFirstRoundPeeker]->peer->address, m_defendersRoundConfigs[peekerDefenderConfigCombinationsIdx[sessConfig->numberOfRoundsPlayed / 2].second].clientLatency);
                                NetworkUtils::setAddressLatency(m_connectedClients[!m_clientFirstRoundPeeker]->unreliableAddress, m_defendersRoundConfigs[peekerDefenderConfigCombinationsIdx[sessConfig->numberOfRoundsPlayed / 2].second].clientLatency);
                            }
                            else {

                                // Make them instantly spawn to the new location
                                m_peekersRoundConfigs[peekerDefenderConfigCombinationsIdx[sessConfig->numberOfRoundsPlayed / 2].first].respawnToPos = true;
                                m_defendersRoundConfigs[peekerDefenderConfigCombinationsIdx[sessConfig->numberOfRoundsPlayed / 2].second].respawnToPos = true;

                                shared_ptr<SendPlayerConfigPacket> outPacket = GenericPacket::createReliable<SendPlayerConfigPacket>(m_connectedClients[!m_clientFirstRoundPeeker]->peer);
                                outPacket->populate(m_peekersRoundConfigs[peekerDefenderConfigCombinationsIdx[sessConfig->numberOfRoundsPlayed / 2].first], sessConfig->networkedSessionProgress);
                                NetworkUtils::send(outPacket);
                                outPacket = GenericPacket::createReliable<SendPlayerConfigPacket>(m_connectedClients[m_clientFirstRoundPeeker]->peer);
                                outPacket->populate(m_defendersRoundConfigs[peekerDefenderConfigCombinationsIdx[sessConfig->numberOfRoundsPlayed / 2].second], sessConfig->networkedSessionProgress);
                                NetworkUtils::send(outPacket);

                                // Set Latency 
                                NetworkUtils::setAddressLatency(m_connectedClients[!m_clientFirstRoundPeeker]->peer->address, m_peekersRoundConfigs[peekerDefenderConfigCombinationsIdx[sessConfig->numberOfRoundsPlayed / 2].first].clientLatency);
                                NetworkUtils::setAddressLatency(m_connectedClients[!m_clientFirstRoundPeeker]->unreliableAddress, m_peekersRoundConfigs[peekerDefenderConfigCombinationsIdx[sessConfig->numberOfRoundsPlayed / 2].first].clientLatency);

                                NetworkUtils::setAddressLatency(m_connectedClients[m_clientFirstRoundPeeker]->peer->address, m_defendersRoundConfigs[peekerDefenderConfigCombinationsIdx[sessConfig->numberOfRoundsPlayed / 2].second].clientLatency);
                                NetworkUtils::setAddressLatency(m_connectedClients[m_clientFirstRoundPeeker]->unreliableAddress, m_defendersRoundConfigs[peekerDefenderConfigCombinationsIdx[sessConfig->numberOfRoundsPlayed / 2].second].clientLatency);
                            }
                            shared_ptr<StartSessionPacket> startSessPacket = GenericPacket::createForBroadcast<StartSessionPacket>();
                            startSessPacket->populate(m_networkFrameNum);
                            NetworkUtils::broadcastReliable(startSessPacket, m_localHost);
                            m_clientFeedbackSubmitted = 0;
                            m_dataHandler = new ServerDataHandler();
                            m_dataHandler->SetParameters(experimentConfig.pastFrame);
                            for (auto c : m_connectedClients) {
                                m_dataHandler->AddNewClient(c->guid.toString16());
                            }
                            netSess.get()->startRound();
                            debugPrintf("All PLAYERS ARE READY!\n");
                        }
                    }
                    break;
            }
            case CLIENT_ROUND_TIMEOUT: {
                    m_clientsTimedOut++;

                    if (m_clientsTimedOut >= experimentConfig.numPlayers)
                    {
                        sessConfig->numberOfRoundsPlayed++;
                        debugPrintf("Rounds Played %d, Rounds Left %d\n", sessConfig->numberOfRoundsPlayed, sessConfig->trials[0].count - sessConfig->numberOfRoundsPlayed);
                        sessConfig->networkedSessionProgress = (float)sessConfig->numberOfRoundsPlayed / (float)sessConfig->trials[0].count;
                        debugPrintf("SESSION PROGRESS: %f\n", sessConfig->networkedSessionProgress);
                        m_clientsReady = 0;
                        m_clientsTimedOut = 0;
                        debugPrintf("Round Over!\n");

                        shared_ptr<ClientFeedbackStartPacket> outPacket = GenericPacket::createForBroadcast<ClientFeedbackStartPacket>();
                        NetworkUtils::broadcastReliable(outPacket, m_localHost);
                    }
                    break;
            }
            case CLIENT_FEEDBACK_SUBMITTED: {
                    m_clientFeedbackSubmitted++;

                    if (sessConfig->numberOfRoundsPlayed >= sessConfig->trials[0].count)
                    {
                        debugPrintf("SESSION OVER");
                        shared_ptr<ClientSessionEndPacket> outPacket = GenericPacket::createForBroadcast<ClientSessionEndPacket>();
                        NetworkUtils::broadcastReliable(outPacket, m_localHost);
                    }

                    else if (m_clientFeedbackSubmitted >= experimentConfig.numPlayers) {
                        shared_ptr<ResetClientRoundPacket> outPacket = GenericPacket::createForBroadcast<ResetClientRoundPacket>();
                        NetworkUtils::broadcastReliable(outPacket, m_localHost);
                    }
                    break;
            }
            default:
                debugPrintf("WARNING: unhandled packet receved on the reliable channel of type: %d\n", inPacket->type());
                break;
            }
        }

        inPacket = NetworkUtils::receivePacket(m_localHost, &m_unreliableSocket);
    }

    // only do if not in authoritative server mode, if yes, onASBroadcast() will handle this
    if (!experimentConfig.isAuthoritativeServer)
    {
        /* Now we send the latest position of all entities to all connected clients */
        Array<shared_ptr<NetworkedEntity>> entityArray;
        scene()->getTypedEntityArray<NetworkedEntity>(entityArray);
        Array<BatchEntityUpdatePacket::EntityUpdate> updates;
        // make update of latest validated position for each entity
        Array<ENetAddress*> clientAddresses;
        for (NetworkUtils::ConnectedClient* c : m_connectedClients) {
            String name = c->guid.toString16();
            int frameNum = m_dataHandler->lastValidFromFrameNum(name, m_networkFrameNum);
            updates.append(BatchEntityUpdatePacket::EntityUpdate(m_dataHandler->GetCFrame(frameNum, name), name, c->playerID, frameNum));
            clientAddresses.append(&c->unreliableAddress);
        }
        shared_ptr<BatchEntityUpdatePacket> updatePacket = GenericPacket::createForBroadcast<BatchEntityUpdatePacket>();
        updatePacket->populate(updates, BatchEntityUpdatePacket::NetworkUpdateType::REPLACE_FRAME);
        NetworkUtils::broadcastUnreliable(updatePacket, &m_unreliableSocket, clientAddresses);

        // Broadcast the PlayerConfig to all clients
        if (sessConfig->player.propagatePlayerConfigsToAll) {
            sessConfig->player.propagatePlayerConfigsToAll = false;
            shared_ptr<SendPlayerConfigPacket> configPacket = GenericPacket::createForBroadcast<SendPlayerConfigPacket>();
            configPacket->populate(sessConfig->player, sessConfig->networkedSessionProgress);
            NetworkUtils::broadcastReliable(configPacket, m_localHost);
        }

        if (sessConfig->player.propagatePlayerConfigsToSelectedClient) {
            sessConfig->player.propagatePlayerConfigsToSelectedClient = false;
            shared_ptr<SendPlayerConfigPacket> configPacket;
            if (sessConfig->player.selectedClientIdx == 0) {
                configPacket = GenericPacket::createReliable<SendPlayerConfigPacket>(m_connectedClients[0]->peer);
            }
            else {
                configPacket = GenericPacket::createReliable<SendPlayerConfigPacket>(m_connectedClients[1]->peer);
            }
            configPacket->populate(sessConfig->player, sessConfig->networkedSessionProgress);
            NetworkUtils::send(configPacket);
        }
    }
}

void FPSciServerApp::onInit() {
    this->setLowerFrameRateInBackground(startupConfig.lowerFrameRateInBackground);

    if (enet_initialize()) {
        throw std::runtime_error("Failed to initalize enet!");
    }
    // Seed random based on the time
    Random::common().reset(uint32(time(0)));

    GApp::onInit(); // Initialize the G3D application (one time)
    FPSciServerApp::initExperiment();
    preparePerRoundConfigs();
}

void FPSciServerApp::oneFrame() {
    // Count this frame (for shaders)
    m_frameNumber++;


    // Target frame time (only call this method once per one frame!)
    RealTime targetFrameTime = sess->targetFrameTime();

    // Wait
    // Note: we might end up spending all of our time inside of
    // RenderDevice::beginFrame.  Waiting here isn't double waiting,
    // though, because while we're sleeping the CPU the GPU is working
    // to catch up.    
    if ((submitToDisplayMode() == SubmitToDisplayMode::MINIMIZE_LATENCY)) {
        BEGIN_PROFILER_EVENT("Wait");
        m_waitWatch.tick(); {
            RealTime nowAfterLoop = System::time();

            // Compute accumulated time
            RealTime cumulativeTime = nowAfterLoop - m_lastWaitTime;

            debugAssert(m_wallClockTargetDuration < finf());
            // Perform wait for target time needed
            RealTime duration = targetFrameTime;
            if (!window()->hasFocus() && m_lowerFrameRateInBackground) {
                // Lower frame rate to 4fps
                duration = 1.0 / 4.0;
            }
            RealTime desiredWaitTime = max(0.0, duration - cumulativeTime);
            onWait(max(0.0, desiredWaitTime - m_lastFrameOverWait) * 0.97);

            // Update wait timers
            m_lastWaitTime = System::time();
            RealTime actualWaitTime = m_lastWaitTime - nowAfterLoop;

            // Learn how much onWait appears to overshoot by and compensate
            double thisOverWait = actualWaitTime - desiredWaitTime;
            if (G3D::abs(thisOverWait - m_lastFrameOverWait) / max(G3D::abs(m_lastFrameOverWait), G3D::abs(thisOverWait)) > 0.4) {
                // Abruptly change our estimate
                m_lastFrameOverWait = thisOverWait;
            }
            else {
                // Smoothly change our estimate
                m_lastFrameOverWait = lerp(m_lastFrameOverWait, thisOverWait, 0.1);
            }
        }  m_waitWatch.tock();
        END_PROFILER_EVENT();
    }

    for (int repeat = 0; repeat < max(1, m_renderPeriod); ++repeat) {
        Profiler::nextFrame();
        m_lastTime = m_now;
        m_now = System::time();
        RealTime timeStep = m_now - m_lastTime;

        // User input
        m_userInputWatch.tick();
        if (manageUserInput) {
            processGEventQueue();
        }
        onAfterEvents();
        onUserInput(userInput);
        m_userInputWatch.tock();

        // Network
        BEGIN_PROFILER_EVENT("FPSciNetworkApp::onNetwork");
        m_networkWatch.tick();
        onNetwork();
        m_networkWatch.tock();
        END_PROFILER_EVENT();

        // Logic
        m_logicWatch.tick();
        {
            onAI();
        }
        m_logicWatch.tock();

        // Validity checks
        checkFrameValidity();

        // Simulation
        m_simulationWatch.tick();
        BEGIN_PROFILER_EVENT("Simulation");
        {
            RealTime rdt = timeStep;

            SimTime sdt = m_simTimeStep;
            if (sdt == MATCH_REAL_TIME_TARGET) {
                sdt = (SimTime)targetFrameTime;
            }
            else if (sdt == REAL_TIME) {
                sdt = float(timeStep);
            }
            sdt *= m_simTimeScale;

            SimTime idt = (SimTime)targetFrameTime;

            onBeforeSimulation(rdt, sdt, idt);
            onSimulation(rdt, sdt, idt);
            simulateWeapons();
            onAfterSimulation(rdt, sdt, idt);

            m_previousSimTimeStep = float(sdt);
            m_previousRealTimeStep = float(rdt);
            setRealTime(realTime() + rdt);
            setSimTime(simTime() + sdt);
        }
        m_simulationWatch.tock();
        END_PROFILER_EVENT();

        // will only broadcast if in authoritative server mode
        onASBroadcast();
    }

    // Pose
    BEGIN_PROFILER_EVENT("Pose");
    m_poseWatch.tick();
    {
        m_posed3D.fastClear();
        m_posed2D.fastClear();
        onPose(m_posed3D, m_posed2D);

        // The debug camera is not in the scene, so we have
        // to explicitly pose it. This actually does nothing, but
        // it allows us to trigger the TAA code.
        playerCamera->onPose(m_posed3D);
    }
    m_poseWatch.tock();
    END_PROFILER_EVENT();

    // Wait
    // Note: we might end up spending all of our time inside of
    // RenderDevice::beginFrame.  Waiting here isn't double waiting,
    // though, because while we're sleeping the CPU the GPU is working
    // to catch up.    
    if ((submitToDisplayMode() != SubmitToDisplayMode::MINIMIZE_LATENCY)) {
        BEGIN_PROFILER_EVENT("Wait");
        m_waitWatch.tick(); {
            RealTime nowAfterLoop = System::time();

            // Compute accumulated time
            RealTime cumulativeTime = nowAfterLoop - m_lastWaitTime;

            debugAssert(m_wallClockTargetDuration < finf());
            // Perform wait for actual time needed
            RealTime duration = targetFrameTime;
            if (!window()->hasFocus() && m_lowerFrameRateInBackground) {
                // Lower frame rate to 4fps
                duration = 1.0 / 4.0;
            }
            RealTime desiredWaitTime = max(0.0, duration - cumulativeTime);
            onWait(max(0.0, desiredWaitTime - m_lastFrameOverWait) * 0.97);

            // Update wait timers
            m_lastWaitTime = System::time();
            RealTime actualWaitTime = m_lastWaitTime - nowAfterLoop;

            // Learn how much onWait appears to overshoot by and compensate
            double thisOverWait = actualWaitTime - desiredWaitTime;
            if (G3D::abs(thisOverWait - m_lastFrameOverWait) / max(G3D::abs(m_lastFrameOverWait), G3D::abs(thisOverWait)) > 0.4) {
                // Abruptly change our estimate
                m_lastFrameOverWait = thisOverWait;
            }
            else {
                // Smoothly change our estimate
                m_lastFrameOverWait = lerp(m_lastFrameOverWait, thisOverWait, 0.1);
            }
        }  m_waitWatch.tock();
        END_PROFILER_EVENT();
    }

    // Graphics
    debugAssertGLOk();
    if ((submitToDisplayMode() == SubmitToDisplayMode::BALANCE) && (!renderDevice->swapBuffersAutomatically()))
    {
        swapBuffers();
    }

    if (notNull(m_gazeTracker))
    {
        BEGIN_PROFILER_EVENT("Gaze Tracker");
        sampleGazeTrackerData();
        END_PROFILER_EVENT();
    }

    BEGIN_PROFILER_EVENT("Graphics");
    renderDevice->beginFrame();
    m_widgetManager->onBeforeGraphics();
    m_graphicsWatch.tick();
    {
        debugAssertGLOk();
        renderDevice->pushState();
        {
            debugAssertGLOk();
            onGraphics(renderDevice, m_posed3D, m_posed2D);
        }
        renderDevice->popState();
    }
    m_graphicsWatch.tock();
    renderDevice->endFrame();
    if ((submitToDisplayMode() == SubmitToDisplayMode::MINIMIZE_LATENCY) && (!renderDevice->swapBuffersAutomatically()))
    {
        swapBuffers();
    }
    END_PROFILER_EVENT();

    // Remove all expired debug shapes
    for (int i = 0; i < debugShapeArray.size(); ++i)
    {
        if (debugShapeArray[i].endTime <= m_now)
        {
            debugShapeArray.fastRemove(i);
            --i;
        }
    }

    for (int i = 0; i < debugLabelArray.size(); ++i)
    {
        if (debugLabelArray[i].endTime <= m_now)
        {
            debugLabelArray.fastRemove(i);
            --i;
        }
    }

    debugText.fastClear();

    m_posed3D.fastClear();
    m_posed2D.fastClear();

    if (m_endProgram && window()->requiresMainLoop()) {
        window()->popLoopBody();
    }
}

void FPSciServerApp::preparePerRoundConfigs() {

    int peekersConfigIdx = 0;
    int defendersConfigIdx = 0;

    for (int i = 0; i < sessConfig->player.respawnPosArray.size(); i++) {
        if (i % 2 == 0) {
            // Peekers
            for (int j = peekersConfigIdx; j < sessConfig->player.clientLatencyArray.size() + peekersConfigIdx; j++) {
                m_peekersRoundConfigs.push_back(sessConfig->player); // load with default values first

                // Create different peekers with different configs
                m_peekersRoundConfigs[j].respawnPos = sessConfig->player.respawnPosArray[i];
                m_peekersRoundConfigs[j].movementRestrictionX = sessConfig->player.movementRestrictionXArray[i];
                m_peekersRoundConfigs[j].movementRestrictionZ = sessConfig->player.movementRestrictionZArray[i];
                m_peekersRoundConfigs[j].restrictedMovementEnabled = sessConfig->player.restrictedMovementEnabledArray[i];
                m_peekersRoundConfigs[j].restrictionBoxAngle = sessConfig->player.restrictionBoxAngleArray[i];
                m_peekersRoundConfigs[j].playerType = "PEEKER";

                m_peekersRoundConfigs[j].clientLatency = sessConfig->player.clientLatencyArray[j - peekersConfigIdx];   //TODO CHANGE MOVERATE WITH LATENCY
            }
            peekersConfigIdx += sessConfig->player.clientLatencyArray.size();
            
        }
        else {
            // Defenders
            for (int j = defendersConfigIdx; j < sessConfig->player.clientLatencyArray.size() + defendersConfigIdx; j++) {
                m_defendersRoundConfigs.push_back(sessConfig->player); // load with default values first

                // Create different defenders with different configs
                m_defendersRoundConfigs[j].respawnPos = sessConfig->player.respawnPosArray[i];
                m_defendersRoundConfigs[j].respawnHeading = sessConfig->player.respawnHeadingArray[i];
                m_defendersRoundConfigs[j].movementRestrictionX = sessConfig->player.movementRestrictionXArray[i];
                m_defendersRoundConfigs[j].movementRestrictionZ = sessConfig->player.movementRestrictionZArray[i];
                m_defendersRoundConfigs[j].restrictedMovementEnabled = sessConfig->player.restrictedMovementEnabledArray[i];
                m_defendersRoundConfigs[j].restrictionBoxAngle = sessConfig->player.restrictionBoxAngleArray[i];
                m_defendersRoundConfigs[j].playerType = "DEFENDER";

                m_defendersRoundConfigs[j].clientLatency = sessConfig->player.clientLatencyArray[j - defendersConfigIdx];   //TODO CHANGE MOVERATE WITH LATENCY
            }
            defendersConfigIdx += sessConfig->player.clientLatencyArray.size();
        }
    }

    /// Create all possible pairs of peeker vs defender matchups
    
    for (int batch = 0; batch < m_defendersRoundConfigs.size() / sessConfig->player.clientLatencyArray.size(); batch++) {
        for (int pIdx = batch * sessConfig->player.clientLatencyArray.size(); pIdx < (batch + 1) * sessConfig->player.clientLatencyArray.size(); pIdx++) {
            for (int dIdx = batch * sessConfig->player.clientLatencyArray.size(); dIdx < (batch + 1) * sessConfig->player.clientLatencyArray.size(); dIdx++) {
                peekerDefenderConfigCombinationsIdx.push_back(std::make_pair(pIdx, dIdx));
            }
        }
    }

    /// Set number of rounds
    sessConfig->trials[0].count = peekerDefenderConfigCombinationsIdx.size() * 2;

    /// Shuffle combinations
    for (int i = 0; i < peekerDefenderConfigCombinationsIdx.size(); i++)
        debugPrintf("COMBINATION BEFORE SHUFFLE p%d d%d\n", peekerDefenderConfigCombinationsIdx[i].first, peekerDefenderConfigCombinationsIdx[i].second);
    
    std::random_shuffle(peekerDefenderConfigCombinationsIdx.begin(), peekerDefenderConfigCombinationsIdx.end());

    for (int i = 0; i < peekerDefenderConfigCombinationsIdx.size(); i++)
        debugPrintf("COMBINATION AFTER SHUFFLE p%d d%d\n", peekerDefenderConfigCombinationsIdx[i].first, peekerDefenderConfigCombinationsIdx[i].second);

    

    debugPrintf("PEEKER SIZE: %d", m_peekersRoundConfigs.size());
    debugPrintf("    DEFENDER SIZE: %d    COMBINATION SIZE %d\n", m_defendersRoundConfigs.size(), peekerDefenderConfigCombinationsIdx.size());
}

NetworkUtils::ConnectedClient* FPSciServerApp::getClientFromAddress(ENetAddress e)
{
    for (NetworkUtils::ConnectedClient* client : m_connectedClients) {
        if (client->unreliableAddress.host == e.host && (client->unreliableAddress.port == e.port || client->peer->address.port == e.port)) {
            return client;
        }
    }
    return new NetworkUtils::ConnectedClient();
}

NetworkUtils::ConnectedClient* FPSciServerApp::getClientFromGUID(GUniqueID ID)
{
    for (NetworkUtils::ConnectedClient* client : m_connectedClients) {
        if (client->guid == ID) {
            return client;
        }
    }
}

uint32 FPSciServerApp::frameNumFromID(GUniqueID id) 
{
    return getClientFromGUID(id)->frameNumber;
}

void FPSciServerApp::updateSession(const String& id, bool forceReload) {
    // Check for a valid ID (non-emtpy and
    Array<String> ids;
    experimentConfig.getSessionIds(ids);
    if (!id.empty() && ids.contains(id))
    {
        // Load the session config specified by the id
        sessConfig = experimentConfig.getSessionConfigById(id);
        logPrintf("User selected session: %s. Updating now...\n", id.c_str());
        m_userSettingsWindow->setSelectedSession(id);
        // Create the session based on the loaded config
        if (experimentConfig.isNetworked) {
            netSess = NetworkedSession::create(this, sessConfig);
            sess = (shared_ptr<Session>)netSess;
        }
        else {
            sess = Session::create(this, sessConfig);
        }
    }
    else
    {
        // Create an empty session
        sessConfig = SessionConfig::create();
        netSess = NetworkedSession::create(this);
        sess = (shared_ptr<Session>)netSess;
    }

    // Update reticle
    reticleConfig.index = sessConfig->reticle.indexSpecified ? sessConfig->reticle.index : currentUser()->reticle.index;
    reticleConfig.scale = sessConfig->reticle.scaleSpecified ? sessConfig->reticle.scale : currentUser()->reticle.scale;
    reticleConfig.color = sessConfig->reticle.colorSpecified ? sessConfig->reticle.color : currentUser()->reticle.color;
    reticleConfig.changeTimeS = sessConfig->reticle.changeTimeSpecified ? sessConfig->reticle.changeTimeS : currentUser()->reticle.changeTimeS;
    setReticle(reticleConfig.index);

    // Update the controls for this session
    updateControls(m_firstSession); // If first session consider showing the menu

    // Update the frame rate/delay
    updateParameters(sessConfig->render.frameDelay, sessConfig->render.frameRate);

    // Handle buffer setup here
    updateShaderBuffers();

    // Update shader table
    m_shaderTable.clear();
    if (!sessConfig->render.shader3D.empty())
    {
        m_shaderTable.set(sessConfig->render.shader3D, G3D::Shader::getShaderFromPattern(sessConfig->render.shader3D));
    }
    if (!sessConfig->render.shader2D.empty())
    {
        m_shaderTable.set(sessConfig->render.shader2D, G3D::Shader::getShaderFromPattern(sessConfig->render.shader2D));
    }
    if (!sessConfig->render.shaderComposite.empty())
    {
        m_shaderTable.set(sessConfig->render.shaderComposite, G3D::Shader::getShaderFromPattern(sessConfig->render.shaderComposite));
    }

    // Update shader parameters
    m_startTime = System::time();
    m_last2DTime = m_startTime;
    m_last3DTime = m_startTime;
    m_lastCompositeTime = m_startTime;
    m_frameNumber = 0;

    // Load (session dependent) fonts
    hudFont = GFont::fromFile(System::findDataFile(sessConfig->hud.hudFont));
    m_combatFont = GFont::fromFile(System::findDataFile(sessConfig->targetView.combatTextFont));

    // Handle clearing the targets here (clear any remaining targets before loading a new scene)
    if (notNull(scene()))
        sess->clearTargets();

    // Load the experiment scene if we haven't already (target only)
    if (sessConfig->scene.name.empty())
    {
        // No scene specified, load default scene
        if (m_loadedScene.name.empty() || forceReload)
        {
            loadScene(m_defaultSceneName); // Note: this calls onGraphics()
            m_loadedScene.name = m_defaultSceneName;
        }
        // Otherwise let the loaded scene persist
    }
    else if (sessConfig->scene != m_loadedScene || forceReload)
    {
        loadScene(sessConfig->scene.name);
        m_loadedScene = sessConfig->scene;
    }

    // Player parameters
    initPlayer();

    // Check for play mode specific parameters
    if (notNull(weapon))
        weapon->clearDecals();
    weapon->setConfig(&sessConfig->weapon);
    weapon->setScene(scene());
    weapon->setCamera(activeCamera());

    // Update weapon model (if drawn) and sounds
    weapon->loadModels();
    weapon->loadSounds();
    if (!sessConfig->audio.sceneHitSound.empty())
    {
        m_sceneHitSound = Sound::create(System::findDataFile(sessConfig->audio.sceneHitSound));
    }
    if (!sessConfig->audio.refTargetHitSound.empty())
    {
        m_refTargetHitSound = Sound::create(System::findDataFile(sessConfig->audio.refTargetHitSound));
    }

    // Load static HUD textures
    for (StaticHudElement element : sessConfig->hud.staticElements)
    {
        hudTextures.set(element.filename, Texture::fromFile(System::findDataFile(element.filename)));
    }

    // Update colored materials to choose from for target health
    for (String id : sessConfig->getUniqueTargetIds())
    {
        shared_ptr<TargetConfig> tconfig = experimentConfig.getTargetConfigById(id);
        materials.remove(id);
        materials.set(id, makeMaterials(tconfig));
    }

    const String resultsDirPath = startupConfig.experimentList[experimentIdx].resultsDirPath;

    // Check for need to start latency logging and if so run the logger now
    if (!FileSystem::isDirectory(resultsDirPath))
    {
        FileSystem::createDirectory(resultsDirPath);
    }

    // Create and check log file name
    const String logFileBasename = sessConfig->logger.logToSingleDb ? experimentConfig.description + "_" + userStatusTable.currentUser + "_" + m_expConfigHash : id + "_" + userStatusTable.currentUser + "_" + String(FPSciLogger::genFileTimestamp());
    const String logFilename = FilePath::makeLegalFilename(logFileBasename);
    // This is the specified path and log basename with illegal characters replaced, but not suffix (.db)
    const String logPath = resultsDirPath + logFilename;

    if (systemConfig.hasLogger)
    {
        if (!sessConfig->clickToPhoton.enabled)
        {
            logPrintf("WARNING: Using a click-to-photon logger without the click-to-photon region enabled!\n\n");
        }
        if (m_pyLogger == nullptr)
        {
            m_pyLogger = PythonLogger::create(systemConfig.loggerComPort, systemConfig.hasSync, systemConfig.syncComPort);
        }
        else
        {
            // Handle running logger if we need to (terminate then merge results)
            m_pyLogger->mergeLogToDb();
        }
        // Run a new logger if we need to (include the mode to run in here...)
        m_pyLogger->run(logPath, sessConfig->clickToPhoton.mode);
    }

    // Initialize the experiment (this creates the results file)
    sess->onInit(logPath, experimentConfig.description + "/" + sessConfig->description);

    // Don't create a results file for a user w/ no sessions left
    if (m_userSettingsWindow->sessionsForSelectedUser() == 0)
    {
        logPrintf("No sessions remaining for selected user.\n");
    }
    else if (sessConfig->logger.enable)
    {
        logPrintf("Created results file: %s.db\n", logPath.c_str());
    }

    if (m_firstSession)
    {
        m_firstSession = false;
    }

    if (experimentConfig.isNetworked) {
        for (NetworkUtils::ConnectedClient* client : m_connectedClients) {
            /* Set the latency for every client that is currently connected */
            NetworkUtils::setAddressLatency(client->peer->address, sessConfig->networkLatency);
            NetworkUtils::setAddressLatency(client->unreliableAddress, sessConfig->networkLatency);
        }
    }
}

void FPSciServerApp::onASBroadcast()
{
    // only do if in authoritative server mode
    if (experimentConfig.isAuthoritativeServer)
    {
        /* Now we send the latest position of all entities to all connected clients */
        Array<shared_ptr<NetworkedEntity>> entityArray;
        scene()->getTypedEntityArray<NetworkedEntity>(entityArray);
        Array<BatchEntityUpdatePacket::EntityUpdate> updates;
        // make update of latest validated position for each entity
        Array<ENetAddress*> clientAddresses;
        for (NetworkUtils::ConnectedClient* c : m_connectedClients) {
            String name = c->guid.toString16();
            int frameNum = m_dataHandler->lastValidFromFrameNum(name, m_networkFrameNum);
            updates.append(BatchEntityUpdatePacket::EntityUpdate(m_dataHandler->GetCFrame(frameNum, name), name, c->playerID, frameNum));
            clientAddresses.append(&c->unreliableAddress);
        }
        shared_ptr<BatchEntityUpdatePacket> updatePacket = GenericPacket::createForBroadcast<BatchEntityUpdatePacket>();
        updatePacket->populate(updates, BatchEntityUpdatePacket::NetworkUpdateType::REPLACE_FRAME);
        NetworkUtils::broadcastUnreliable(updatePacket, &m_unreliableSocket, clientAddresses);

        // Broadcast the PlayerConfig to all clients
        if (sessConfig->player.propagatePlayerConfigsToAll) {
            sessConfig->player.propagatePlayerConfigsToAll = false;
            shared_ptr<SendPlayerConfigPacket> configPacket = GenericPacket::createForBroadcast<SendPlayerConfigPacket>();
            configPacket->populate(sessConfig->player, sessConfig->networkedSessionProgress);
            NetworkUtils::broadcastReliable(configPacket, m_localHost);
        }

        if (sessConfig->player.propagatePlayerConfigsToSelectedClient) {
            sessConfig->player.propagatePlayerConfigsToSelectedClient = false;
            shared_ptr<SendPlayerConfigPacket> configPacket;
            if (sessConfig->player.selectedClientIdx == 0) {
                configPacket = GenericPacket::createReliable<SendPlayerConfigPacket>(m_connectedClients[0]->peer);
            }
            else {
                configPacket = GenericPacket::createReliable<SendPlayerConfigPacket>(m_connectedClients[1]->peer);
            }
            configPacket->populate(sessConfig->player, sessConfig->networkedSessionProgress);
            NetworkUtils::send(configPacket);
        }
    }
}

void FPSciServerApp::checkFrameValidity()
{
    for (auto& i1 : m_connectedClients)
    {
        shared_ptr<NetworkedEntity> e1 = i1->entity;

        // Player displacement check
        // Check if a player is moving too quickly, based on player max speed and frame rate
        // If so, snap the player to the last valid position
        CoordinateFrame pastFrame = m_dataHandler->GetCFrame(
            m_dataHandler->m_clientLastValid->get(i1->guid.toString16()),
            i1->guid.toString16());
        CoordinateFrame currentFrame = e1->frame();
        float dist = (currentFrame.translation - pastFrame.translation).length();
        float maxDist = 0.012; // magic number
        // 0.0114698 is calculated moving average of player speed in game units when moveRate is 7m/s
        int framesElapsed = 1; //TODO: get frames elapsed since last valid frame
        auto zeroFrame = new CoordinateFrame();
        if (dist > maxDist * framesElapsed && pastFrame.translation != zeroFrame->translation)
        {
            logPrintf("Player %d moved too far in the past %d frames. Moving back to last valid position.\n", i1->playerID, framesElapsed);
            snapBackPlayer(i1->playerID);
        }

        // Player to player collision detection
        // Check if two players are too close, based on body radius
        // If so, snap the players to the last valid positions
        Point3 player1Pos = e1->frame().translation;
        for (auto& i2 : m_connectedClients)
        {
            shared_ptr<NetworkedEntity> e2 = i2->entity;
            Point3 player2Pos = e2->frame().translation;
            double dist = sqrt(pow(player1Pos.x - player2Pos.x, 2) + pow(player1Pos.y - player2Pos.y, 2) + pow(player1Pos.z - player2Pos.z, 2));
            // Hardcoded to 1.0 * 2 m based on sphere parameters in PlayerEntity, maybe move to config later?
            if (dist < 2 && e1->getPlayerID() != e2->getPlayerID()) {
                debugPrintf("%d collided with %d\n", i1->playerID, i2->playerID);
                snapBackPlayer(i1->playerID);
                snapBackPlayer(i2->playerID);
            }
        }
        m_dataHandler->ValidateData(i1->guid.toString16(), m_dataHandler->m_clientLatestFrame->get(i1->guid.toString16()));
        i1->camera->setFrame(m_dataHandler->GetCFrame(m_dataHandler->m_clientLastValid->get(i1->guid.toString16()), i1->guid.toString16()));
    }
    Array<int32>* values = new Array<int32>;
    m_dataHandler->m_clientLastValid->getValues(*values);
}

void FPSciServerApp::simulateWeapons()
{
    for (auto input : *m_dataHandler->m_unreadFiredbuffer)
    {
        if (input.m_fired)
        {
            // Time Warp
            if (experimentConfig.timeWarpEnabled)
            {
                TimeWarpFrameSetup(input.m_frameNum);
            }
            else // Use latest frame data
            {
                CurrentTimeFrameSetup();
            }
            Array<shared_ptr<Entity>> dontHit;
            dontHit.append(m_explosions);
            dontHit.append(sess->unhittableTargets());
            Model::HitInfo info;
            float hitDist = finf();
            int hitIdx = -1;

            shared_ptr<TargetEntity> target;
            NetworkUtils::ConnectedClient *shooter = nullptr;
            Array<shared_ptr<TargetEntity>> targets;
            for (auto client : m_connectedClients)
            {
                if (client->guid.toString16() == input.m_playerID)
                {
                    // Don't hit shooter
                    dontHit.append(client->entity);
                    shooter = client;
                }
                else
                {
                    targets.append(client->entity);
                }
            }

            if (shooter != nullptr) // Fire the weapon
            {
                target = shooter->weapon->fire(targets, hitIdx, hitDist, info, dontHit, false);
            }
            if (!isNull(target)) // Hit case
            {
                debugPrintf("Player %s hit target %s", input.m_playerID.c_str(), target->name().c_str());

                // Any changes to this might also need to be reflected in Server side Sim (case PacketType::REPORT_HIT:)

                // This just causes everyone to respawn
                shared_ptr<NetworkedEntity> hitEntity = dynamic_pointer_cast<NetworkedEntity>(target);
                // Log the hit on the server
                const shared_ptr<NetworkedEntity> shooterEntity = scene()->typedEntity<NetworkedEntity>(input.m_playerID);
                auto rpa = RemotePlayerAction();
                rpa.time = sess->logger->getFileTime();
                rpa.viewDirection = shooterEntity->getLookAzEl();
                rpa.position = shooterEntity->frame().translation;
                rpa.state = sess->currentState;
                rpa.action = Hit;
                rpa.actorID = input.m_playerID;
                rpa.affectedID = input.m_playerID;
                sess->logger->logRemotePlayerAction(rpa);

                float damage = 1.001 / sessConfig->hitsToKill;

                if (hitEntity->doDamage(damage))
                {
                    //TODO: PARAMETERIZE THIS DAMAGE VALUE SOME HOW! DO IT! DON'T FORGET!  DON'T DO IT! (huh it looks like you still haven't done it)
                    debugPrintf("Server determined a player died! Resetting game...\n");
                    m_clientsReady = 0;
                    //static_cast<NetworkedSession*>(sess.get())->resetSession();
                     netSess->resetRound();
                    scene()->typedEntity<PlayerEntity>("player")->setPlayerMovement(true);
                    //Allow the server to move freely

                    Array<shared_ptr<NetworkedEntity>> entities;
                    scene()->getTypedEntityArray<NetworkedEntity>(entities);
                    for (shared_ptr<NetworkedEntity> entity : entities)
                    {
                        entity->respawn();
                    }

                    /* Send a respawn packet to everyone */
                    shared_ptr<RespawnClientPacket> respawnPacket = GenericPacket::createForBroadcast<RespawnClientPacket>();
                    respawnPacket->populate(m_networkFrameNum);
                    NetworkUtils::broadcastReliable(respawnPacket, m_localHost);
                    // Find the connected client's ENetPeer to add point
                    // TODO: Server Authoritative points system
                    for (auto client : m_connectedClients)
                    {
                        if (client->guid.toString16() == input.m_playerID)
                        {
                            shared_ptr<AddPointPacket> pointPacket = GenericPacket::createReliable<AddPointPacket>(client->peer);
                            NetworkUtils::send(pointPacket);
                        }
                    }
                }
                // Notify every player of the hit
                shared_ptr<PlayerInteractPacket> interactPacket = GenericPacket::createForBroadcast<
                    PlayerInteractPacket>();
                interactPacket->populate(m_networkFrameNum, Hit, GUniqueID::fromString16(input.m_playerID));
                Array<ENetAddress*> clientAddresses;
                for (NetworkUtils::ConnectedClient* c : m_connectedClients)
                {
                    clientAddresses.append(&c->unreliableAddress);
                }
                /* Send this as a player interact packet so that clients log it */
                NetworkUtils::broadcastUnreliable(interactPacket, &m_unreliableSocket, clientAddresses);
                break;
            }
            // Time Warp
            if (experimentConfig.timeWarpEnabled)
            {
                CurrentTimeFrameSetup();
            }
        }
    }

    m_dataHandler->FlushFiredBuffer();
}

void FPSciServerApp::snapBackPlayer(uint8 playerID)
{
    // change the player's position to the last valid position
    int snapBackFrameNum = m_dataHandler->lastValidFromFrameNum(
          m_connectedClients[playerID]->guid.toString16(), 
        m_dataHandler->m_clientLastValid->get((m_connectedClients[playerID]->guid.toString16())));
    // snap back to the last valid position if not zero
    CoordinateFrame snapBackFrame = m_dataHandler->GetCFrame(snapBackFrameNum,
        m_connectedClients[playerID]->guid.toString16());
    auto zeroFrame = new CoordinateFrame();
    if (snapBackFrame != *zeroFrame)
    {
        m_dataHandler->UpdateCframe(m_connectedClients[playerID]->guid.toString16(), snapBackFrame, m_networkFrameNum, false);
        m_connectedClients[playerID]->camera->setFrame(snapBackFrame);
        m_connectedClients[playerID]->entity->setFrame(snapBackFrame);
    }
    else {
        
    }
}

void FPSciServerApp::TimeWarpFrameSetup(uint32 frameNum) {
    Array<shared_ptr<NetworkedEntity>> entityArray;
    scene()->getTypedEntityArray<NetworkedEntity>(entityArray);
    for (shared_ptr<NetworkedEntity> e : entityArray) {
        m_connectedClients[e->getPlayerID()]->camera->setFrame(
            m_dataHandler->GetCFrame(m_dataHandler->lastValidFromFrameNum(e->name(), frameNum), e->name()));
        e->setFrame(m_dataHandler->GetCFrame(m_dataHandler->lastValidFromFrameNum(e->name(), frameNum), e->name()));

    }
}

void FPSciServerApp::CurrentTimeFrameSetup() {
    Array<shared_ptr<NetworkedEntity>> entityArray;
    scene()->getTypedEntityArray<NetworkedEntity>(entityArray);
    for (shared_ptr<NetworkedEntity> e : entityArray) {
        m_connectedClients[e->getPlayerID()]->camera->setFrame(
            m_dataHandler->GetCFrame(m_dataHandler->lastValidFromFrameNum(e->name(), m_dataHandler->m_clientLastValid->get(e->name())), e->name()));
        e->setFrame(m_dataHandler->GetCFrame(m_dataHandler->m_clientLastValid->get(e->name()), e->name()));
    }
}

uint8 FPSciServerApp::GUIDtoPlayerID(GUniqueID guid)
{
    // search for the player with the given GUID
    for (auto& client : m_connectedClients)
        if (client->guid == guid)
            return client->playerID;
    return -1;
}

GUniqueID FPSciServerApp::playerIDtoGUID(uint8 playerID)
{
    // search for the player with the given playerID
    for (auto& client : m_connectedClients)
        if (client->playerID == playerID)
            return client->guid;
    return GUniqueID::NONE(0);
}
