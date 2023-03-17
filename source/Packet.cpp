#include "Packet.h"

/******************
 * Generic Packet *
 ******************/

GenericPacket::GenericPacket() {
	m_inbound = false;
}
GenericPacket::GenericPacket(ENetAddress srcAddr) {
	m_srcAddr = srcAddr;
	m_inbound = true;
}

GenericPacket::GenericPacket(ENetAddress srcAddr, BinaryInput& inBuffer) {
	m_srcAddr = srcAddr;
	m_inbound = true;
	this->deserialize(inBuffer);
}
GenericPacket::GenericPacket(ENetPeer* destPeer) {
	m_inbound = false;
	m_destPeer = destPeer;
	m_reliable = true;
}
GenericPacket::GenericPacket(ENetSocket* srcSocket, ENetAddress* destAddr) {
	m_inbound = false;
	m_srcSocket = srcSocket;
	m_destAddr = destAddr;
	m_reliable = false;
}

int GenericPacket::send() {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D_BIG_ENDIAN);
	this->serialize(outBuffer);
	if (m_reliable) {
		ENetPacket* packet = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length(), ENET_PACKET_FLAG_RELIABLE);
		return enet_peer_send(m_destPeer, 0, packet);
	}
	else {
		ENetBuffer buff;
		buff.data = (void*)outBuffer.getCArray();
		buff.dataLength = outBuffer.length();
		return enet_socket_send(*m_srcSocket, m_destAddr, &buff, 1);
	}
}

void GenericPacket::serialize(BinaryOutput& outBuffer) {
	outBuffer.writeUInt8(this->type());
}

void GenericPacket::deserialize(BinaryInput& inBuffer) {
	m_type = (PacketType)inBuffer.readUInt8();
}


/******************************
 * Batch Entity Udpate Packet *
 ******************************/

void BatchEntityUpdatePacket::populate(Array<EntityUpdate> updates, NetworkUpdateType updateType) {
	m_updates = updates;
	m_updateType = updateType;
}

void BatchEntityUpdatePacket::serialize(BinaryOutput& outBuffer) {
	GenericPacket::serialize(outBuffer);	// Call the super serialize
	outBuffer.writeUInt8(m_updates.size());
	outBuffer.writeUInt8(m_updateType);
	for (EntityUpdate e : m_updates) {
		if (e.frame.translation[0] != e.frame.translation[0]) {
			debugPrintf("Oops, updated with a nan\n");
		}
		GUniqueID guid = GUniqueID::fromString16(e.name.c_str());
		guid.serialize(outBuffer);
		e.frame.serialize(outBuffer);
		outBuffer.writeUInt8(e.playerID);
		outBuffer.writeUInt32(e.frameNumber);
	}
}

void BatchEntityUpdatePacket::deserialize(BinaryInput& inBuffer) {
	GenericPacket::deserialize(inBuffer);	// Call the super deserialize
	uint8 numEntities = inBuffer.readUInt8();
	m_updateType = (NetworkUpdateType)inBuffer.readUInt8();
	m_updates = Array<EntityUpdate>();
	switch (m_updateType) {
	case REPLACE_FRAME:
		for (int i = 0; i < numEntities; i++) {
			GUniqueID guid;
			guid.deserialize(inBuffer);
			CFrame frame;
			frame.deserialize(inBuffer);
			uint8 playerID;
			playerID = inBuffer.readUInt8();
			uint32 frameNumber;
			frameNumber = inBuffer.readUInt32();
			m_updates.append(EntityUpdate(frame, guid.toString16(), playerID, frameNumber));
		}
	}
}

/************************
 * Create Entity Packet *
 ************************/

void CreateEntityPacket::populate(uint32 frameNumber, GUniqueID guid, uint8 playerID) {
	m_frameNumber = frameNumber;
	m_guid = guid;
	m_playerID = playerID;
}

