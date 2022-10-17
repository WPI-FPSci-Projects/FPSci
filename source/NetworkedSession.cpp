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
}

void NetworkedSession::onInit(String filename, String description)
{
	m_player = m_app->scene()->typedEntity<PlayerEntity>("player");
	resetSession();
}

void NetworkedSession::updateNetworkedPresentationState()
{
	if (currentState == PresentationState::initialNetworkedState) {
		if (!m_player->getPlayerReady())
			m_feedbackMessage = formatFeedback(m_config->feedback.networkedSesstionInitial);
		else
			m_feedbackMessage = formatFeedback(m_config->feedback.networkedSesstionWaitForOthers);
	}
	else if (currentState == PresentationState::networkedSessionRoundStart) {
		if (getRemainingTrialTime() <= 0.0f && !m_app->isServer)
			sessionTimeout();
	}
	else if (currentState == PresentationState::networkedSessionRoundFeedback) {
		updatePresentationState();
	}
}

void NetworkedSession::startSession()
{
	m_config->hud.enable = true;
	m_config->hud.showBanner = true;
	m_config->hud.bannerTimerMode = "remaining";
	m_timer.startTimer();
	m_player->setPlayerMovement(true);
	m_feedbackMessage.clear();
	sessionStarted = true;
	currentState = PresentationState::networkedSessionRoundStart;
}

void NetworkedSession::resetSession()
{
	currentState = PresentationState::initialNetworkedState;
	m_player->setPlayerReady(false);
	m_player->setPlayerMovement(false);
}

void NetworkedSession::sessionTimeout()
{
	currentState = PresentationState::networkedSessionRoundTimeout;
	NetworkUtils::sendSessionTimeoutMessage(m_app->getServerPeer(), m_app->getFrameNumber());
}

void NetworkedSession::feedbackStart() {
	currentState = PresentationState::networkedSessionRoundFeedback;
}