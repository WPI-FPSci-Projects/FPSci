/** \file FPSciApp.cpp */
#include "FPSciApp.h"
#include "Dialogs.h"
#include "Logger.h"
#include "Session.h"
#include "NetworkedSession.h"
#include "PhysicsScene.h"
#include "WaypointManager.h"
#include <chrono>

// Storage for configuration static vars
int TrialCount::defaultCount;
Array<String> UserSessionStatus::defaultSessionOrder;
bool UserSessionStatus::randomizeDefaults;

StartupConfig FPSciApp::startupConfig;

FPSciApp::FPSciApp(const GApp::Settings& settings) : GApp(settings) {}

FPSciApp::FPSciApp(const GApp::Settings& settings, OSWindow* window, RenderDevice* rd, bool createWindowOnNull) : GApp(settings, window, rd, createWindowOnNull) {}

/** Initialize the app */
void FPSciApp::onInit() {
	this->setLowerFrameRateInBackground(startupConfig.lowerFrameRateInBackground);
	// Seed random based on the time
	Random::common().reset(uint32(time(0)));

	GApp::onInit(); // Initialize the G3D application (one time)
	// TODO: Move validateExperiments() to a developer mode GUI button
	// startupConfig.validateExperiments();
	initExperiment(); // Initialize the experiment
}

void FPSciApp::initExperiment() {
	// Load config from files
	loadConfigs(startupConfig.experimentList[experimentIdx]);
	m_lastSavedUser = *currentUser(); // Copy over the startup user for saves

	// Setup the display mode
	setSubmitToDisplayMode(
		// SubmitToDisplayMode::EXPLICIT);
		SubmitToDisplayMode::MINIMIZE_LATENCY);
	// SubmitToDisplayMode::BALANCE);
	// SubmitToDisplayMode::MAXIMIZE_THROUGHPUT);

	// Set the initial simulation timestep to REAL_TIME. The desired timestep is set later.
	setFrameDuration(frameDuration(), REAL_TIME);
	m_lastOnSimulationRealTime = 0.0;

	// Setup/update waypoint manager
	if (startupConfig.developerMode && startupConfig.waypointEditorMode)
	{
		waypointManager = WaypointManager::create(this);
	}

	// Setup the scene
	setScene(PhysicsScene::create(m_ambientOcclusion));
	scene()->registerEntitySubclass("PlayerEntity", &PlayerEntity::create); // Register the player entity for creation
	scene()->registerEntitySubclass("FlyingEntity", &FlyingEntity::create); // Register the target entity for creation
	scene()->registerEntitySubclass("NetworkedEntity", &NetworkedEntity::create); //Register any networked entities for creation

	weapon = Weapon::create(&experimentConfig.weapon, scene(), activeCamera());
	weapon->setHitCallback(std::bind(&FPSciApp::hitTarget, this, std::placeholders::_1));
	weapon->setMissCallback(std::bind(&FPSciApp::missEvent, this));

	// Load models and set the reticle
	loadModels();
	setReticle(reticleConfig.index);

	// Load fonts and images
	outputFont = GFont::fromFile(System::findDataFile("arial.fnt"));
	hudTextures.set("scoreBannerBackdrop", Texture::fromFile(System::findDataFile("gui/scoreBannerBackdrop.png")));

	// Setup the GUI
	showRenderingStats = false;
	makeGUI();

	updateMouseSensitivity();													  // Update (apply) mouse sensitivity
	const Array<String> sessions = m_userSettingsWindow->updateSessionDropDown(); // Update the session drop down to remove already completed sessions
	updateSession(sessions[0], true);											  // Update session to create results file/start collection

	// Setup the connection to the server if this experiment is networked
	if (experimentConfig.isNetworked)
	{
		if (enet_initialize() != 0) {
			// print an error and terminate if Enet does not initialize successfully
			throw std::runtime_error("Could not initialize ENet networking");
			
		}
		
		m_localHost = enet_host_create(NULL, 1, 2, 0, 0); // create a host on an arbitrary port which we use to contact the server
		if (m_localHost == NULL)
		{
			throw std::runtime_error("Could not create a local host for the server to connect to");
		}
		enet_address_set_host(&m_reliableServerAddress, experimentConfig.serverAddress.c_str());
		m_reliableServerAddress.port = experimentConfig.serverPort;
		m_serverPeer = enet_host_connect(m_localHost, &m_reliableServerAddress, 2, 0);		//setup a peer with the server
		if (m_serverPeer == NULL)
		{
			throw std::runtime_error("Could not create a connection to the server");
		}
		logPrintf("created a peer with the server at %s:%d (the connection may not have been accepeted by the server)", experimentConfig.serverAddress, experimentConfig.serverPort);

		// create the socket for communicating quickly and unreliably
		enet_address_set_host(&m_unreliableServerAddress, experimentConfig.serverAddress.c_str());
		m_unreliableServerAddress.port = experimentConfig.serverPort + 1;
		m_unreliableSocket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
		enet_socket_set_option(m_unreliableSocket, ENET_SOCKOPT_NONBLOCK, 1); //Set socket to non-blocking

		// initialize variables to be reset by handshakes
		m_enetConnected = false;
		m_socketConnected = false;
	}
	sessConfig->isNetworked = &experimentConfig.isNetworked;
}

void FPSciApp::toggleUserSettingsMenu() {
	if (!m_userSettingsWindow->visible())
	{
		openUserSettingsWindow();
	}
	else
	{
		closeUserSettingsWindow();
	}
	// Make sure any change to sensitivity are applied
	updateMouseSensitivity();
}

/** Handle the user settings window visibility */
void FPSciApp::openUserSettingsWindow() {
	// set focus so buttons properly highlight
	moveToCenter(m_userSettingsWindow);
	m_userSettingsWindow->setVisible(true);
	if (!dialog)
	{													 // Don't allow the user menu to hide the mouse when a dialog box is open
		setMouseInputMode(MouseInputMode::MOUSE_CURSOR); // Set mouse mode to cursor to allow pointer-based interaction
	}
	m_widgetManager->setFocusedWidget(m_userSettingsWindow);
}

/** Handle the user settings window visibility */
void FPSciApp::closeUserSettingsWindow() {
	if (sessConfig->menu.allowUserSettingsSave)
	{						  // If the user could have saved their settings
		saveUserConfig(true); // Save the user config (if it has changed) whenever this window is closed
	}
	if (!dialog)
	{												  // Don't allow the user menu to hide the mouse when a dialog box is open
		setMouseInputMode(MouseInputMode::MOUSE_FPM); // Set mouse mode to FPM to allow steering the view again
	}
	m_userSettingsWindow->setVisible(false);
}

void FPSciApp::saveUserConfig(bool onDiff) {
	// Check for save on diff, without mismatch
	if (onDiff && m_lastSavedUser == *currentUser())
		return;
	if (notNull(sess->logger))
	{
		sess->logger->logUserConfig(*currentUser(), sessConfig->id, sessConfig->player.turnScale);
	}
	userTable.save(startupConfig.experimentList[experimentIdx].userConfigFilename, startupConfig.jsonAnyOutput);
	m_lastSavedUser = *currentUser(); // Copy over this user
	logPrintf("User table saved.\n"); // Print message to log
}

void FPSciApp::saveUserStatus(void) {
	userStatusTable.save(startupConfig.experimentList[experimentIdx].userStatusFilename, startupConfig.jsonAnyOutput);
	logPrintf("User status saved.\n");
}

/** Update the mouse mode/sensitivity */
void FPSciApp::updateMouseSensitivity() {
	const shared_ptr<UserConfig> user = currentUser();
	// Converting from mouseDPI (dots/in) and sensitivity (cm/turn) into rad/dot which explains cm->in (2.54) and turn->rad (2*PI) factors
	// rad/dot = rad/cm * cm/dot = 2PI / (cm/turn) * 2.54 / (dots/in) = (2.54 * 2PI)/ (DPI * cm/360)
	const double cmp360 = 36.0 / user->mouseDegPerMm;
	const double radiansPerDot = 2.0 * pi() * 2.54 / (cmp360 * user->mouseDPI);
	const shared_ptr<FirstPersonManipulator>& fpm = dynamic_pointer_cast<FirstPersonManipulator>(cameraManipulator());

	// Control player motion using the experiment config parameter
	shared_ptr<PlayerEntity> player = scene()->typedEntity<PlayerEntity>("player");
	if (notNull(player))
	{
		player->m_cameraRadiansPerMouseDot = (float)radiansPerDot;
		player->turnScale = currentTurnScale();
	}

	m_userSettingsWindow->updateCmp360();
}

void FPSciApp::setMouseInputMode(MouseInputMode mode) {
	const shared_ptr<FirstPersonManipulator>& fpm = dynamic_pointer_cast<FirstPersonManipulator>(cameraManipulator());
	switch (mode)
	{
	case MouseInputMode::MOUSE_CURSOR:
		fpm->setMouseMode(FirstPersonManipulator::MOUSE_DIRECT_RIGHT_BUTTON); // Display cursor in right button mode (only holding right mouse rotates view)
		break;
	case MouseInputMode::MOUSE_DISABLED: // Disabled case uses direct mode, but ignores view changes
	case MouseInputMode::MOUSE_FPM:		 // FPM is "direct mode" wherein view is steered by mouse
		fpm->setMouseMode(FirstPersonManipulator::MOUSE_DIRECT);
		break;
	}
	m_mouseInputMode = mode;
}