void CreateEntityPacket::serialize(BinaryOutput& outBuffer) {
	GenericPacket::serialize(outBuffer);	// Call the super serialize
	outBuffer.writeUInt32(m_frameNumber);
	m_guid.serialize(outBuffer);
	outBuffer.writeUInt8(m_playerID);
}

void CreateEntityPacket::deserialize(BinaryInput& inBuffer) {
	GenericPacket::deserialize(inBuffer);	// Call the super deserialize
	m_frameNumber = inBuffer.readUInt32();
	m_guid.deserialize(inBuffer);
	m_playerID = inBuffer.readUInt8();
}

/*************************
 * Destroy Entity Packet *
 *************************/

void DestroyEntityPacket::populate(uint32 frameNumber, GUniqueID guid) {
	m_frameNumber = frameNumber;
	m_guid = guid;
}

void DestroyEntityPacket::serialize(BinaryOutput& outBuffer) {
	GenericPacket::serialize(outBuffer);	// Call the super serialize
	outBuffer.writeUInt32(m_frameNumber);
	m_guid.serialize(outBuffer);
}

void DestroyEntityPacket::deserialize(BinaryInput& inBuffer) {
	GenericPacket::deserialize(inBuffer);	// Call the super deserialize
	m_frameNumber = inBuffer.readUInt32();
	m_guid.deserialize(inBuffer);
}

/**************************
 * Regisger Client Packet *
 **************************/

void RegisterClientPacket::populate(ENetPeer* peer, GUniqueID guid, uint16 portNum, uint16 pingPortNum) {
	m_peer = peer;
	m_guid = guid;
	m_portNum = portNum;
	m_pingPortNum = pingPortNum;
}

void RegisterClientPacket::serialize(BinaryOutput& outBuffer) {
	GenericPacket::serialize(outBuffer);	// Call the super serialize
	m_guid.serialize(outBuffer);
	outBuffer.writeUInt16(m_portNum);
	outBuffer.writeUInt16(m_pingPortNum);
}

void RegisterClientPacket::deserialize(BinaryInput& inBuffer) {
	GenericPacket::deserialize(inBuffer);	// Call the super deserialize
	m_guid.deserialize(inBuffer);
	m_portNum = inBuffer.readUInt16();
	m_pingPortNum = inBuffer.readUInt16();
}

/*****************************
 * Registration Reply Packet *
 *****************************/

void RegistrationReplyPacket::populate(GUniqueID guid, uint8 status, uint8 playerID) {
	m_guid = guid;
	m_status = status;
	m_playerID = playerID;
}

void RegistrationReplyPacket::serialize(BinaryOutput& outBuffer) {
	GenericPacket::serialize(outBuffer);	// Call the super serialize
	m_guid.serialize(outBuffer);
	outBuffer.writeUInt8(m_status);
	outBuffer.writeUInt8(m_playerID);
}

void RegistrationReplyPacket::deserialize(BinaryInput& inBuffer) {
	GenericPacket::deserialize(inBuffer);	// Call the super deserialize
	m_guid.deserialize(inBuffer);
	m_status = inBuffer.readUInt8();
	m_playerID = inBuffer.readUInt8();
}

/********************
 * Handshake Packet *
 ********************/

 /** This packet has no data so serialize does nothing */
void HandshakePacket::serialize(BinaryOutput& outBuffer) {
	GenericPacket::serialize(outBuffer);	// Call the super serialize
}

/** This packet has no data so deserialize does nothing */
void HandshakePacket::deserialize(BinaryInput& inBuffer) {
	GenericPacket::deserialize(inBuffer);	// Call the super deserialize
}

/**************************
 * Handshake Reply Packet *
 **************************/

 /** This packet has no data so serialize does nothing */
void HandshakeReplyPacket::serialize(BinaryOutput& outBuffer) {
	GenericPacket::serialize(outBuffer);	// Call the super serialize
}

/** This packet has no data so deserialize does nothing */
void HandshakeReplyPacket::deserialize(BinaryInput& inBuffer) {
	GenericPacket::deserialize(inBuffer);	// Call the super deserialize
}

