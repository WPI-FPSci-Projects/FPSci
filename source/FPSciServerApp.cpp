/** \file FPSciServerApp.cpp */
#include "FPSciServerApp.h"
#include "PhysicsScene.h"
#include "WaypointManager.h"
#include <Windows.h>

FPSciServerApp::FPSciServerApp(const GApp::Settings& settings) : FPSciApp(settings) {}


void FPSciServerApp::initExperiment() {
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
        ENetAddress addr_from;
        ENetBuffer buff;
        void* data = malloc(ENET_HOST_DEFAULT_MTU);  //Allocate 1 mtu worth of space for the data from the packet
        buff.data = data;
        buff.dataLength = ENET_HOST_DEFAULT_MTU;

        while (true) {
            while (enet_socket_receive(socket, &addr_from, &buff, 1)) {
                char ip[16];
                enet_address_get_host_ip(&addr_from, ip, 16);
                BinaryInput packet_contents((const uint8*)buff.data, buff.dataLength, G3D_BIG_ENDIAN, false, true);
                NetworkUtils::MessageType type = (NetworkUtils::MessageType)packet_contents.readUInt8();

                if (type == NetworkUtils::MessageType::PING) {
                    NetworkUtils::sendPingReply(socket, addr_from, &buff);
                }
            }
        }
    };

    std::thread s2cPing_Th(s2cPing, m_pingSocket);
    s2cPing_Th.detach();
    
}

void FPSciServerApp::onNetwork() {
    /* None of this is from the upsteam project */

    /* First we receive on the unreliable connection */

    ENetAddress addr_from;
    ENetBuffer buff;
    void* data = malloc(ENET_HOST_DEFAULT_MTU);  //Allocate 1 mtu worth of space for the data from the packet
    buff.data = data;
    buff.dataLength = ENET_HOST_DEFAULT_MTU;

    while (enet_socket_receive(m_unreliableSocket, &addr_from, &buff, 1)) { //while there are packets to receive
        /* Unpack the basic data from the packet */
        char ip[16];
        enet_address_get_host_ip(&addr_from, ip, 16);
        BinaryInput packet_contents((const uint8*)buff.data, buff.dataLength, G3D_BIG_ENDIAN, false, true);
        NetworkUtils::MessageType type = (NetworkUtils::MessageType)packet_contents.readUInt8();

        /* Respond to a handsake request */
        if (type == NetworkUtils::MessageType::HANDSHAKE) {
            debugPrintf("Replying to handshake...\n");
            if (NetworkUtils::sendHandshakeReply(m_unreliableSocket, addr_from) <= 0) {
                debugPrintf("Failed to send reply...\n");
            };
        }
        /* If the client is trying to update an entity's positon on the server */
        else if (type == NetworkUtils::MessageType::BATCH_ENTITY_UPDATE) {
            //update locally entity displayed on the server: 
            int num_packet_members = packet_contents.readUInt8(); // get # of frames in this packet
            for (int i = 0; i < num_packet_members; i++) { // get new frames and update objects
                NetworkUtils::updateEntity(Array<GUniqueID>(), scene(), packet_contents); // Read the data from the packet and update on the local entity
            }
        }

    }
    free(data);

    /* Now we handle any incoming packets on the reliable connection */

    ENetEvent event;
    while (enet_host_service(m_localHost, &event, 0) > 0) {    // This services the host; processing all activity on a host including sending and recieving packets then a single inbound packet is returned as an event
        /* Unpack basic data from packet */
        debugPrintf("Processing Reliable Packet.... %i\n", event.type);
        char ip[16];
        enet_address_get_host_ip(&event.peer->address, ip, 16);
        if (event.type == ENET_EVENT_TYPE_CONNECT) {
            debugPrintf("connection recieved...\n");
            logPrintf("made connection to %s in response to input\n", ip);
        }
        else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
            debugPrintf("disconnection recieved...\n");
            logPrintf("%s disconnected.\n", ip);
            /* Remvoes the clinet from the list of connected clients and orders all other clients to delete that entity */
            for (int i = 0; i < m_connectedClients.size(); i++) {
                if (m_connectedClients[i].peer->address.host == event.peer->address.host &&
                    m_connectedClients[i].peer->address.port == event.peer->address.port) {
                    GUniqueID id = m_connectedClients[i].guid;
                    shared_ptr<NetworkedEntity> entity = scene()->typedEntity<NetworkedEntity>(id.toString16());
                    if (entity != nullptr) {
                        scene()->remove(entity);
                    }
                    m_connectedClients.remove(i, 1);
                    NetworkUtils::broadcastDestroyEntity(id, m_localHost);
                }
            }
        }
        else if (event.type == ENET_EVENT_TYPE_RECEIVE) {

            BinaryInput packet_contents(event.packet->data, event.packet->dataLength, G3D_BIG_ENDIAN);
            NetworkUtils::MessageType type = (NetworkUtils::MessageType)packet_contents.readUInt8();

            /* Now parse the type of message we received */

            if (type == NetworkUtils::MessageType::REGISTER_CLIENT) {
                debugPrintf("Registering client...\n");
                NetworkUtils::ConnectedClient newClient = NetworkUtils::registerClient(event, packet_contents);
                m_connectedClients.append(newClient);
                debugPrintf("\tRegistered client: %s\n", newClient.guid.toString16());

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
                const shared_ptr<NetworkedEntity>& target = NetworkedEntity::create(newClient.guid.toString16(), &(*scene()), model, CFrame());

                target->setWorldSpace(true);
                target->setColor(G3D::Color3(20.0, 20.0, 200.0));

                /* Add the new target to the scene */
                (*scene()).insert(target);

                /* ADD NEW CLIENT TO OTHER CLIENTS, ADD OTHER CLIENTS TO NEW CLIENT */

                NetworkUtils::broadcastCreateEntity(newClient.guid, m_localHost);
                debugPrintf("Sent a broadcast packet to all connected peers\n");

                for (int i = 0; i < m_connectedClients.length(); i++) {
                    // Create entitys on the new client for all other clients
                    if (newClient.guid != m_connectedClients[i].guid) {
                        NetworkUtils::sendCreateEntity(m_connectedClients[i].guid, newClient.peer);
                        debugPrintf("Sent add to %s to add %s\n", newClient.guid.toString16(), m_connectedClients[i].guid.toString16());
                    }
                }
                // move the client to a different location
                // TODO: Make this smart not just some test code
                if (m_connectedClients.length() % 2 == 0) {
                    Point3 position = Point3(-46, -2.3, 0);
                    float heading = 90;
                    NetworkUtils::sendSetSpawnPos(position, heading, event.peer);
                    //CFrame frame = CFrame::fromXYZYPRDegrees(-46, -2.3, 0, -90, -0, 0);
                    //NetworkUtils::sendMoveClient(frame, event.peer);
                    NetworkUtils::sendRespawnClient(event.peer);
                }
            }
            else if (type == NetworkUtils::MessageType::REPORT_HIT) {
                NetworkUtils::handleHitReport(m_localHost, packet_contents);
            }
            enet_packet_destroy(event.packet);
        }
    }

    /* Now we send the position of all entities to all connected clients */
    Array<shared_ptr<NetworkedEntity>> entityArray;
    scene()->getTypedEntityArray<NetworkedEntity>(entityArray);
    
    NetworkUtils::serverBatchEntityUpdate(entityArray, m_connectedClients, m_unreliableSocket);
}

void FPSciServerApp::onInit() {
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