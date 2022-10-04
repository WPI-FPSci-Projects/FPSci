/** \file FPSciServerApp.cpp */
#include "FPSciServerApp.h"
#include "PhysicsScene.h"
#include "WaypointManager.h"
#include "NetworkedSession.h"
#include <Windows.h>

FPSciServerApp::FPSciServerApp(const GApp::Settings& settings) : FPSciApp(settings) {}




void FPSciServerApp::initExperiment() {
    playersReady = 0;
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
    shared_ptr<PlayerEntity> player = scene()->typedEntity<PlayerEntity>("player");
    player->setPlayerMovement(true);
}

void FPSciServerApp::onNetwork() {
    /* None of this is from the upsteam project */

    //if (!static_cast<NetworkedSession*>(sess.get())->currentState == NetworkedPresentationState::networkedSessionStart) {
    m_networkFrameNum++;
    //}
    
    /* First we receive on the unreliable connection */

    shared_ptr<GenericPacket> inPacket = NetworkUtils::receivePacket(m_localHost, &m_unreliableSocket);
    char ip[16];
    ENetAddress srcAddr = inPacket->srcAddr();
    enet_address_get_host_ip(&srcAddr, ip, 16);
    NetworkUtils::ConnectedClient* client = getClientFromAddress(inPacket->srcAddr());
    if (!inPacket->reliable()){
        switch (inPacket->type()) {
        case HANDSHAKE: {
            shared_ptr<HandshakeReplyPacket> outPacket = GenericPacket::createUnreliable<HandshakeReplyPacket>(&m_unreliableSocket, &client->unreliableAddress);
            if (outPacket->send() <= 0) {
                debugPrintf("Failed to send the handshke reply\n");
            }
            break;
        }
        case BATCH_ENTITY_UPDATE: {
            BatchEntityUpdatePacket* typedPacket = static_cast<BatchEntityUpdatePacket*> (inPacket.get());
            for (BatchEntityUpdatePacket::EntityUpdate e: typedPacket->m_updates) {
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
                        break;
                    }
                }
            }
            break;
        }
        case PLAYER_INTERACT:{
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
        default:
            debugPrintf("WARNING: received a packet of type %d on the unreliable channel that was not handled\n", inPacket->type());
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
            /* Removes the clinet from the list of connected clients and orders all other clients to delete that entity */
            shared_ptr<NetworkedEntity> entity = scene()->typedEntity<NetworkedEntity>(client->guid.toString16());
            if (entity != nullptr) {
                scene()->remove(entity);
            }
            shared_ptr<DestroyEntityPacket> outPacket = GenericPacket::createReliable<DestroyEntityPacket>(client->peer);
            outPacket->populate(m_networkFrameNum, client->guid);
            outPacket->send();
            for (int i = 0; i < m_connectedClients.length(); i++) {
                if (m_connectedClients[i]->guid == client->guid) {
                    m_connectedClients.remove(i, 1);
                }
            }
            break;
        }
        case REGISTER_CLIENT: {
            RegisterClientPacket* typedPacket = static_cast<RegisterClientPacket*> (inPacket.get());
            debugPrintf("Registering client...\n");
            NetworkUtils::ConnectedClient* newClient = NetworkUtils::registerClient(typedPacket);   // TODO: Decide if this should be in NetworkUtils or not
            m_connectedClients.append(newClient);
            /* Reply to the registration */
            shared_ptr<RegistrationReplyPacket> registrationReply = GenericPacket::createReliable<RegistrationReplyPacket>(newClient->peer);
            registrationReply->populate(newClient->guid, 0);
            registrationReply->send();
            debugPrintf("\tRegistered client: %s\n", newClient->guid.toString16());

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

            target->setWorldSpace(true);
            target->setColor(G3D::Color3(20.0, 20.0, 200.0));

            /* Add the new target to the scene */
            (*scene()).insert(target);

            /* ADD NEW CLIENT TO OTHER CLIENTS, ADD OTHER CLIENTS TO NEW CLIENT */
            shared_ptr<CreateEntityPacket> createEntityPacket = GenericPacket::createForBroadcast<CreateEntityPacket>();
            createEntityPacket->populate(m_networkFrameNum, newClient->guid);
            NetworkUtils::broadcastReliable(createEntityPacket, m_localHost);
            debugPrintf("Sent a broadcast packet to all connected peers\n");

            for (int i = 0; i < m_connectedClients.length(); i++) {
                // Create entitys on the new client for all other clients
                if (newClient->guid != m_connectedClients[i]->guid) {
                    createEntityPacket = GenericPacket::createReliable<CreateEntityPacket>(client->peer);
                    createEntityPacket->populate(m_networkFrameNum, m_connectedClients[i]->guid);
                    createEntityPacket->send();
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
                setSpawnPacket->send();
                shared_ptr<RespawnClientPacket> respawnPacket = GenericPacket::createReliable<RespawnClientPacket>(newClient->peer);
                respawnPacket->populate();
                respawnPacket->send();
            }
        }
        case REPORT_HIT: {
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

            if (hitEntity->doDamage(damage)) { //TODO PARAMETERIZE THIS DAMAGE VALUE SOME HOW! DO IT! DON'T FORGET!  DON'T DO IT!
                debugPrintf("A player died! Resetting game...\n");
                playersReady = 0;
                //static_cast<NetworkedSession*>(sess.get())->resetSession();
                netSess->resetSession();
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
        case READY_UP_CLIENT: {
            playersReady++;
            debugPrintf("Connected Number of Clients: %d\nReady Clints: %d\n", m_connectedClients.length(), playersReady);
            if (playersReady >= experimentConfig.numPlayers)
            {
                netSess->startSession();
                shared_ptr<StartSessionPacket> sessionStartPacket = GenericPacket::createForBroadcast<StartSessionPacket>();
                sessionStartPacket->populate(m_networkFrameNum);
                NetworkUtils::broadcastReliable(sessionStartPacket, m_localHost);
                debugPrintf("All PLAYERS ARE READY!\n");
            }
        }
        }
    }

    /* Now we send the position of all entities to all connected clients */
    Array<shared_ptr<NetworkedEntity>> entityArray;
    scene()->getTypedEntityArray<NetworkedEntity>(entityArray);
    shared_ptr<BatchEntityUpdatePacket> updatePacket = GenericPacket::createForBroadcast<BatchEntityUpdatePacket>();
    updatePacket->populate(m_networkFrameNum, entityArray, BatchEntityUpdatePacket::NetworkUpdateType::REPLACE_FRAME);
    
    Array<ENetAddress*> clientAddresses;
    for (NetworkUtils::ConnectedClient* c : m_connectedClients) {
        clientAddresses.append(&c->unreliableAddress);
    }
    NetworkUtils::broadcastUnreliable(updatePacket, &m_unreliableSocket, clientAddresses);
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
            onAfterSimulation(rdt, sdt, idt);

            m_previousSimTimeStep = float(sdt);
            m_previousRealTimeStep = float(rdt);
            setRealTime(realTime() + rdt);
            setSimTime(simTime() + sdt);
        }
        m_simulationWatch.tock();
        END_PROFILER_EVENT();
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