/**********************
 * Move Client Packet *
 **********************/

void MoveClientPacket::populate(uint32 frameNumber, CFrame newPosition) {
	m_frameNumber = frameNumber;
	m_newPosition = newPosition;
}

void MoveClientPacket::serialize(BinaryOutput& outBuffer) {
	GenericPacket::serialize(outBuffer);	// Call the super serialize
	outBuffer.writeUInt32(m_frameNumber);
	m_newPosition.serialize(outBuffer);
}

void MoveClientPacket::deserialize(BinaryInput& inBuffer) {
	GenericPacket::deserialize(inBuffer);	// Call the super deserialize
	m_frameNumber = inBuffer.readUInt32();
	m_newPosition.deserialize(inBuffer);
}

/*********************
 * Report Hit Packet *
 *********************/

void ReportHitPacket::populate(uint32 frameNumber, GUniqueID shotID, GUniqueID shooterID) {
	m_frameNumber = frameNumber;
	m_shotID = shotID;
	m_shooterID = shooterID;
}

void ReportHitPacket::serialize(BinaryOutput& outBuffer) {
	GenericPacket::serialize(outBuffer);	// Call the super serialize
	outBuffer.writeUInt32(m_frameNumber);
	m_shotID.serialize(outBuffer);
	m_shooterID.serialize(outBuffer);
}

void ReportHitPacket::deserialize(BinaryInput& inBuffer) {
	GenericPacket::deserialize(inBuffer);	// Call the super deserialize
	m_frameNumber = inBuffer.readUInt32();
	m_shotID.deserialize(inBuffer);
	m_shooterID.deserialize(inBuffer);
}

/*********************
 * Report Fire Packet *
 *********************/

void ReportFirePacket::populate(uint32 frameNumber, bool fired, GUniqueID shooterID) {
	m_frameNumber = frameNumber;
	m_fired = fired;
	m_shooterID = shooterID;
}

void ReportFirePacket::serialize(BinaryOutput& outBuffer) {
	GenericPacket::serialize(outBuffer);	// Call the super serialize
	outBuffer.writeUInt32(m_frameNumber);
	outBuffer.writeBool8(m_fired);
	m_shooterID.serialize(outBuffer);
}

void ReportFirePacket::deserialize(BinaryInput& inBuffer) {
	GenericPacket::deserialize(inBuffer);	// Call the super deserialize
	m_frameNumber = inBuffer.readUInt32();
	m_fired = inBuffer.readBool8();
	m_shooterID.deserialize(inBuffer);
}

/********************
 * Set Spawn Packet *
 ********************/

void SetSpawnPacket::populate(Point3 spawnPositionTranslation, float spawnHeading) {
	m_spawnPositionTranslation = spawnPositionTranslation;
	m_spawnHeading = spawnHeading;
}

void SetSpawnPacket::serialize(BinaryOutput& outBuffer) {
	GenericPacket::serialize(outBuffer);	// Call the super serialize
	m_spawnPositionTranslation.serialize(outBuffer);
	outBuffer.writeFloat32(m_spawnHeading);
}

void SetSpawnPacket::deserialize(BinaryInput& inBuffer) {
	GenericPacket::deserialize(inBuffer);	// Call the super deserialize
	m_spawnPositionTranslation.deserialize(inBuffer);
	m_spawnHeading = inBuffer.readFloat32();
}

/*************************
 * Respawn Client Packet *
 *************************/

void RespawnClientPacket::populate(uint32 frameNumber) {
	m_frameNumber = frameNumber;
}

void RespawnClientPacket::serialize(BinaryOutput& outBuffer) {
	GenericPacket::serialize(outBuffer);	// Call the super serialize
	outBuffer.writeUInt32(m_frameNumber);
}

void RespawnClientPacket::deserialize(BinaryInput& inBuffer) {
	GenericPacket::deserialize(inBuffer);	// Call the super deserialize
	m_frameNumber = inBuffer.readUInt32();
}

