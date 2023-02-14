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
#pragma once

#include <G3D/G3D.h>
#include "FpsConfig.h"
#include <ctime>
#include "Session.h"
#include "NetworkUtils.h"

struct RemotePlayerAction {
	FILETIME			time;
	Point2				viewDirection = Point2::zero();
	Point3				position = Point3::zero();
	PresentationState	state;
	PlayerActionType	action = PlayerActionType::None;
	String				actorID = "";
	String				affectedID = "";

	RemotePlayerAction() {};

	RemotePlayerAction(FILETIME t, Point2 playerViewDirection, Point3 playerPosition, PresentationState trialState, PlayerActionType playerAction, String actor, String affected) {
		time = t;
		viewDirection = playerViewDirection;
		position = playerPosition;
		action = playerAction;
		state = trialState;
		actorID = actor;
		affectedID = affected;
	}

	inline bool noChangeFrom(const RemotePlayerAction& other) const {
		return viewDirection == other.viewDirection && position == other.position && action == other.action && state == other.state && actorID == other.actorID && affectedID == other.affectedID;
	}
};

/* Data storage object for Logging purposes*/
struct NetworkedClient {
	FILETIME	time;
	Point2		viewDirection = Point2::zero();
	Point3		position = Point3::zero();
	GUniqueID	playerID = GUniqueID::NONE(0);
	uint32		localFrame = 0;
	uint32		remoteFrame = 0;
	PresentationState	state;
	PlayerActionType	action = PlayerActionType::None;

	NetworkedClient() {};

	NetworkedClient(FILETIME t, Point2 playerViewDirection, Point3 playerPosition, GUniqueID id, uint32 local_frame, uint32 remote_frame) {
		time = t;
		viewDirection = playerViewDirection;
		position = playerPosition;
		remoteFrame = remote_frame;
		localFrame = local_frame;
		playerID = id;
	}

	NetworkedClient(FILETIME t, Point2 playerViewDirection, Point3 playerPosition, GUniqueID id, uint32 local_frame, uint32 remote_frame, PresentationState playerState, PlayerActionType playerAction) {
		time = t;
		viewDirection = playerViewDirection;
		position = playerPosition;
		remoteFrame = remote_frame;
		localFrame = local_frame;
		playerID = id;
		state = playerState;
		action = playerAction;

	}
	
	inline bool noChangeFrom(const NetworkedClient& other) const {
		return viewDirection == other.viewDirection && position == other.position && state == other.state && playerID == other.playerID;
	}

};

/* Ping statistics designated for logging */
struct LoggedPingStatistics {
	long long latestPing;
	long long smaPing;
	long long maxPing;
	long long minPing;
	
	long long rawLatest;
	long long rawSMA;
	long long rawMin;
	long long rawMax;

	LoggedPingStatistics() {};

	LoggedPingStatistics(NetworkUtils::PingStatistics stats) {
		latestPing = stats.pingQueue.last();
		smaPing = stats.smaPing;
		minPing = stats.minPing;
		maxPing = stats.maxPing;
		
		rawLatest = stats.rawPingQueue.last();
		rawSMA = stats.rawSMAPing;
		rawMin = stats.rawMinPing;
		rawMax = stats.rawMaxPing;
	}
};

/* Simple Struct for logging raw simulated weapon fire inputs across the network */
struct RawRemoteFireInput {
	String shooterID = "";
	Point3 shooterPos = Point3::zero();
	String targetID_TW = "";
	String targetID_No_TW = "";
	Point3 targetPos = Point3::zero();
	bool hitTimeWarp = false;
	bool hitNoTimeWarp = false;
	uint32 frameNum = 0;

	RawRemoteFireInput() {};

};


class NetworkedSession : public Session {
protected:

	bool m_sessionStarted = false;			///< Checks if the session has started or not
	bool m_roundOver = false;				///< Checks if the round is over or not

	NetworkedSession(FPSciApp* app) : Session(app) {}
	NetworkedSession(FPSciApp* app, shared_ptr<SessionConfig> config) : Session(app, config) {}

public:

	static shared_ptr<NetworkedSession> create(FPSciApp* app) {
		return createShared<NetworkedSession>(app);
	}
	static shared_ptr<NetworkedSession> create(FPSciApp* app, shared_ptr<SessionConfig> config) {
		return createShared<NetworkedSession>(app, config);
	}
	void addHittableTarget(shared_ptr<TargetEntity> target);
	void onSimulation(RealTime rdt, SimTime sdt, SimTime idt) override;
	void onInit(String filename, String description) override;
	void updateNetworkedPresentationState();
	void startRound();
	void resetRound();
	void roundTimeout();
	void feedbackStart();
	void endSession();
	void accumulateFrameInfo(RealTime t, float sdt, float idt) override;
	void logNetworkedEntity(shared_ptr<NetworkedEntity> entity, uint32 remoteFrame, PlayerActionType action);
	void logNetworkedEntity(shared_ptr<NetworkedEntity> entity, uint32 remoteFrame);
};