void FPSciApp::loadConfigs(const ConfigFiles& configs) {
	// Load experiment setting from file
	experimentConfig = ExperimentConfig::load(configs.experimentConfigFilename, startupConfig.jsonAnyOutput);
	experimentConfig.printToLog();
	experimentConfig.validate(true);

	// Get hash for experimentconfig.Any file
	const size_t hash = HashTrait<String>::hashCode(experimentConfig.toAny().unparse()); // Hash the serialized Any (don't consider formatting)
	m_expConfigHash = format("%x", hash);												 // Store the hash as a hex string
	logPrintf("Experiment hash: %s\r\n", m_expConfigHash);								 // Write to log

	Array<String> sessionIds;
	experimentConfig.getSessionIds(sessionIds);

	// Load per user settings from file
	userTable = UserTable::load(configs.userConfigFilename, startupConfig.jsonAnyOutput);
	userTable.printToLog();

	// Load per experiment user settings from file and make sure they are valid
	userStatusTable = UserStatusTable::load(configs.userStatusFilename, startupConfig.jsonAnyOutput);
	userStatusTable.printToLog();
	userStatusTable.validate(sessionIds, userTable.getIds());

	// Get info about the system
	SystemInfo info = SystemInfo::get();
	info.printToLog(); // Print system info to log.txt

	// Get system configuration
	systemConfig = SystemConfig::load(configs.systemConfigFilename, startupConfig.jsonAnyOutput);
	systemConfig.printToLog(); // Print the latency logger config to log.txt

	// Load the key binds
	keyMap = KeyMapping::load(configs.keymapConfigFilename, startupConfig.jsonAnyOutput);
	userInput->setKeyMapping(&keyMap.uiMap);
}

void FPSciApp::loadModels() {
	if ((experimentConfig.weapon.renderModel || startupConfig.developerMode) && !experimentConfig.weapon.modelSpec.filename.empty())
	{
		// Load the model if we (might) need it
		weapon->loadModels();
	}

	// Add all the unqiue targets to this list
	Table<String, Any> targetsToBuild;
	Table<String, String> explosionsToBuild;
	Table<String, float> explosionScales;
	for (TargetConfig target : experimentConfig.targets)
	{
		targetsToBuild.set(target.id, target.modelSpec);
		explosionsToBuild.set(target.id, target.destroyDecal);
		explosionScales.set(target.id, target.destroyDecalScale);
	}

	// Append reference target model(s)
	Any& defaultRefTarget = experimentConfig.targetView.refTargetModelSpec;
	for (SessionConfig& sess : experimentConfig.sessions)
	{
		if (sess.targetView.refTargetModelSpec != defaultRefTarget)
		{
			// This is a custom reference target model
			String id = sess.id + "_reference";
			targetsToBuild.set(id, sess.targetView.refTargetModelSpec);
			explosionsToBuild.set(id, "explosion_01.png");
			explosionScales.set(id, 1.0);
		}
	}
	// Add default reference
	targetsToBuild.set("reference", defaultRefTarget);
	explosionsToBuild.set("reference", "explosion_01.png");
	explosionScales.set("reference", 1.0);

	// Scale the models into the m_targetModel table
	for (String id : targetsToBuild.getKeys())
	{
		// Get the any specs
		Any tSpec = targetsToBuild.get(id);
		Any explosionSpec = Any::parse(format(
			"ArticulatedModel::Specification {\
				filename = \"ifs/square.ifs\";\
				preprocess = {\
					transformGeometry(all(), Matrix4::scale(0.1, 0.1, 0.1));\
					setMaterial(all(), UniversalMaterial::Specification{\
						lambertian = Texture::Specification {\
							filename = \"%s\";\
							encoding = Color3(1, 1, 1);\
						};\
					});\
				};\
			}",
			explosionsToBuild.get(id).c_str()));

		// Get the bounding box to scale to size rather than arbitrary factor
		shared_ptr<ArticulatedModel> size_model = ArticulatedModel::create(ArticulatedModel::Specification(tSpec));
		AABox bbox;
		size_model->getBoundingBox(bbox);
		Vector3 extent = bbox.extent();
		logPrintf("%20s bounding box: [%2.2f, %2.2f, %2.2f]\n", id.c_str(), extent[0], extent[1], extent[2]);
		const float default_scale = 1.0f / extent[0]; // Setup scale so that default model is 1m across

		// Create the target/explosion models for this target
		Array<shared_ptr<ArticulatedModel>> tModels, expModels;
		for (int i = 0; i <= TARGET_MODEL_SCALE_COUNT; ++i)
		{
			const float scale = pow(1.0f + TARGET_MODEL_ARRAY_SCALING, float(i) - TARGET_MODEL_ARRAY_OFFSET);
			tSpec.set("scale", scale * default_scale);
			explosionSpec.set("scale", (20.0 * scale * explosionScales.get(id)));
			tModels.push(ArticulatedModel::create(tSpec));
			expModels.push(ArticulatedModel::create(explosionSpec));
		}
		targetModels.set(id, tModels);
		m_explosionModels.set(id, expModels);

		// Create a series of colored materials to choose from for target health
		shared_ptr<TargetConfig> tconfig = experimentConfig.getTargetConfigById(id);
		materials.remove(id);
		materials.set(id, makeMaterials(tconfig));
	}
}

Array<shared_ptr<UniversalMaterial>> FPSciApp::makeMaterials(shared_ptr<TargetConfig> tconfig) {
	Array<shared_ptr<UniversalMaterial>> targetMaterials;
	for (int i = 0; i < matTableSize; i++)
	{
		float complete = (float)i / (matTableSize - 1);

		Color4 color;
		if (notNull(tconfig) && tconfig->colors.length() > 0)
		{
			color = lerpColor(tconfig->colors, complete);
		}
		else
		{
			color = lerpColor(sessConfig->targetView.healthColors, complete);
		}

		Color4 gloss;
		if (notNull(tconfig) && tconfig->hasGloss)
		{
			gloss = tconfig->gloss;
		}
		else
		{
			gloss = sessConfig->targetView.gloss;
		}

		Color4 emissive;
		if (notNull(tconfig) && tconfig->emissive.length() > 0)
		{
			emissive = lerpColor(tconfig->emissive, complete);
		}
		else if (sessConfig->targetView.emissive.length() > 0)
		{
			emissive = lerpColor(sessConfig->targetView.emissive, complete);
		}
		else
		{
			emissive = color * 0.7f; // Historical behavior fallback for unspecified case
		}

		UniversalMaterial::Specification materialSpecification;
		materialSpecification.setLambertian(Texture::Specification(color));
		materialSpecification.setEmissive(Texture::Specification(emissive));
		materialSpecification.setGlossy(Texture::Specification(gloss)); // Used to be Color4(0.4f, 0.2f, 0.1f, 0.8f)
		targetMaterials.append(UniversalMaterial::create(materialSpecification));
	}
	return targetMaterials;
}

Color4 FPSciApp::lerpColor(Array<Color4> colors, float a) {
	if (colors.length() == 0)
	{
		throw "Cannot interpolate from colors array with length 0!";
	}
	else if (colors.length() == 1 || a >= 1.0f)
	{
		// a >= 1.0f indicates we should use the first color in the array
		// since above 1.0 would imply negative a negative array index
		return colors[0];
	}
	else if (a <= 0.0f)
	{
		// Use only the last color in the array
		return colors[colors.length() - 1];
	}
	else
	{
		// For 2 or more colors, linearly interpolate between the N colors.
		// a comes in the range [0, 1] where
		//     0 means to use the last value in colors
		// and 1 means to use the first value in colors
		// This means that a = 0 maps to colors[colors.length() - 1]
		// and a = 1 maps to colors[0]
		// We need to flip the direction and scale up to the number of elements in the array
		// a will indicate which two entries to interpolate between (left of .), and how much of each to use (right of .)
		float interp = (1.0f - a) * (colors.length() - 1);
		int idx = int(floor(interp));
		interp = interp - float(idx);
		Color4 output = colors[idx] * (1.0f - interp) + colors[idx + 1] * interp;
		return output;
	}
}

void FPSciApp::updateControls(bool firstSession) {
	// Update the user settings window
	updateUserMenu = true;
	if (!firstSession)
		m_showUserMenu = sessConfig->menu.showMenuBetweenSessions;

	// Update the waypoint manager
	if (notNull(waypointManager))
	{
		waypointManager->updateControls();
	}

	// Update the player controls
	bool visible = false;
	Rect2D rect;
	if (notNull(m_playerControls))
	{
		visible = m_playerControls->visible();
		rect = m_playerControls->rect();
		removeWidget(m_playerControls);
	}
	m_playerControls = PlayerControls::create(*sessConfig, std::bind(&FPSciApp::exportScene, this), theme);
	m_playerControls->setVisible(visible);
	if (!rect.isEmpty())
		m_playerControls->setRect(rect);
	addWidget(m_playerControls);

	// Update the render controls
	visible = false;
	rect = Rect2D();
	if (notNull(m_renderControls))
	{
		visible = m_renderControls->visible();
		rect = m_renderControls->rect();
		removeWidget(m_renderControls);
	}
	m_renderControls = RenderControls::create(this, *sessConfig, renderFPS, numReticles, sceneBrightness, theme, MAX_HISTORY_TIMING_FRAMES);
	m_renderControls->setVisible(visible);
	if (!rect.isEmpty())
		m_renderControls->setRect(rect);
	addWidget(m_renderControls);

	// Update the weapon controls
	visible = false;
	rect = Rect2D();
	if (notNull(m_weaponControls))
	{
		visible = m_weaponControls->visible();
		rect = m_weaponControls->rect();
		removeWidget(m_weaponControls);
	}
	m_weaponControls = WeaponControls::create(sessConfig->weapon, theme);
	m_weaponControls->setVisible(visible);
	if (!rect.isEmpty())
		m_weaponControls->setRect(rect);
	addWidget(m_weaponControls);
}