/**************************
 * Ready Up Client Packet *
 **************************/

 /** This packet has no data so serialize does nothing */
void ReadyUpClientPacket::serialize(BinaryOutput& outBuffer) {
	GenericPacket::serialize(outBuffer);	// Call the super serialize
}

/** This packet has no data so serialize does nothing */
void ReadyUpClientPacket::deserialize(BinaryInput& inBuffer) {
	GenericPacket::deserialize(inBuffer);	// Call the super deserialize
}

/************************
 * Start Session Packet *
 ************************/

void StartSessionPacket::populate(uint32 frameNumber) {
	m_frameNumber = frameNumber;
}

void StartSessionPacket::serialize(BinaryOutput& outBuffer) {
	GenericPacket::serialize(outBuffer);	// Call the super serialize
	outBuffer.writeUInt32(m_frameNumber);
}

void StartSessionPacket::deserialize(BinaryInput& inBuffer) {
	GenericPacket::deserialize(inBuffer);	// Call the super deserialize
	m_frameNumber = inBuffer.readUInt32();
}

/*************************
 *Player Interact Packet *
 *************************/

void PlayerInteractPacket::populate(uint32 frameNumber, uint8 remoteAction, GUniqueID actorID) {
	m_frameNumber = frameNumber;
	m_remoteAction = remoteAction;
	m_actorID = actorID;
}

void PlayerInteractPacket::serialize(BinaryOutput& outBuffer) {
	GenericPacket::serialize(outBuffer);	// Call the super serialize
	outBuffer.writeUInt32(m_frameNumber);
	outBuffer.writeUInt8(m_remoteAction);
	m_actorID.serialize(outBuffer);
}

void PlayerInteractPacket::deserialize(BinaryInput& inBuffer) {
	GenericPacket::deserialize(inBuffer);	// Call the super deserialize
	m_frameNumber = inBuffer.readUInt32();
	m_remoteAction = inBuffer.readUInt8();
	m_actorID.deserialize(inBuffer);
}

/****************************
 *Send Player Config Packet *
 ****************************/

void SendPlayerConfigPacket::populate(PlayerConfig playerConfig, float sessionProgress) {
	m_playerConfig = createShared<PlayerConfig>(playerConfig);
	m_networkedSessionProgress = sessionProgress;
}

void SendPlayerConfigPacket::serialize(BinaryOutput& outBuffer) {
	GenericPacket::serialize(outBuffer);	// Call the super serialize
	PlayerConfig selectedConfig = *m_playerConfig;
	if ((*m_playerConfig).readFromFile == true) {
		int index = (*m_playerConfig).selectedClientIdx ? index = 0 : index = 1;
		selectedConfig = (*m_playerConfig).clientPlayerConfigs[index];
		(*m_playerConfig).readFromFile = false;
	}
	outBuffer.writeFloat32(selectedConfig.moveRate);
	outBuffer.writeVector2(selectedConfig.moveScale);

	outBuffer.writeBool8((selectedConfig.axisLock)[0]);
	outBuffer.writeBool8((selectedConfig.axisLock)[1]);
	outBuffer.writeBool8((selectedConfig.axisLock)[2]);

	outBuffer.writeBool8(selectedConfig.accelerationEnabled);
	outBuffer.writeFloat32(selectedConfig.movementAcceleration);
	outBuffer.writeFloat32(selectedConfig.movementDeceleration);

	outBuffer.writeFloat32(selectedConfig.sprintMultiplier);

	outBuffer.writeFloat32(selectedConfig.jumpVelocity);
	outBuffer.writeFloat32(selectedConfig.jumpInterval);
	outBuffer.writeBool8(selectedConfig.jumpTouch);

	outBuffer.writeFloat32(selectedConfig.height);
	outBuffer.writeFloat32(selectedConfig.crouchHeight);

	outBuffer.writeBool8(selectedConfig.headBobEnabled);
	outBuffer.writeFloat32(selectedConfig.headBobAmplitude);
	outBuffer.writeFloat32(selectedConfig.headBobFrequency);

	outBuffer.writeVector3(selectedConfig.respawnPos);
	outBuffer.writeBool8(true);
	outBuffer.writeFloat32(selectedConfig.respawnHeading);

	outBuffer.writeFloat32(selectedConfig.movementRestrictionX);
	outBuffer.writeFloat32(selectedConfig.movementRestrictionZ);
	outBuffer.writeBool8(selectedConfig.restrictedMovementEnabled);
	outBuffer.writeFloat32(selectedConfig.restrictionBoxAngle);

	outBuffer.writeBool8(selectedConfig.counterStrafing);

	outBuffer.writeString(selectedConfig.playerType);

	outBuffer.writeFloat32(m_networkedSessionProgress);

	outBuffer.writeFloat32(selectedConfig.clientLatency);

	outBuffer.writeInt32(selectedConfig.placeboPingType);
	outBuffer.writeInt32(selectedConfig.placeboPingModifier);

	outBuffer.writeBool8(selectedConfig.timeWarpEnabled);
}

