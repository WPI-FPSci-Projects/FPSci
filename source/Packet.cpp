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

void GenericPacket::serialize(BinaryOutput &outBuffer) {
	outBuffer.writeUInt8(this->type());
}

void GenericPacket::deserialize(BinaryInput &inBuffer) {
	m_type = (PacketType) inBuffer.readUInt8();
}


/******************************
 * Batch Entity Udpate Packet *
 ******************************/

void BatchEntityUpdatePacket::populate(uint32 frameNumber, Array<EntityUpdate> updates, NetworkUpdateType updateType) {
	m_updates = updates;
	m_frameNumber = frameNumber;
	m_updateType = updateType;
}

void BatchEntityUpdatePacket::serialize(BinaryOutput& outBuffer) {
	GenericPacket::serialize(outBuffer);	// Call the super serialize
	outBuffer.writeUInt32(m_frameNumber);
	outBuffer.writeUInt8(m_updates.size());
	outBuffer.writeUInt8(m_updateType);
	for (EntityUpdate e : m_updates) {
		if (e.frame.translation[0] != e.frame.translation[0]) {
			debugPrintf("Oops, updated with a nan\n");
		}
		GUniqueID guid = GUniqueID::fromString16(e.name.c_str());
		guid.serialize(outBuffer);
		e.frame.serialize(outBuffer);
	}
}

void BatchEntityUpdatePacket::deserialize(BinaryInput& inBuffer) {
	GenericPacket::deserialize(inBuffer);	// Call the super deserialize
	m_frameNumber = inBuffer.readUInt32();
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
			m_updates.append(EntityUpdate(frame, guid.toString16()));
		}
	}
}

/************************
 * Create Entity Packet *
 ************************/

void CreateEntityPacket::populate(uint32 frameNumber, GUniqueID guid) {
	m_frameNumber = frameNumber;
	m_guid = guid;
}

void CreateEntityPacket::serialize(BinaryOutput &outBuffer) {
	GenericPacket::serialize(outBuffer);	// Call the super serialize
	outBuffer.writeUInt32(m_frameNumber);
	m_guid.serialize(outBuffer);
}

void CreateEntityPacket::deserialize(BinaryInput& inBuffer) {
	GenericPacket::deserialize(inBuffer);	// Call the super deserialize
	m_frameNumber = inBuffer.readUInt32();
	m_guid.deserialize(inBuffer);
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

void RegisterClientPacket::populate(ENetPeer* peer, GUniqueID guid, uint16 portNum) {
	m_peer = peer;
	m_guid = guid;
	m_portNum = portNum;
}

void RegisterClientPacket::serialize(BinaryOutput& outBuffer) {
	GenericPacket::serialize(outBuffer);	// Call the super serialize
	m_guid.serialize(outBuffer);
	outBuffer.writeUInt16(m_portNum);
}

void RegisterClientPacket::deserialize(BinaryInput& inBuffer) {
	GenericPacket::deserialize(inBuffer);	// Call the super deserialize
	m_guid.deserialize(inBuffer);
	m_portNum = inBuffer.readUInt16();
}

/*****************************
 * Registration Reply Packet *
 *****************************/

void RegistrationReplyPacket::populate(GUniqueID guid, uint8 status) {
	m_guid = guid;
	m_status = status;
}

void RegistrationReplyPacket::serialize(BinaryOutput& outBuffer) {
	GenericPacket::serialize(outBuffer);	// Call the super serialize
	m_guid.serialize(outBuffer);
	outBuffer.writeUInt8(m_status);
}

void RegistrationReplyPacket::deserialize(BinaryInput& inBuffer) {
	GenericPacket::deserialize(inBuffer);	// Call the super deserialize
	m_guid.deserialize(inBuffer);
	m_status = inBuffer.readUInt8();
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