void FPSciApp::makeGUI() {

	theme = GuiTheme::fromFile(System::findDataFile("osx-10.7.gtm"));
	debugWindow->setVisible(startupConfig.developerMode);

	if (startupConfig.developerMode)
	{
		developerWindow->cameraControlWindow->setVisible(startupConfig.developerMode);
		developerWindow->videoRecordDialog->setEnabled(true);
		developerWindow->videoRecordDialog->setCaptureGui(true);

		// Update the scene editor (for new PhysicsScene pointer, initially loaded in GApp)
		removeWidget(developerWindow->sceneEditorWindow);
		developerWindow->sceneEditorWindow = SceneEditorWindow::create(this, scene(), theme);
		developerWindow->sceneEditorWindow->moveTo(developerWindow->cameraControlWindow->rect().x0y1() + Vector2(0, 15));
		developerWindow->sceneEditorWindow->setVisible(startupConfig.developerMode);
	}

	// Open sub-window buttons here (menu-style)
	debugPane->removeAllChildren();
	debugPane->beginRow();
	{
		debugPane->addButton("Render Controls [1]", this, &FPSciApp::showRenderControls);
		debugPane->addButton("Player Controls [2]", this, &FPSciApp::showPlayerControls);
		debugPane->addButton("Weapon Controls [3]", this, &FPSciApp::showWeaponControls);
		if (notNull(waypointManager))
			debugPane->addButton("Waypoint Manager [4]", waypointManager, &WaypointManager::showWaypointWindow);
	}
	debugPane->endRow();

	// Create the user settings window
	if (notNull(m_userSettingsWindow))
	{
		removeWidget(m_userSettingsWindow);
	}
	m_userSettingsWindow = UserMenu::create(this, userTable, userStatusTable, sessConfig->menu, theme, Rect2D::xywh(0.0f, 0.0f, 10.0f, 10.0f));
	addWidget(m_userSettingsWindow);
	openUserSettingsWindow();

	// Setup the debug window
	debugWindow->pack();
	debugWindow->setRect(Rect2D::xywh(0, 0, (float)window()->renderDevice()->viewport().width(), debugWindow->rect().height()));
	m_debugMenuHeight = startupConfig.developerMode ? debugWindow->rect().height() : 0.0f;

	// Add the control panes here
	updateControls();
	m_showUserMenu = experimentConfig.menu.showMenuOnStartup;
}

void FPSciApp::exportScene() {
	CFrame frame = scene()->typedEntity<PlayerEntity>("player")->frame();
	logPrintf("Player position is: [%f, %f, %f]\n", frame.translation.x, frame.translation.y, frame.translation.z);
	String filename = Scene::sceneNameToFilename(sessConfig->scene.name);
	scene()->toAny().save(filename); // Save this w/o JSON format (breaks scene.Any file)
}

void FPSciApp::showPlayerControls() {
	m_playerControls->setVisible(true);
}

void FPSciApp::showRenderControls() {
	m_renderControls->setVisible(true);
}

void FPSciApp::showWeaponControls() {
	m_weaponControls->setVisible(true);
}

void FPSciApp::presentQuestion(Question question) {
	if (notNull(dialog))
		removeWidget(dialog);				  // Remove the current dialog widget (if valid)
	currentQuestion = question;				  // Store this for processing key-bound presses
	Array<String> options = question.options; // Make a copy of the options (to add key binds if necessary)
	if (question.randomOrder)
		options.randomize();
	const Rect2D windowRect = window()->clientRect();
	const Point2 size = question.fullscreen ? Point2(windowRect.width(), windowRect.height()) : Point2(400, 200);
	switch (question.type)
	{
	case Question::Type::MultipleChoice:
		if (question.optionKeys.length() > 0)
		{ // Add key-bound option to the dialog
			for (int i = 0; i < options.length(); i++)
			{
				// Find the correct index for this option (order might be randomized)
				int keyIdx;
				for (keyIdx = 0; keyIdx < question.options.length(); keyIdx++)
				{
					if (options[i] == question.options[keyIdx])
						break;
				}
				options[i] += format(" (%s)", question.optionKeys[keyIdx].toString());
			}
		}
		dialog = SelectionDialog::create(question.prompt, options, theme, question.title, question.showCursor, question.optionsPerRow, size, !question.fullscreen,
			question.promptFontSize, question.optionFontSize, question.buttonFontSize);
		break;
	case Question::Type::Entry:
		dialog = TextEntryDialog::create(question.prompt, theme, question.title, false, size, !question.fullscreen, question.promptFontSize, question.buttonFontSize);
		break;
	case Question::Type::Rating:
		if (question.optionKeys.length() > 0)
		{ // Add key-bound option to the dialog
			for (int i = 0; i < options.length(); i++)
			{
				// Find the correct index for this option (order might be randomized)
				int keyIdx;
				for (keyIdx = 0; keyIdx < question.options.length(); keyIdx++)
				{
					if (options[i] == question.options[keyIdx])
						break;
				}
				options[i] += format(" (%s)", question.optionKeys[keyIdx].toString());
			}
		}
		dialog = RatingDialog::create(question.prompt, options, theme, question.title, question.showCursor, size, !question.fullscreen,
			question.promptFontSize, question.optionFontSize, question.buttonFontSize);
		break;
	case Question::Type::DropDown:
		dialog = DropDownDialog::create(question.prompt, options, theme, question.title, size, !question.fullscreen, question.promptFontSize, question.buttonFontSize);
		break;
	default:
		throw "Unknown question type!";
		break;
	}

	moveToCenter(dialog);
	addWidget(dialog);
	setMouseInputMode(question.showCursor ? MouseInputMode::MOUSE_CURSOR : MouseInputMode::MOUSE_DISABLED);
}

void FPSciApp::markSessComplete(String sessId) {
	if (notNull(m_pyLogger))
	{
		m_pyLogger->mergeLogToDb();
	}
	// Add the session id to completed session array and save the user status table
	userStatusTable.addCompletedSession(userStatusTable.currentUser, sessId);
	logPrintf("Marked session: %s complete for user %s.\n", sessId, userStatusTable.currentUser);

	// Update the session drop-down to remove this session
	m_userSettingsWindow->updateSessionDropDown();
}

void FPSciApp::updateParameters(int frameDelay, float frameRate) {
	// Apply frame lag
	displayLagFrames = frameDelay;
	lastSetFrameRate = frameRate;
	// Set a maximum *finite* frame rate
	float dt = 0;
	if (frameRate > 0)
		dt = 1.0f / frameRate;
	else
		dt = 1.0f / float(window()->settings().refreshRate);
	// Update the desired realtime framerate, leaving the simulation timestep as it were (likely REAL_TIME)
	setFrameDuration(dt, simStepDuration());
}

void FPSciApp::initPlayer(bool setSpawnPosition) {
	shared_ptr<PhysicsScene> pscene = typedScene<PhysicsScene>();
	shared_ptr<PlayerEntity> player = scene()->typedEntity<PlayerEntity>("player"); // Get player from the scene

	// Update the player camera
	const String pcamName = sessConfig->scene.playerCamera;
	playerCamera = pcamName.empty() ? scene()->defaultCamera() : scene()->typedEntity<Camera>(pcamName);
	alwaysAssertM(notNull(playerCamera), format("Scene %s does not contain a camera named \"%s\"!", sessConfig->scene.name, pcamName));
	setActiveCamera(playerCamera);

	// Set gravity and camera field of view
	Vector3 grav = experimentConfig.player.gravity;
	float FoV = experimentConfig.render.hFoV;
	if (notNull(sessConfig))
	{
		grav = sessConfig->player.gravity;
		FoV = sessConfig->render.hFoV;
	}
	pscene->setGravity(grav);

	String respawnHeightSource;
	playerCamera->setFieldOfView(FoV * units::degrees(), FOVDirection::HORIZONTAL);
	if (!m_sceneHasPlayerEntity)
	{ // Scene doesn't have player entity, copy the player entity frame from the camera
		respawnHeightSource = format("\"%s\" camera in scene.Any file", playerCamera->name());
		player->setFrame(m_initialCameraFrames[playerCamera->name()]);
		setSpawnPosition = true; // Set the player spawn position from the camera
	}
	else
	{
		respawnHeightSource = "PlayerEntity in scene.Any file";
	}
	playerCamera->setFrame(player->getCameraFrame());

	// For now make the player invisible (prevent issues w/ seeing model from inside)
	player->setVisible(false);

	// Set the reset height
	String resetHeightSource = "scene configuration \"resetHeight\" parameter";
	float resetHeight = sessConfig->scene.resetHeight;
	if (isnan(resetHeight))
	{
		resetHeightSource = "scene.Any Physics \"minHeight\" field";
		resetHeight = pscene->resetHeight();
		if (isnan(resetHeight))
		{
			resetHeightSource = "default value";
			resetHeight = -1e6;
		}
	}
	player->setRespawnHeight(resetHeight);

	// Update the respawn heading
	if (isnan(sessConfig->scene.spawnHeadingDeg))
	{
		if (setSpawnPosition)
		{ // This is the first spawn in the scene
			// No SceneConfig spawn heading specified, get heading from scene.Any player entity heading field
			Point3 view_dir = playerCamera->frame().lookVector();
			float spawnHeadingDeg = atan2(view_dir.x, -view_dir.z) * 180 / pif();
			player->setRespawnHeadingDegrees(spawnHeadingDeg);
		}
	}
	else
	{ // Respawn heading specified by the scene config
		player->setRespawnHeadingDegrees(sessConfig->scene.spawnHeadingDeg);
	}

	// Set player respawn location
	float respawnPosHeight = player->respawnPosHeight(); // Report the respawn position height
	if (sessConfig->scene.spawnPosition.isNaN())
	{
		if (setSpawnPosition)
		{ // This is the first spawn, copy the respawn position from the scene
			player->setRespawnPosition(player->frame().translation);
			respawnPosHeight = player->frame().translation.y;
		}
	}
	else
	{ // Respawn position set by scene config
		player->setRespawnPosition(sessConfig->scene.spawnPosition);
		respawnPosHeight = sessConfig->scene.spawnPosition.y;
		respawnHeightSource = "scene configuration \"spawnPosition\" parameter";
	}

	if (respawnPosHeight < resetHeight)
	{
		throw format("Invalid respawn height (%f) from %s (< %f specified from %s)!", respawnPosHeight, respawnHeightSource.c_str(), resetHeight, resetHeightSource.c_str());
	}

	// Set player values from session config
	player->moveRate = &sessConfig->player.moveRate;
	player->sprintMultiplier = &sessConfig->player.sprintMultiplier;
	player->headBobEnabled = &sessConfig->player.headBobEnabled;
	player->headBobAmplitude = &sessConfig->player.headBobAmplitude;
	player->headBobFrequency = &sessConfig->player.headBobFrequency;
	player->respawnPos = &sessConfig->player.respawnPos;
	player->respawnToPos = &sessConfig->player.respawnToPos;
	player->respawnHeading = &sessConfig->player.respawnHeading;
	player->accelerationEnabled = &sessConfig->player.accelerationEnabled;
	player->movementAcceleration = &sessConfig->player.movementAcceleration;
	player->movementDeceleration = &sessConfig->player.movementDeceleration;
	player->moveScale = &sessConfig->player.moveScale;
	player->axisLock = &sessConfig->player.axisLock;
	player->jumpVelocity = &sessConfig->player.jumpVelocity;
	player->jumpInterval = &sessConfig->player.jumpInterval;
	player->jumpTouch = &sessConfig->player.jumpTouch;
	player->height = &sessConfig->player.height;
	player->crouchHeight = &sessConfig->player.crouchHeight;
	player->movementRestrictionX = &sessConfig->player.movementRestrictionX;
	player->movementRestrictionZ = &sessConfig->player.movementRestrictionZ;
	player->restrictedMovementEnabled = &sessConfig->player.restrictedMovementEnabled;
	player->restrictionBoxAngle = &sessConfig->player.restrictionBoxAngle;
	player->counterStrafing = &sessConfig->player.counterStrafing;
	player->propagatePlayerConfigsToAll = &sessConfig->player.propagatePlayerConfigsToAll;
	player->propagatePlayerConfigsToSelectedClient = &sessConfig->player.propagatePlayerConfigsToSelectedClient;
	player->readFromFile = &sessConfig->player.readFromFile;
	player->selectedClientIdx = &sessConfig->player.selectedClientIdx;
	player->clientPlayerConfigs = &sessConfig->player.clientPlayerConfigs;
	player->cornerPosition = &sessConfig->player.cornerPosition;
	player->defenderRandomDisplacementAngle = &sessConfig->player.defenderRandomDisplacementAngle;
	player->playerType = &sessConfig->player.playerType;
	// Respawn player
	player->respawn();
	updateMouseSensitivity();

	// Set initial heading for session
	sess->initialHeadingRadians = player->heading();
}

