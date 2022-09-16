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


	//TODO: Networked Ticks
	Array<shared_ptr<NetworkedEntity>> entityArray;
	m_app->scene()->getTypedEntityArray<NetworkedEntity>(entityArray);
	for (std::shared_ptr<NetworkedEntity> client : entityArray) {
		Point2 dir = Point2();//client->frame().rotation;
		Point3 loc = client->frame().translation;
		GUniqueID id = GUniqueID::fromString16(client->name());
		int frame = m_app->frameNumFromID(id);
		NetworkedClient nc = NetworkedClient(FPSciLogger::getFileTime(), dir, loc, id, m_app->m_frameNumber, frame);
		logger->logNetworkedClient(nc);
		debugPrintf("Logged...");
	}
	
}

void NetworkedSession::onInit(String filename, String description)
{
	//TODO: Networked Init
	m_player = m_app->scene()->typedEntity<PlayerEntity>("player");
	m_scene = m_app->scene().get();
	m_camera = m_app->activeCamera();

	if (m_config->logger.enable) {
		UserConfig user = *m_app->currentUser();
		// Setup the logger and create results file
		
		logger = FPSciLogger::create(filename + "-net" + ".db", user.id,
			m_app->startupConfig.experimentList[m_app->experimentIdx].experimentConfigFilename,
			m_config, description);
		logger->logTargetTypes(m_app->experimentConfig.getSessionTargets(m_config->id));			// Log target info at start of session
		logger->logUserConfig(user, m_config->id, m_config->player.turnScale);						// Log user info at start of session
		m_dbFilename = filename;
	}
	m_player = m_app->scene()->typedEntity<PlayerEntity>("player");
	resetSession();
}

void NetworkedSession::updateNetworkedPresentationState()
{
	if (currentState == NetworkedPresentationState::initialNetworkedState) {
		if (!m_player->getPlayerReady())
			m_feedbackMessage = formatFeedback(m_config->feedback.networkedSesstionInitial);
		else
			m_feedbackMessage = formatFeedback(m_config->feedback.networkedSesstionWaitForOthers);
	}
	else if (currentState == NetworkedPresentationState::networkedSessionStart) {
		// TODO: Experiment Session Ticks
	}
}

void NetworkedSession::startSession()
{
	sessionStarted = true;
	currentState = NetworkedPresentationState::networkedSessionStart;
	m_player->setPlayerMovement(true);
	m_feedbackMessage.clear();
}

void NetworkedSession::resetSession()
{
	currentState = NetworkedPresentationState::initialNetworkedState;
	m_player->setPlayerReady(false);
	m_player->setPlayerMovement(false);
}
