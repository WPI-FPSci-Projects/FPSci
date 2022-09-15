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

/* Data storage object for Logging purposes*/
struct NetworkedClient {
	FILETIME	time;
	Point2		viewDirection = Point2::zero();
	Point3		position = Point3::zero();
	GUniqueID	playerID = GUniqueID::NONE(0);
	uint32		serverFrame = 0;
	uint32		clientFrame = 0;
	PresentationState	state;
	PlayerActionType	action = PlayerActionType::None;

	NetworkedClient() {};

	NetworkedClient(FILETIME t, Point2 playerViewDirection, Point3 playerPosition, PresentationState trialState, PlayerActionType playerAction, GUniqueID id, uint32 server_frame, uint32 client_frame) {
		time = t;
		viewDirection = playerViewDirection;
		position = playerPosition;
		action = playerAction;
		state = trialState;
		clientFrame = client_frame;
		serverFrame = server_frame;
		playerID = id;
	}

	inline bool noChangeFrom(const NetworkedClient& other) const {
		return viewDirection == other.viewDirection && position == other.position && action == other.action && state == other.state && playerID == other.playerID;
	}

};


class NetworkedSession : public Session {
protected:
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
};