void FPSciApp::updateSession(const String& id, bool forceReload) {
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
	const String logFileBasename = sessConfig->logger.logToSingleDb ? experimentConfig.description + "_" + userStatusTable.currentUser + "_" + m_expConfigHash + "_" + String(FPSciLogger::genFileTimestamp()) : id + "_" + userStatusTable.currentUser + "_" + String(FPSciLogger::genFileTimestamp());
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
		/* Set the latency to be what the new latency */
		NetworkUtils::setAddressLatency(m_reliableServerAddress, sessConfig->networkLatency);
		NetworkUtils::setAddressLatency(m_unreliableServerAddress, sessConfig->networkLatency);
	}
}

void FPSciApp::quitRequest() {
	// End session logging
	if (notNull(sess))
	{
		sess->endLogging();
	}
	// Merge Python log into session log (if logging)
	if (notNull(m_pyLogger))
	{
		m_pyLogger->mergeLogToDb(true);
	}
	if (experimentConfig.isNetworked && m_serverPeer != nullptr) { // disconnect from the server if we're running in Network mode
		enet_peer_disconnect(m_serverPeer, 0);
	}
	setExitCode(0);
}

void FPSciApp::onAfterLoadScene(const Any& any, const String& sceneName) {

	// Make sure the scene has a "player" entity
	shared_ptr<PlayerEntity> player = scene()->typedEntity<PlayerEntity>("player");
	m_sceneHasPlayerEntity = notNull(player);
	if (!m_sceneHasPlayerEntity)
	{ // Add a player if one isn't present in the scene
		logPrintf("WARNING: Didn't find a \"player\" specified in \"%s\"! Adding one at the origin.", sceneName);
		shared_ptr<Entity> newPlayer = PlayerEntity::create("player", scene().get(), CFrame(), nullptr);
		scene()->insert(newPlayer);
	}

	// Build lookup of initial camera positions here
	Array<shared_ptr<Camera>> camArray;
	scene()->getTypedEntityArray<Camera>(camArray);
	for (shared_ptr<Camera> cam : camArray)
	{
		m_initialCameraFrames.set(cam->name(), cam->frame());
	}

	initPlayer(true); // Initialize the player (first time for this scene)

	if (weapon)
	{
		weapon->setScene(scene());
		weapon->setCamera(playerCamera);
	}
}

void FPSciApp::onAI() {
	GApp::onAI();
	// Add non-simulation game logic and AI code here
}

