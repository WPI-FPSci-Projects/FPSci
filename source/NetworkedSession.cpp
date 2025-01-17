/***************************************************************************
# Copyright (c) 2015, NVIDIA CORPORATION. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#  * Neither the name of NVIDIA CORPORATION nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
# OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***************************************************************************/
#include "NetworkedSession.h"
#include "FPSciApp.h"
#include "FPSciServerApp.h"
#include "Logger.h"
#include "TargetEntity.h"
#include "PlayerEntity.h"
#include "Dialogs.h"
#include "Weapon.h"
#include "FPSciAnyTableReader.h"



void NetworkedSession::addHittableTarget(shared_ptr<TargetEntity> target) {
	m_hittableTargets.append(target);
}

void NetworkedSession::onSimulation(RealTime rdt, SimTime sdt, SimTime idt)
{
	updateNetworkedPresentationState();

	Array<shared_ptr<NetworkedEntity>> entityArray;
	m_app->scene()->getTypedEntityArray<NetworkedEntity>(entityArray);
	for (std::shared_ptr<NetworkedEntity> client : entityArray) {
		logNetworkedEntity(client, m_app->frameNumFromID(GUniqueID::fromString16(client->name())));
	}
	FPSciServerApp* serverApp = dynamic_cast<FPSciServerApp*> (m_app);
	if (serverApp != nullptr) {
		for (NetworkUtils::ConnectedClient* client : serverApp->getConnectedClients()) {
			//TODO should be accumulate? not sure, but doing this for now to prevent crash:
			if (notNull(logger)) {
				logger->logFrameInfo(FrameInfo(FPSciLogger::getFileTime(), sdt, client->peer->lastRoundTripTime, serverApp->m_networkFrameNum, client->frameNumber, client->guid));
			}
		}
	}
	accumulatePlayerAction(PlayerActionType::Move);
}

void NetworkedSession::onInit(String filename, String description)
{
	//TODO: Networked Init
	m_player = m_app->scene()->typedEntity<PlayerEntity>("player");
	m_scene = m_app->scene().get();
	m_camera = m_app->activeCamera();
	String filenameBase = filename;

	if (m_config->logger.enable) {
		UserConfig user = *m_app->currentUser();
		// Setup the logger and create results file
		FPSciServerApp* server_app = dynamic_cast<FPSciServerApp*>(m_app);
		if (server_app != nullptr) {
			filename += "-server.db";
		}
		else {
			filename += "-client-" + m_app->m_playerGUID.toString16() + ".db";
		}
		logger = FPSciLogger::create(filename, user.id,
			m_app->startupConfig.experimentList[m_app->experimentIdx].experimentConfigFilename,
			m_config, description);
		logger->logTargetTypes(m_app->experimentConfig.getSessionTargets(m_config->id));			// Log target info at start of session
		logger->logUserConfig(user, m_config->id, m_config->player.turnScale);						// Log user info at start of session
		m_dbFilename = filenameBase;
	}
	m_player = m_app->scene()->typedEntity<PlayerEntity>("player");
	resetRound();
	String testStr = presentationStateToString(currentState);
}


void NetworkedSession::updateNetworkedPresentationState() {
	if (currentState == PresentationState::initialNetworkedState) {
		if (!m_player->getPlayerReady())
			m_feedbackMessage = formatFeedback(m_config->feedback.networkedSesstionInitial);
		else
			m_feedbackMessage = formatFeedback(m_config->feedback.networkedSesstionWaitForOthers);
	}
	else if (currentState == PresentationState::networkedSessionRoundStart) {
		if (getRemainingTrialTime() <= 0.0f && !m_app->isServer)
			roundTimeout();
	}
	else if (currentState == PresentationState::networkedSessionRoundFeedback) {
		updatePresentationState();
	}
	else if (currentState == PresentationState::networkedSessionRoundOver) {

		if (!m_roundOver) {
			shared_ptr<ClientFeedbackSubmittedPacket> outPacket = GenericPacket::createReliable<ClientFeedbackSubmittedPacket>(m_app->getServerPeer());
			NetworkUtils::send(outPacket);
		}

		m_player->setPlayerMovement(false);
		m_roundOver = true;
	}
}

void NetworkedSession::startRound() {
	m_config->hud.enable = true;
	m_config->hud.showBanner = true;
	m_config->hud.bannerTimerMode = "remaining";
	m_timer.startTimer();
	m_player->setPlayerMovement(true);
	m_feedbackMessage.clear();
	m_sessionStarted = true;
	currentState = PresentationState::networkedSessionRoundStart;
	m_roundOver = false;
	m_app->closeUserSettingsWindow();
}


void NetworkedSession::resetRound() {
	currentState = PresentationState::initialNetworkedState;
	m_player->setPlayerReady(false);
	m_player->setPlayerMovement(false);
}


void NetworkedSession::roundTimeout() {
	currentState = PresentationState::networkedSessionRoundTimeout;
	shared_ptr<ClientRoundTimeoutPacket> outPacket = GenericPacket::createReliable<ClientRoundTimeoutPacket>(m_app->getServerPeer());
	NetworkUtils::send(outPacket);
}

void NetworkedSession::feedbackStart() {
	currentState = PresentationState::networkedSessionRoundFeedback;
}

void NetworkedSession::endSession() {
	endLogging();
	m_app->quitRequest();
}

void NetworkedSession::logNetworkedEntity(shared_ptr<NetworkedEntity> entity, uint32 remoteFrame) {
	logNetworkedEntity(entity, remoteFrame, PlayerActionType::None);
}

void NetworkedSession::logNetworkedEntity(shared_ptr<NetworkedEntity> entity, uint32 remoteFrame, PlayerActionType action)
{
	if (notNull(logger)) {
		Point2 dir = entity->getLookAzEl();
		Point3 loc = entity->frame().translation;
		GUniqueID id = GUniqueID::fromString16(entity->name());
		NetworkedClient nc = NetworkedClient(FPSciLogger::getFileTime(), dir, loc, id, m_app->m_networkFrameNum, remoteFrame, currentState, action);
		logger->logNetworkedClient(nc);
		//debugPrintf("Logged...");
	}
}

void NetworkedSession::accumulateFrameInfo(RealTime t, float sdt, float idt) {
	if (notNull(logger) && m_config->logger.logFrameInfo) {
		logger->logFrameInfo(FrameInfo(FPSciLogger::getFileTime(), sdt));
	}
}