void SendPlayerConfigPacket::deserialize(BinaryInput& inBuffer) {
	GenericPacket::deserialize(inBuffer);	// Call the super deserialize

	m_playerConfig = createShared<PlayerConfig>();
	m_playerConfig->moveRate = inBuffer.readFloat32();
	m_playerConfig->moveScale = inBuffer.readVector2();

	(m_playerConfig->axisLock)[0] = inBuffer.readBool8();
	(m_playerConfig->axisLock)[1] = inBuffer.readBool8();
	(m_playerConfig->axisLock)[2] = inBuffer.readBool8();

	m_playerConfig->accelerationEnabled = inBuffer.readBool8();
	m_playerConfig->movementAcceleration = inBuffer.readFloat32();
	m_playerConfig->movementDeceleration = inBuffer.readFloat32();

	m_playerConfig->sprintMultiplier = inBuffer.readFloat32();

	m_playerConfig->jumpVelocity = inBuffer.readFloat32();
	m_playerConfig->jumpInterval = inBuffer.readFloat32();
	m_playerConfig->jumpTouch = inBuffer.readBool8();

	m_playerConfig->height = inBuffer.readFloat32();
	m_playerConfig->crouchHeight = inBuffer.readFloat32();

	m_playerConfig->headBobEnabled = inBuffer.readBool8();
	m_playerConfig->headBobAmplitude = inBuffer.readFloat32();
	m_playerConfig->headBobFrequency = inBuffer.readFloat32();

	m_playerConfig->respawnPos = inBuffer.readVector3();
	m_playerConfig->respawnToPos = inBuffer.readBool8();
	m_playerConfig->respawnHeading = inBuffer.readFloat32();

	m_playerConfig->movementRestrictionX = inBuffer.readFloat32();
	m_playerConfig->movementRestrictionZ = inBuffer.readFloat32();
	m_playerConfig->restrictedMovementEnabled = inBuffer.readBool8();
	m_playerConfig->restrictionBoxAngle = inBuffer.readFloat32();

	m_playerConfig->counterStrafing = inBuffer.readBool8();

	m_playerConfig->playerType = inBuffer.readString();

	m_networkedSessionProgress = inBuffer.readFloat32();

	m_playerConfig->clientLatency = inBuffer.readFloat32();

	m_playerConfig->placeboPingType = inBuffer.readInt32();
	m_playerConfig->placeboPingModifier = inBuffer.readInt32();

	m_playerConfig->timeWarpEnabled = inBuffer.readBool8();
}


/********************
 *ADD POINTS PACKET *
 ********************/

void AddPointPacket::serialize(BinaryOutput& outBuffer) {
	GenericPacket::serialize(outBuffer);	// Call the super serialize
}