void FPSciApp::onNetwork() {
	GApp::onNetwork();


	//if (experimentConfig.isNetworked && sess->currentState == PresentationState::networkedSessionRoundStart) {
		m_networkFrameNum++;
	//}


	if (!m_socketConnected) {
		shared_ptr<HandshakePacket> handshake = GenericPacket::createUnreliable<HandshakePacket>(&m_unreliableSocket, &m_unreliableServerAddress);
		NetworkUtils::send(handshake);
		//handshake->send();
	}

	// wait to send updates until we're sure we're connected
	if (m_socketConnected && m_enetConnected) {
		// Get and serialize the players frame
		shared_ptr<BatchEntityUpdatePacket> updatePacket = GenericPacket::createUnreliable<BatchEntityUpdatePacket>(&m_unreliableSocket, &m_unreliableServerAddress);
		Array<BatchEntityUpdatePacket::EntityUpdate> updates;
		updates.append(BatchEntityUpdatePacket::EntityUpdate(scene()->entity("player")->frame(), m_playerGUID.toString16()));
		updatePacket->populate(m_networkFrameNum, updates, BatchEntityUpdatePacket::NetworkUpdateType::REPLACE_FRAME);
		NetworkUtils::send(updatePacket);
		//updatePacket->send();
	}

	/* Recevie and handle any packets */
	shared_ptr<GenericPacket> inPacket = NetworkUtils::receivePacket(m_localHost, &m_unreliableSocket);
	while (inPacket != nullptr) {
		ENetAddress srcAddr = inPacket->srcAddr();
		char ip[16];
		enet_address_get_host_ip(&srcAddr, ip, 16);
		/* Handle Unreliable packets here */
		if (!inPacket->isReliable()) {
			switch (inPacket->type()) {
			case BATCH_ENTITY_UPDATE: {
				/* Take a set of entity updates from the server and apply them to local entities */
				BatchEntityUpdatePacket* typedPacket = static_cast<BatchEntityUpdatePacket*>(inPacket.get());
				//TODO: refactor this out into some other place, maybe NetworkUtils??
				for (BatchEntityUpdatePacket::EntityUpdate e : typedPacket->m_updates) {
					if (e.name != m_playerGUID.toString16()) { // Don't listen to updates for this client
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
				}
				break;
			}
			case HANDSHAKE_REPLY: {
				m_socketConnected = true;
				debugPrintf("Received HANDSHAKE_REPLY from server\n");
				break;
			}
			case PLAYER_INTERACT: {
				PlayerInteractPacket* typedPacket = static_cast<PlayerInteractPacket*> (inPacket.get());
				if (typedPacket->m_actorID != m_playerGUID) {
					// Only log actions that happen on another machine
					const shared_ptr<NetworkedEntity> clientEntity = scene()->typedEntity<NetworkedEntity>(typedPacket->m_actorID.toString16());
					RemotePlayerAction rpa = RemotePlayerAction();
					rpa.time = sess->logger->getFileTime();
					rpa.viewDirection = clientEntity->getLookAzEl();
					rpa.position = clientEntity->frame().translation;
					rpa.state = sess->currentState;
					rpa.action = (PlayerActionType)typedPacket->m_remoteAction;
					rpa.actorID = typedPacket->m_actorID.toString16();
					sess->logger->logRemotePlayerAction(rpa);
				}
				break;
			}
			default:
				debugPrintf("WARNING: Unhandled packet received on unreliable channel of type %d\n", inPacket->type());
			}
		}
		else {
			switch (inPacket->type()) {
				/* Handle Reliable packets here */
			case RELIABLE_CONNECT: {
				ENetAddress localAddress;
				enet_socket_get_address(m_unreliableSocket, &localAddress);
				char ipStr[16];
				enet_address_get_host_ip(&localAddress, ipStr, 16);
				debugPrintf("Registering client...\n");
				debugPrintf("\tPort: %i\n", localAddress.port);
				debugPrintf("\tHost: %s\n", ipStr);
				shared_ptr<RegisterClientPacket> registrationPacket = GenericPacket::createReliable<RegisterClientPacket>(m_serverPeer);
				registrationPacket->populate(m_serverPeer, m_playerGUID, localAddress.port);
				NetworkUtils::send(registrationPacket);
				//registrationPacket->send();
				break;
			}
			case CREATE_ENTITY: {
				CreateEntityPacket* typedPacket = static_cast<CreateEntityPacket*> (inPacket.get());
				if (typedPacket->m_guid != m_playerGUID) {
					debugPrintf("Created entity with ID %s\n", typedPacket->m_guid.toString16());

					Any modelSpec = PARSE_ANY(ArticulatedModel::Specification{			///< Basic model spec for target
						filename = "model/target/pointingplayer.obj";
						preprocess = {
						transformGeometry(all(), Matrix4::yawDegrees(120));
						//transformGeometry(all(), Matrix4::translation(0, -1, 0));
						};
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

					const shared_ptr<NetworkedEntity>& target = NetworkedEntity::create(typedPacket->m_guid.toString16(), &(*scene()), model, CFrame());
					//target->setFrame(position);
					target->setWorldSpace(true);
					//target->setHitSound(config->hitSound, m_app->soundTable, config->hitSoundVol);
					//target->setDestoyedSound(config->destroyedSound, m_app->soundTable, config->destroyedSoundVol);
					target->setColor(G3D::Color3(20.0, 20.0, 200.0));

					(*scene()).insert(target);
					netSess->addHittableTarget(target);
				}
				break;
			}
			case CLIENT_REGISTRATION_REPLY: {
				RegistrationReplyPacket* typedPacket = static_cast<RegistrationReplyPacket*>(inPacket.get());
				debugPrintf("INFO: Received registration reply...\n");
				if (typedPacket->m_guid == m_playerGUID) {
					if (typedPacket->m_status == 0) {
						m_enetConnected = true;
						debugPrintf("INFO: Received registration from server\n");

						/* Set the amount of latency to add */
						NetworkUtils::setAddressLatency(m_unreliableServerAddress, sessConfig->networkLatency);
						NetworkUtils::setAddressLatency(typedPacket->srcAddr(), sessConfig->networkLatency);
					}
					else {
						debugPrintf("WARN: Server connection refused (%i)", typedPacket->m_status);
					}
				}
				break;
			}
			case MOVE_CLIENT: {
				MoveClientPacket* typedPacket = static_cast<MoveClientPacket*> (inPacket.get());
				shared_ptr<PlayerEntity> entity = scene()->typedEntity<PlayerEntity>("player");
				entity->setFrame(typedPacket->m_newPosition);
				break;
			}
			case DESTROY_ENTITY: {
				DestroyEntityPacket* typedPacket = static_cast<DestroyEntityPacket*>(inPacket.get());
				debugPrintf("Recieved destroy entity request for: %s\n", typedPacket->m_guid.toString16());
				shared_ptr<NetworkedEntity> entity = scene()->typedEntity<NetworkedEntity>(typedPacket->m_guid.toString16());
				scene()->remove(entity);
				break;
			}
			case SET_SPAWN_LOCATION: {
				SetSpawnPacket* typedPacket = static_cast<SetSpawnPacket*> (inPacket.get());
				debugPrintf("Recieved an updated spawn position\n");
				shared_ptr<PlayerEntity> player = scene()->typedEntity<PlayerEntity>("player");
				player->setRespawnPosition(typedPacket->m_spawnPositionTranslation);
				player->setRespawnHeadingDegrees(typedPacket->m_spawnHeading);
				break;
			}
			case RESPAWN_CLIENT: {
				debugPrintf("Recieved a request to respawn\n");
				scene()->typedEntity<PlayerEntity>("player")->respawn();
				//netSess->resetSession();
				break;
			}
			case START_NETWORKED_SESSION: {
				StartSessionPacket* typedPacket = static_cast<StartSessionPacket*> (inPacket.get());
				netSess->startRound();
				m_networkFrameNum = typedPacket->m_frameNumber; // Set the frame number to sync with the server
				debugPrintf("Recieved a request to start session.\n");
				break;
			}
			case SEND_PLAYER_CONFIG: {
				//TODO: Decide if we can just replace the the local player config and do that instead
				SendPlayerConfigPacket* typedPacket = static_cast<SendPlayerConfigPacket*> (inPacket.get());
				sessConfig->player.moveRate = typedPacket->m_playerConfig->moveRate;
				sessConfig->player.moveScale = typedPacket->m_playerConfig->moveScale;

				(sessConfig->player.axisLock)[0] = typedPacket->m_playerConfig->axisLock[0];
				(sessConfig->player.axisLock)[1] = typedPacket->m_playerConfig->axisLock[1];
				(sessConfig->player.axisLock)[2] = typedPacket->m_playerConfig->axisLock[2];

				sessConfig->player.accelerationEnabled = typedPacket->m_playerConfig->accelerationEnabled;
				sessConfig->player.movementAcceleration = typedPacket->m_playerConfig->movementAcceleration;
				sessConfig->player.movementDeceleration = typedPacket->m_playerConfig->movementDeceleration;

				sessConfig->player.sprintMultiplier = typedPacket->m_playerConfig->sprintMultiplier;

				sessConfig->player.jumpVelocity = typedPacket->m_playerConfig->jumpVelocity;
				sessConfig->player.jumpInterval = typedPacket->m_playerConfig->jumpInterval;
				sessConfig->player.jumpTouch = typedPacket->m_playerConfig->jumpTouch;

				sessConfig->player.height = typedPacket->m_playerConfig->height;
				sessConfig->player.crouchHeight = typedPacket->m_playerConfig->crouchHeight;

				sessConfig->player.headBobEnabled = typedPacket->m_playerConfig->headBobEnabled;
				sessConfig->player.headBobAmplitude = typedPacket->m_playerConfig->headBobAmplitude;
				sessConfig->player.headBobFrequency = typedPacket->m_playerConfig->headBobFrequency;

				sessConfig->player.respawnPos = typedPacket->m_playerConfig->respawnPos;
				sessConfig->player.respawnToPos = typedPacket->m_playerConfig->respawnToPos;
				sessConfig->player.respawnHeading = typedPacket->m_playerConfig->respawnHeading;

				sessConfig->player.movementRestrictionX = typedPacket->m_playerConfig->movementRestrictionX;
				sessConfig->player.movementRestrictionZ = typedPacket->m_playerConfig->movementRestrictionZ;
				sessConfig->player.restrictedMovementEnabled = typedPacket->m_playerConfig->restrictedMovementEnabled;
				sessConfig->player.restrictionBoxAngle = typedPacket->m_playerConfig->restrictionBoxAngle;

				sessConfig->player.counterStrafing = typedPacket->m_playerConfig->counterStrafing;

				sessConfig->player.playerType = typedPacket->m_playerConfig->playerType;

				sessConfig->networkedSessionProgress = typedPacket->m_networkedSessionProgress;

				sessConfig->player.clientLatency = typedPacket->m_playerConfig->clientLatency;

				sessConfig->player.defenderRandomDisplacementAngle = typedPacket->m_playerConfig->defenderRandomDisplacementAngle;
				sessConfig->player.cornerPosition = typedPacket->m_playerConfig->cornerPosition;


				//Set Latency
				NetworkUtils::setAddressLatency(m_unreliableServerAddress, sessConfig->player.clientLatency);
				NetworkUtils::setAddressLatency(typedPacket->srcAddr(), sessConfig->player.clientLatency);

				break;
			}
			case ADD_POINTS: {
				sessConfig->clientScore++;
				debugPrintf("Enemy Hit! Points Added!\n");
				scene()->typedEntity<PlayerEntity>("player")->respawn();
				break;
			}
			case RESET_CLIENT_ROUND: {
				netSess.get()->resetRound();
				scene()->typedEntity<PlayerEntity>("player")->respawn();
				sessConfig->clientScore = 0;
				break;
			}
			case CLIENT_FEEDBACK_START: {
				netSess.get()->feedbackStart();
				break;
			}
			case CLIENT_SESSION_END: {
				netSess->endSession();
				break;
			}
			default:
				debugPrintf("WARNING: unhandled packet received on reliable channel of type: %d\n", inPacket->type());
			}
		}
		inPacket = NetworkUtils::receivePacket(m_localHost, &m_unreliableSocket);
	}
}

void FPSciApp::onSimulation(RealTime rdt, SimTime sdt, SimTime idt) {
	// TODO: this should eventually probably use sdt instead of rdt
	RealTime currentRealTime;
	if (m_lastOnSimulationRealTime == 0)
	{
		m_lastOnSimulationRealTime = System::time();  // Grab the current system time if uninitialized
		currentRealTime = m_lastOnSimulationRealTime; // Set this equal to the current system time
	}
	else
	{
		currentRealTime = m_lastOnSimulationRealTime + rdt; // Increment the time by the current real time delta
	}

	bool stateCanFire = sess->currentState == PresentationState::trialTask || (experimentConfig.isNetworked) && !m_userSettingsWindow->visible();

	// These variables will be used to fire after the various weapon styles populate them below
	int numShots = 0;
	float damagePerShot = +weapon->damagePerShot();
	RealTime newLastFireTime = currentRealTime;

	if (shootButtonJustPressed && stateCanFire && !weapon->canFire(currentRealTime))
	{
		// Invalid click since the weapon isn't ready to fire
		sess->accumulatePlayerAction(PlayerActionType::FireCooldown);
	}
	else if (shootButtonJustPressed && !weapon->config()->autoFire && weapon->canFire(currentRealTime) && stateCanFire)
	{
		// Discrete weapon fires a single shot with normal damage at the current time
		numShots = 1;
		// These copy the above defaults, but are here for clarity
		damagePerShot = weapon->damagePerShot();
		newLastFireTime = currentRealTime;
	}
	else if (weapon->config()->autoFire && !weapon->config()->isContinuous() && !shootButtonUp && stateCanFire)
	{
		// Autofire weapon should create shots until currentRealTime with normal damage
		if (shootButtonJustPressed)
		{
			// If the button was just pressed, fire one bullet half way through
			weapon->setLastFireTime(m_lastOnSimulationRealTime + rdt * 0.5f);
			numShots = 1;
		}
		// Add on bullets until the frame time
		int newShots = weapon->numShotsUntil(currentRealTime);
		numShots += newShots;
		newLastFireTime = weapon->lastFireTime() + (float)(newShots)*weapon->config()->firePeriod;
		// This copies the above default, but are here for clarity
		damagePerShot = weapon->damagePerShot();
	}
	else if (weapon->config()->isContinuous() && (!shootButtonUp || shootButtonJustReleased) && stateCanFire)
	{
		// Continuous weapon should have been firing continuously, but since we do sampled simulation
		// this approximates continuous fire by releasing a single "megabullet"
		// with power that matches the elapsed time at the current
		numShots = 1;

		// If the button was just pressed, assume the duration should begin half way through
		if (shootButtonJustPressed)
		{
			weapon->setLastFireTime(m_lastOnSimulationRealTime + rdt * 0.5f);
		}
		// If the shoot button just released, assume the fire ended half way through
		newLastFireTime = shootButtonJustReleased ? m_lastOnSimulationRealTime + rdt * 0.5f : currentRealTime;
		RealTime fireDuration = weapon->fireDurationUntil(newLastFireTime);
		damagePerShot = (float)fireDuration * weapon->config()->damagePerSecond;
	}

	// Actually shoot here
	m_currentWeaponDamage = damagePerShot; // pass this to the callback where weapon damage is applied
	bool shotFired = false;
	for (int shotId = 0; shotId < numShots; shotId++)
	{
		Array<shared_ptr<Entity>> dontHit;
		dontHit.append(m_explosions);
		dontHit.append(sess->unhittableTargets());
		Model::HitInfo info;
		float hitDist = finf();
		int hitIdx = -1;

		shared_ptr<TargetEntity> target = weapon->fire(sess->hittableTargets(), hitIdx, hitDist, info, dontHit, false); // Fire the weapon
		if (isNull(target))																								// Miss case
		{
			// Play scene hit sound
			if (!weapon->config()->isContinuous() && notNull(m_sceneHitSound))
			{
				m_sceneHitSound->play(sessConfig->audio.sceneHitSoundVol);
			}
		}
		shotFired = true;
	}
	if (shotFired)
	{
		weapon->setLastFireTime(newLastFireTime);
	}
	weapon->playSound(shotFired, shootButtonUp);

	// TODO (or NOTTODO): The following can be cleared at the cost of one more level of inheritance.
	sess->onSimulation(rdt, sdt, idt);

	// These are all we need from GApp::onSimulation() for walk mode
	m_widgetManager->onSimulation(rdt, sdt, idt);
	if (scene())
	{
		scene()->onSimulation(sdt);
	}

	// make sure mouse sensitivity is set right
	if (m_userSettingsWindow->visible())
	{
		updateMouseSensitivity();
	}

	// Simulate the projectiles
	weapon->simulateProjectiles(sdt, sess->hittableTargets());

	// explosion animation
	for (int i = 0; i < m_explosions.size(); i++)
	{
		shared_ptr<VisibleEntity> explosion = m_explosions[i];
		m_explosionRemainingTimes[i] -= sdt;
		if (m_explosionRemainingTimes[i] <= 0)
		{
			scene()->remove(explosion);
			m_explosions.fastRemove(i);
			m_explosionRemainingTimes.fastRemove(i);
			i--;
		}
		else
		{
			// could update animation here...
		}
	}

	// Move the player
	const shared_ptr<PlayerEntity>& p = scene()->typedEntity<PlayerEntity>("player");
	playerCamera->setFrame(p->getCameraFrame());

	// Handle developer mode features here
	if (startupConfig.developerMode)
	{
		// If the debug camera is selected, update it's position from the FPM
		if (activeCamera() == m_debugCamera)
		{
			m_debugCamera->setFrame(m_cameraManipulator->frame());
		}

		// Handle frame rate/delay updates here
		if (sessConfig->render.frameRate != lastSetFrameRate || displayLagFrames != sessConfig->render.frameDelay)
		{
			updateParameters(sessConfig->render.frameDelay, sessConfig->render.frameRate);
		}

		if (notNull(waypointManager))
		{
			// Handle highlighting for selected target
			waypointManager->updateSelected();
			// Handle player motion recording here
			waypointManager->updatePlayerPosition(p->getCameraFrame().translation);
		}

		// Example GUI dynamic layout code.  Resize the debugWindow to fill
		// the screen horizontally.
		debugWindow->setRect(Rect2D::xywh(0.0f, 0.0f, (float)window()->width(), debugWindow->rect().height()));
	}

	// Check for completed session
	if (sess->moveOn)
	{
		// Get the next session for the current user
		updateSession(userStatusTable.getNextSession());
	}
	// Update time at which this simulation finished
	m_lastOnSimulationRealTime = m_lastOnSimulationRealTime + rdt;
	m_lastOnSimulationSimTime = m_lastOnSimulationSimTime + sdt;
	m_lastOnSimulationIdealSimTime = m_lastOnSimulationIdealSimTime + idt;

	// Clear button press state
	shootButtonJustPressed = false;
	shootButtonJustReleased = false;
}

bool FPSciApp::onEvent(const GEvent& event) {
	GKey ksym = event.key.keysym.sym;
	bool foundKey = false;

	// Handle developer mode key-bound shortcuts here...
	if (startupConfig.developerMode)
	{
		if (event.type == GEventType::KEY_DOWN)
		{
			// Window display toggle
			if (keyMap.map["toggleRenderWindow"].contains(ksym))
			{
				m_renderControls->setVisible(!m_renderControls->visible());
				foundKey = true;
			}
			else if (keyMap.map["togglePlayerWindow"].contains(ksym))
			{
				m_playerControls->setVisible(!m_playerControls->visible());
				foundKey = true;
			}
			else if (keyMap.map["toggleWeaponWindow"].contains(ksym))
			{
				m_weaponControls->setVisible(!m_weaponControls->visible());
				foundKey = true;
			}
			else if (keyMap.map["reloadConfigs"].contains(ksym))
			{
				loadConfigs(startupConfig.experimentList[experimentIdx]); // (Re)load the configs
				// Update session from the reloaded configs
				m_userSettingsWindow->updateSessionDropDown();
				updateSession(m_userSettingsWindow->selectedSession());
				// Do not set foundKey = true to allow shader reloading from GApp::onEvent()
			}
			// Waypoint editor only keys
			else if (notNull(waypointManager))
			{
				if (keyMap.map["toggleWaypointWindow"].contains(ksym))
				{
					waypointManager->toggleWaypointWindow();
					foundKey = true;
				}
				else if (keyMap.map["toggleRecording"].contains(ksym))
				{
					waypointManager->recordMotion = !waypointManager->recordMotion;
					foundKey = true;
				}
				// Waypoint movement controls
				else if (keyMap.map["dropWaypoint"].contains(ksym))
				{
					waypointManager->dropWaypoint();
					foundKey = true;
				}
				else if (keyMap.map["moveWaypointUp"].contains(ksym))
				{
					waypointManager->moveMask += Vector3(0.0f, 1.0f, 0.0f);
					foundKey = true;
				}
				else if (keyMap.map["moveWaypointDown"].contains(ksym))
				{
					waypointManager->moveMask += Vector3(0.0f, -1.0f, 0.0f);
					foundKey = true;
				}
				else if (keyMap.map["moveWaypointIn"].contains(ksym))
				{
					waypointManager->moveMask += Vector3(0.0f, 0.0f, 1.0f);
					foundKey = true;
				}
				else if (keyMap.map["moveWaypointOut"].contains(ksym))
				{
					waypointManager->moveMask += Vector3(0.0f, 0.0f, -1.0f);
					foundKey = true;
				}
				else if (keyMap.map["moveWaypointRight"].contains(ksym))
				{
					waypointManager->moveMask += Vector3(1.0f, 0.0f, 0.0f);
					foundKey = true;
				}
				else if (keyMap.map["moveWaypointLeft"].contains(ksym))
				{
					waypointManager->moveMask += Vector3(-1.0f, 0.0f, 0.0f);
					foundKey = true;
				}
			}
		}
		else if (event.type == GEventType::KEY_UP)
		{
			if (notNull(waypointManager))
			{
				if (keyMap.map["moveWaypointUp"].contains(ksym))
				{
					waypointManager->moveMask -= Vector3(0.0f, 1.0f, 0.0f);
					foundKey = true;
				}
				else if (keyMap.map["moveWaypointDown"].contains(ksym))
				{
					waypointManager->moveMask -= Vector3(0.0f, -1.0f, 0.0f);
					foundKey = true;
				}
				else if (keyMap.map["moveWaypointIn"].contains(ksym))
				{
					waypointManager->moveMask -= Vector3(0.0f, 0.0f, 1.0f);
					foundKey = true;
				}
				else if (keyMap.map["moveWaypointOut"].contains(ksym))
				{
					waypointManager->moveMask -= Vector3(0.0f, 0.0f, -1.0f);
					foundKey = true;
				}
				else if (keyMap.map["moveWaypointRight"].contains(ksym))
				{
					waypointManager->moveMask -= Vector3(1.0f, 0.0f, 0.0f);
					foundKey = true;
				}
				else if (keyMap.map["moveWaypointLeft"].contains(ksym))
				{
					waypointManager->moveMask -= Vector3(-1.0f, 0.0f, 0.0f);
					foundKey = true;
				}
			}
		}
	}
	// Handle key strokes explicitly for non-developer mode
	else
	{
		if (event.type == GEventType::KEY_DOWN)
		{
			// Remove F8 as it isn't masked by useDeveloperTools = False
			const Array<GKey> player_masked = { GKey::F8 };
			if (player_masked.contains(ksym))
			{
				foundKey = true;
			}
		}
	}

	// Handle normal keypresses
	if (event.type == GEventType::KEY_DOWN)
	{
		if (keyMap.map["openMenu"].contains(ksym))
		{
			toggleUserSettingsMenu();
			foundKey = true;
		}
		else if (keyMap.map["quit"].contains(ksym))
		{
			quitRequest();
			return true;
		}
		else if (notNull(dialog) && !dialog->complete)
		{ // If we have an open, incomplete dialog, check for key bound question responses
			// Handle key presses to redirect towards dialog
			for (int i = 0; i < currentQuestion.optionKeys.length(); i++)
			{
				if (currentQuestion.optionKeys[i] == ksym)
				{
					dialog->result = currentQuestion.options[i];
					dialog->complete = true;
					dialog->setVisible(false);
					foundKey = true;
				}
			}
		}
		else if (activeCamera() == playerCamera && !foundKey)
		{
			// Override 'q', 'z', 'c', and 'e' keys (unused)
			// THIS IS A PROBLEM IF THESE ARE KEY MAPPED!!!
			const Array<GKey> unused = { (GKey)'e', (GKey)'z', (GKey)'c', (GKey)'q' };
			if (unused.contains(ksym))
			{
				foundKey = true;
			}
			else if (keyMap.map["crouch"].contains(ksym))
			{
				scene()->typedEntity<PlayerEntity>("player")->setCrouched(true);
				foundKey = true;
			}
			else if (keyMap.map["jump"].contains(ksym))
			{
				scene()->typedEntity<PlayerEntity>("player")->setJumpPressed(true);
				foundKey = true;
			}
			else if (keyMap.map["sprint"].contains(ksym)) {
				scene()->typedEntity<PlayerEntity>("player")->setSprintPressed(true);
				foundKey = true;
			}
		}
	}
	else if ((event.type == GEventType::KEY_UP))
	{
		if (activeCamera() == playerCamera)
		{
			if (keyMap.map["crouch"].contains(ksym))
			{
				scene()->typedEntity<PlayerEntity>("player")->setCrouched(false);
				foundKey = true;
			}
			else if (keyMap.map["sprint"].contains(ksym)) {
				scene()->typedEntity<PlayerEntity>("player")->setSprintPressed(false);
				foundKey = true;
			}
		}
	}
	if (foundKey)
	{
		return true;
	}

	// Handle window resize here
	if (event.type == GEventType::VIDEO_RESIZE)
	{
		moveToCenter(m_userSettingsWindow);
	}

	// Handle window-based close ("X" button)
	if (event.type == GEventType::QUIT)
	{
		quitRequest();
		return true;
	}

	// Handle resize event here
	if (event.type == GEventType::VIDEO_RESIZE)
	{
		// Resize the shader buffers here
		updateShaderBuffers();
	}

	// Handle super-class events
	return GApp::onEvent(event);
}

void FPSciApp::onAfterEvents() {
	if (updateUserMenu)
	{
		// Remove the old settings window
		removeWidget(m_userSettingsWindow);

		// Re-create the settings window
		String selSess = m_userSettingsWindow->selectedSession();
		m_userSettingsWindow = UserMenu::create(this, userTable, userStatusTable, sessConfig->menu, theme, Rect2D::xywh(0.0f, 0.0f, 10.0f, 10.0f));
		m_userSettingsWindow->setSelectedSession(selSess);
		moveToCenter(m_userSettingsWindow);
		m_userSettingsWindow->setVisible(m_showUserMenu);
		setMouseInputMode(m_showUserMenu ? MouseInputMode::MOUSE_CURSOR : MouseInputMode::MOUSE_FPM);

		// Add the new settings window and clear the semaphore
		addWidget(m_userSettingsWindow);
		updateUserMenu = false;
	}

	if (reinitExperiment)
	{ // Check for experiment reinitialization (developer-mode only)
		m_widgetManager->clear();
		addWidget(debugWindow);
		addWidget(developerWindow);
		initExperiment();
		reinitExperiment = false;
	}

	GApp::onAfterEvents();
}

Vector2 FPSciApp::currentTurnScale() {
	const shared_ptr<UserConfig> user = currentUser();
	Vector2 baseTurnScale = sessConfig->player.turnScale * user->turnScale;
	// Apply y-invert here
	if (user->invertY)
		baseTurnScale.y = -baseTurnScale.y;
	// If we're not scoped just return the normal user turn scale
	if (!weapon || !weapon->scoped())
		return baseTurnScale;
	// Otherwise create scaled turn scale for the scoped state
	if (user->scopeTurnScale.length() > 0)
	{
		// User scoped turn scale specified, don't perform default scaling
		return baseTurnScale * user->scopeTurnScale;
	}
	else
	{
		// Otherwise scale the scope turn scalue using the ratio of FoV
		return playerCamera->fieldOfViewAngleDegrees() / sessConfig->render.hFoV * baseTurnScale;
	}
}

void FPSciApp::setScopeView(bool scoped) {
	// Get player entity and calculate scope FoV
	const shared_ptr<PlayerEntity>& player = scene()->typedEntity<PlayerEntity>("player");
	const float scopeFoV = sessConfig->weapon.scopeFoV > 0 ? sessConfig->weapon.scopeFoV : sessConfig->render.hFoV;
	weapon->setScoped(scoped);													 // Update the weapon state
	const float FoV = (scoped ? scopeFoV : sessConfig->render.hFoV);			 // Get new FoV in degrees (depending on scope state)
	playerCamera->setFieldOfView(FoV * pif() / 180.f, FOVDirection::HORIZONTAL); // Set the camera FoV
	player->turnScale = currentTurnScale();										 // Scale sensitivity based on the field of view change here
}

void FPSciApp::hitTarget(shared_ptr<TargetEntity> target) {
	// Damage the target
	float damage = m_currentWeaponDamage;
	target->doDamage(damage);
	target->playHitSound();

	debugPrintf("HIT TARGET: %s\n", target->name().c_str());
	if (experimentConfig.isNetworked) {
		if (sess->currentState != PresentationState::networkedSessionRoundFeedback && sess->currentState != PresentationState::networkedSessionRoundTimeout && sess->currentState != PresentationState::initialNetworkedState && sess->currentState != PresentationState::networkedSessionRoundOver) {
			shared_ptr<ReportHitPacket> outPacket = GenericPacket::createReliable<ReportHitPacket>(m_serverPeer);
			outPacket->populate(m_networkFrameNum, GUniqueID::fromString16(target->name().c_str()), m_playerGUID);
			NetworkUtils::send(outPacket);
		}
		return;
	}

	// Check if we need to add combat text for this damage
	if (sessConfig->targetView.showCombatText)
	{
		m_combatTextList.append(FloatingCombatText::create(
			format("%2.0f", 100.f * damage),
			m_combatFont,
			sessConfig->targetView.combatTextSize,
			sessConfig->targetView.combatTextColor,
			sessConfig->targetView.combatTextOutline,
			sessConfig->targetView.combatTextOffset,
			sessConfig->targetView.combatTextVelocity,
			sessConfig->targetView.combatTextFade,
			sessConfig->targetView.combatTextTimeout));
		m_combatTextList.last()->setFrame(target->frame());
	}

	// Check for "kill" condition
	bool respawned = false;
	bool destroyedTarget = false;
	if (target->name() == "reference")
	{
		// Handle reference target here
		sess->destroyTarget(target);
		if (notNull(m_refTargetHitSound))
		{
			m_refTargetHitSound->play(sessConfig->audio.refTargetHitSoundVol);
		}
		destroyedTarget = true;
		sess->accumulatePlayerAction(PlayerActionType::Destroy, target->name());
	}
	else if (target->health() <= 0)
	{
		// Position explosion
		CFrame explosionFrame = target->frame();
		explosionFrame.rotation = playerCamera->frame().rotation;
		// Create the explosion
		const shared_ptr<VisibleEntity> newExplosion = VisibleEntity::create(
			format("explosion%d", m_explosionIdx),
			scene().get(),
			m_explosionModels.get(target->id())[target->scaleIndex()],
			explosionFrame);
		m_explosionIdx++;
		m_explosionIdx %= m_maxExplosions;
		scene()->insert(newExplosion);
		m_explosions.push(newExplosion);
		m_explosionRemainingTimes.push(experimentConfig.getTargetConfigById(target->id())->destroyDecalDuration); // Schedule end of explosion
		target->playDestroySound();

		sess->countDestroy();
		respawned = target->tryRespawn();
		// check for respawn
		if (!respawned)
		{
			// This is the final respawn
			// destroyTarget(hitIdx);
			sess->destroyTarget(target);
			destroyedTarget = true;
		}
		// Target eliminated, must be 'destroy'.
		sess->accumulatePlayerAction(PlayerActionType::Destroy, target->name());
	}
	else
	{
		// Target 'hit', but still alive.
		sess->accumulatePlayerAction(PlayerActionType::Hit, target->name());
	}
	if (!destroyedTarget || respawned)
	{
		if (respawned)
		{
			sess->randomizePosition(target);
		}
		// Update the target color based on it's health
		updateTargetColor(target);
	}
}

void FPSciApp::updateTargetColor(const shared_ptr<TargetEntity>& target) {
	BEGIN_PROFILER_EVENT("updateTargetColor/changeColor");
	BEGIN_PROFILER_EVENT("updateTargetColor/clone");
	shared_ptr<ArticulatedModel::Pose> pose = dynamic_pointer_cast<ArticulatedModel::Pose>(target->pose()->clone());
	END_PROFILER_EVENT();
	BEGIN_PROFILER_EVENT("updateTargetColor/materialSet");
	shared_ptr<UniversalMaterial> mat = materials[target->id()][min((int)(target->health() * matTableSize), matTableSize - 1)];
	pose->materialTable.set("core/icosahedron_default", mat);
	END_PROFILER_EVENT();
	BEGIN_PROFILER_EVENT("updateTargetColor/setPose");
	target->setPose(pose);
	END_PROFILER_EVENT();
	END_PROFILER_EVENT();
}

void FPSciApp::missEvent() {
	if (sess)
	{
		sess->accumulatePlayerAction(PlayerActionType::Miss); // Declare this shot a miss here

		// If this is a networked experiment send this miss data to the server
		if (experimentConfig.isNetworked && m_socketConnected) {
			shared_ptr<PlayerInteractPacket> outPacket = GenericPacket::createUnreliable<PlayerInteractPacket>(&m_unreliableSocket, &m_unreliableServerAddress);
			outPacket->populate(m_networkFrameNum, PlayerActionType::Miss, m_playerGUID);
			NetworkUtils::send(outPacket);
			//outPacket->send();
		}
	}
}

/** Handle user input here */
void FPSciApp::onUserInput(UserInput* ui) {
	BEGIN_PROFILER_EVENT("onUserInput");

	GApp::onUserInput(ui);

	const shared_ptr<PlayerEntity>& player = scene()->typedEntity<PlayerEntity>("player");
	if (m_mouseInputMode == MouseInputMode::MOUSE_FPM && activeCamera() == playerCamera && notNull(player))
	{
		player->updateFromInput(ui); // Only update the player if the mouse input mode is FPM and the active camera is the player view camera
	}
	else if (notNull(player))
	{ // Zero the player velocity and rotation when in the setting menu
		player->setDesiredOSVelocity(Vector3::zero());
		player->setDesiredAngularVelocity(0.0, 0.0);
	}

	// Handle scope behavior
	for (GKey scopeButton : keyMap.map["scope"])
	{
		if (ui->keyPressed(scopeButton))
		{
			// Are we using scope toggling?
			if (sessConfig->weapon.scopeToggle)
			{
				setScopeView(!weapon->scoped());
			}
			// Otherwise just set scope based on the state of the scope button
			else
			{
				setScopeView(true);
			}
		}
		if (ui->keyReleased(scopeButton) && !sessConfig->weapon.scopeToggle)
		{
			setScopeView(false);
		}
	}

	// Record button state changes
	// These will be evaluated and reset on the next onSimulation()
	for (GKey shootButton : keyMap.map["shoot"])
	{
		if (ui->keyReleased(shootButton))
		{
			shootButtonJustReleased = true;
			shootButtonUp = true;
		}
		if (ui->keyPressed(shootButton))
		{
			shootButtonJustPressed = true;
		}
		if (ui->keyDown(shootButton))
		{
			shootButtonUp = false;
		}
	}

	for (GKey selectButton : keyMap.map["selectWaypoint"])
	{
		// Check for developer mode editing here, if so set selected waypoint using the camera
		if (ui->keyDown(selectButton) && notNull(waypointManager))
		{
			waypointManager->aimSelectWaypoint(activeCamera());
		}
	}

	for (GKey dummyShoot : keyMap.map["dummyShoot"])
	{
		if (ui->keyPressed(dummyShoot) && (sess->currentState == PresentationState::trialFeedback) && !m_userSettingsWindow->visible())
		{
			Array<shared_ptr<Entity>> dontHit;
			dontHit.append(m_explosions);
			dontHit.append(sess->unhittableTargets());
			Model::HitInfo info;
			float hitDist = finf();
			int hitIdx = -1;
			shared_ptr<TargetEntity> target = weapon->fire(sess->hittableTargets(), hitIdx, hitDist, info, dontHit, true); // Fire the weapon
			if (sessConfig->audio.refTargetPlayFireSound && !sessConfig->weapon.loopAudio())
			{									// Only play shot sounds for non-looped weapon audio (continuous/automatic fire not allowed)
				weapon->playSound(true, false); // Play audio here for reference target
			}
		}
	}

	for (GKey selectButton : keyMap.map["readyUp"])
	{
		// Send Ready Up Message from here
		if (ui->keyDown(selectButton) && m_serverPeer != nullptr && m_enetConnected) {
			if (!player->getPlayerReady()) {
				player->setPlayerReady(true);
				shared_ptr<ReadyUpClientPacket> outPacket = GenericPacket::createReliable<ReadyUpClientPacket>(m_serverPeer);
				NetworkUtils::send(outPacket);
				//outPacket->send();
			}
		}
	}

	if (m_lastReticleLoaded != currentUser()->reticle.index || m_userSettingsWindow->visible())
	{
		// Slider was used to change the reticle
		if (!sessConfig->reticle.indexSpecified)
		{ // Only allow reticle change if it isn't specified in experiment config
			setReticle(currentUser()->reticle.index);
			m_userSettingsWindow->updateReticlePreview();
		}
	}

	playerCamera->filmSettings().setSensitivity(sceneBrightness);
	END_PROFILER_EVENT();
}

void FPSciApp::onPose(Array<shared_ptr<Surface>>& surface, Array<shared_ptr<Surface2D>>& surface2D) {
	GApp::onPose(surface, surface2D);

	typedScene<PhysicsScene>()->poseExceptExcluded(surface, "player");

	if (weapon)
	{
		weapon->onPose(surface);
	}
}

/** Set the currently reticle by index */
void FPSciApp::setReticle(const int r) {
	int idx = clamp(0, r, numReticles);
	if (idx == m_lastReticleLoaded)
		return; // Nothing to do here, setting current reticle
	if (r < numReticles)
	{
		reticleTexture = Texture::fromFile(System::findDataFile(format("gui/reticle/reticle-%03d.png", idx)));
	}
	else
	{
		// This special case is added to allow a custom reticle not in the gui/reticle/reticle-[x].png format
		reticleTexture = Texture::fromFile(System::findDataFile("gui/reticle.png"));
	}
	m_lastReticleLoaded = idx;
}

void FPSciApp::onCleanup() {
	// Called after the application loop ends.  Place a majority of cleanup code
	// here instead of in the constructor so that exceptions can be caught.
}

/** Overridden (optimized) oneFrame() function to improve latency */
void FPSciApp::oneFrame() {
	// Count this frame (for shaders)
	m_frameNumber++;

	// Target frame time (only call this method once per one frame!)
	RealTime targetFrameTime = sess->targetFrameTime();

	// Wait
	// Note: we might end up spending all of our time inside of
	// RenderDevice::beginFrame.  Waiting here isn't double waiting,
	// though, because while we're sleeping the CPU the GPU is working
	// to catch up.
	if ((submitToDisplayMode() == SubmitToDisplayMode::MINIMIZE_LATENCY))
	{
		BEGIN_PROFILER_EVENT("Wait");
		m_waitWatch.tick();
		{
			RealTime nowAfterLoop = System::time();

			// Compute accumulated time
			RealTime cumulativeTime = nowAfterLoop - m_lastWaitTime;

			debugAssert(m_wallClockTargetDuration < finf());
			// Perform wait for target time needed
			RealTime duration = targetFrameTime;
			if (!window()->hasFocus() && m_lowerFrameRateInBackground)
			{
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
			if (G3D::abs(thisOverWait - m_lastFrameOverWait) / max(G3D::abs(m_lastFrameOverWait), G3D::abs(thisOverWait)) > 0.4)
			{
				// Abruptly change our estimate
				m_lastFrameOverWait = thisOverWait;
			}
			else
			{
				// Smoothly change our estimate
				m_lastFrameOverWait = lerp(m_lastFrameOverWait, thisOverWait, 0.1);
			}
		}
		m_waitWatch.tock();
		END_PROFILER_EVENT();
	}

	for (int repeat = 0; repeat < max(1, m_renderPeriod); ++repeat)
	{
		Profiler::nextFrame();
		m_lastTime = m_now;
		m_now = System::time();
		RealTime timeStep = m_now - m_lastTime;

		// User input
		m_userInputWatch.tick();
		if (manageUserInput)
		{
			processGEventQueue();
		}
		onAfterEvents();
		onUserInput(userInput);
		m_userInputWatch.tock();

		// Network
		BEGIN_PROFILER_EVENT("GApp::onNetwork");
		m_networkWatch.tick();
		if (experimentConfig.isNetworked) {
			onNetwork();
		}
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
			if (sdt == MATCH_REAL_TIME_TARGET)
			{
				sdt = (SimTime)targetFrameTime;
			}
			else if (sdt == REAL_TIME)
			{
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
	if ((submitToDisplayMode() != SubmitToDisplayMode::MINIMIZE_LATENCY))
	{
		BEGIN_PROFILER_EVENT("Wait");
		m_waitWatch.tick();
		{
			RealTime nowAfterLoop = System::time();

			// Compute accumulated time
			RealTime cumulativeTime = nowAfterLoop - m_lastWaitTime;

			debugAssert(m_wallClockTargetDuration < finf());
			// Perform wait for actual time needed
			RealTime duration = targetFrameTime;
			if (!window()->hasFocus() && m_lowerFrameRateInBackground)
			{
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
			if (G3D::abs(thisOverWait - m_lastFrameOverWait) / max(G3D::abs(m_lastFrameOverWait), G3D::abs(thisOverWait)) > 0.4)
			{
				// Abruptly change our estimate
				m_lastFrameOverWait = thisOverWait;
			}
			else
			{
				// Smoothly change our estimate
				m_lastFrameOverWait = lerp(m_lastFrameOverWait, thisOverWait, 0.1);
			}
		}
		m_waitWatch.tock();
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

	if (m_endProgram && window()->requiresMainLoop())
	{
		window()->popLoopBody();
	}
}

FPSciApp::Settings::Settings(const StartupConfig& startupConfig, int argc, const char* argv[]) {
	if (startupConfig.fullscreen)
	{
		// Use the primary
		window.width = (int)OSWindow::primaryDisplaySize().x;
		window.height = (int)OSWindow::primaryDisplaySize().y;
	}
	else
	{
		window.width = (int)startupConfig.windowSize.x;
		window.height = (int)startupConfig.windowSize.y;
	}
	window.fullScreen = startupConfig.fullscreen;
	window.resizable = !window.fullScreen;

	// V-sync off always
	window.asynchronous = true;
	window.caption = "First Person Science";
	window.refreshRate = -1;
	window.defaultIconFilename = "icon.png";

	useDeveloperTools = startupConfig.developerMode;

	hdrFramebuffer.depthGuardBandThickness = Vector2int16(64, 64);
	hdrFramebuffer.colorGuardBandThickness = Vector2int16(0, 0);
	dataDir = FileSystem::currentDirectory();
	screenCapture.includeAppRevision = false;
	screenCapture.includeG3DRevision = false;
	screenCapture.outputDirectory = ""; // "../journal/"
	screenCapture.filenamePrefix = "_";

	renderer.deferredShading = true;
	renderer.orderIndependentTransparency = false;
}

uint32 FPSciApp::frameNumFromID(GUniqueID id) {
	return m_serverFrame;
}