void AddPointPacket::deserialize(BinaryInput& inBuffer) {
	GenericPacket::deserialize(inBuffer);	// Call the super deserialize
}


/****************************
 *RESET CLIENT ROUND PACKET *
 ****************************/

void ResetClientRoundPacket::serialize(BinaryOutput& outBuffer) {
	GenericPacket::serialize(outBuffer);	// Call the super serialize
}

void ResetClientRoundPacket::deserialize(BinaryInput& inBuffer) {
	GenericPacket::deserialize(inBuffer);	// Call the super deserialize
}

/*******************************
 *CLIENT FEEDBACK START PACKET *
 *******************************/

void ClientFeedbackStartPacket::serialize(BinaryOutput& outBuffer) {
	GenericPacket::serialize(outBuffer);	// Call the super serialize
}

void ClientFeedbackStartPacket::deserialize(BinaryInput& inBuffer) {
	GenericPacket::deserialize(inBuffer);	// Call the super deserialize
}


/****************************
 *CLIENT SESSION END PACKET *
 ****************************/

void ClientSessionEndPacket::serialize(BinaryOutput& outBuffer) {
	GenericPacket::serialize(outBuffer);	// Call the super serialize
}

void ClientSessionEndPacket::deserialize(BinaryInput& inBuffer) {
	GenericPacket::deserialize(inBuffer);	// Call the super deserialize
}


/******************************
 *CLIENT ROUND TIMEOUT PACKET *
 ******************************/

void ClientRoundTimeoutPacket::serialize(BinaryOutput& outBuffer) {
	GenericPacket::serialize(outBuffer);	// Call the super serialize
}

void ClientRoundTimeoutPacket::deserialize(BinaryInput& inBuffer) {
	GenericPacket::deserialize(inBuffer);	// Call the super deserialize
}

/***********************************
 *CLIENT FEEDBACK SUBMITTED PACKET *
 ***********************************/

void ClientFeedbackSubmittedPacket::serialize(BinaryOutput& outBuffer) {
	GenericPacket::serialize(outBuffer);	// Call the super serialize
}

void ClientFeedbackSubmittedPacket::deserialize(BinaryInput& inBuffer) {
	GenericPacket::deserialize(inBuffer);	// Call the super deserialize
}

/**************
 *PING PACKET *
 **************/

void PingPacket::populate(std::chrono::steady_clock::time_point time) {
	m_rttStart = time;
}

void PingPacket::serialize(BinaryOutput& outBuffer) {
	GenericPacket::serialize(outBuffer);	// Call the super serialize
	outBuffer.writeBytes(&m_rttStart, sizeof(std::chrono::steady_clock::time_point));
}

void PingPacket::deserialize(BinaryInput& inBuffer) {
	GenericPacket::deserialize(inBuffer);	// Call the super deserialize
	inBuffer.readBytes((void*)&m_rttStart, sizeof(std::chrono::steady_clock::time_point));
	m_RTT = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_rttStart).count();
}

/*******************
 *PING DATA PACKET *
 *******************/

void PingDataPacket::populate(uint16 rttStats[]) {
	m_latestRTT = rttStats[0];
	m_smaRTT = rttStats[1];
	m_minRTT = rttStats[2];
	m_maxRTT = rttStats[3];
}

void PingDataPacket::serialize(BinaryOutput& outBuffer) {
	GenericPacket::serialize(outBuffer);	// Call the super serialize

	outBuffer.writeUInt16(m_latestRTT);
	outBuffer.writeUInt16(m_smaRTT);
	outBuffer.writeUInt16(m_minRTT);
	outBuffer.writeUInt16(m_maxRTT);
}

void PingDataPacket::deserialize(BinaryInput& inBuffer) {
	GenericPacket::deserialize(inBuffer);	// Call the super deserialize

	m_latestRTT = inBuffer.readUInt16();
	m_smaRTT = inBuffer.readUInt16();
	m_minRTT = inBuffer.readUInt16();
	m_maxRTT = inBuffer.readUInt16();

}

