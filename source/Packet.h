#pragma once
#include <G3D/G3D.h>
#include <enet/enet.h>
#include "TargetEntity.h"

enum PacketType {
	BATCH_ENTITY_UPDATE,
	CREATE_ENTITY,
	DESTROY_ENTITY,
	MOVE_CLIENT,


	REGISTER_CLIENT,
	CLIENT_REGISTRATION_REPLY,

	HANDSHAKE,
	HANDSHAKE_REPLY,

	REPORT_HIT,

	SET_SPAWN_LOCATION,
	RESPAWN_CLIENT,

	READY_UP_CLIENT,
	START_NETWORKED_SESSION,

	PLAYER_INTERACT,

	RELIABLE_CONNECT,
	RELIABLE_DISCONNECT
};

class GenericPacket : public ReferenceCountedObject {

protected:
	GenericPacket();
	/* Constructor for receiving a packet */
	GenericPacket(ENetAddress srcAddr, BinaryInput &inBuffer);
	/* Constructor for seinding over the reliable channel to the peer */
	GenericPacket(ENetPeer* destPeer);
	/* Constructor for sending over the unreliable channel to the destAddr */
	GenericPacket(ENetSocket* srcSocket, ENetAddress* destAddr);

public:
	template <class packetType>
	static shared_ptr<packetType> createForBroadcast() {
		return createShared<packetType>();
	}
	template <class packetType>
	static shared_ptr<packetType> createReceive(ENetAddress srcAddr, BinaryInput& inBuffer) {
		return createShared<packetType>(srcAddr, inBuffer);
	}
	template <class packetType>
	static shared_ptr<packetType> createUnreliable(ENetSocket* srcSocket, ENetAddress* destAddr) {
		return createShared<packetType>(srcSocket, destAddr);
	}
	template <class packetType>
	static shared_ptr<packetType> createReliable(ENetPeer* destPeer) {
		return createShared<packetType>(destPeer);
	}

protected:
	virtual void serialize(BinaryOutput &outBuffer);		///> serialize the data in this packet
	virtual void deserialize(BinaryInput &inBuffer);		///> deserialize the data in this packet
	
	bool m_reliable;						// which channel to send; also determines which ENet fields are defined
	ENetPeer* m_destPeer;					// reliable
	ENetSocket* m_srcSocket;				// unreliable
	ENetAddress* m_destAddr;				// unreliable

	PacketType m_type;

	ENetAddress m_srcAddr;					// Only used on inbound packets for the address of the sender
	bool m_inbound = false;					// indicates if the packet is inbound or outbound

public:
	int send();					///> sends the packet over the network

	void setReliableDest(ENetPeer* peer) { 
		m_destPeer = peer;
		m_reliable = true;
	}
	void setUnreliableDest(ENetSocket* srcSocket, ENetAddress* destAddr) {
		m_srcSocket = srcSocket;
		m_destAddr = destAddr;
		m_reliable = false;
	}

	bool reliable() { return m_reliable; }
	ENetAddress srcAddr() { return m_srcAddr; }
	bool inbound() { return m_inbound; }

	virtual PacketType type() { return m_type; };				///> tells the type of packet this is
	virtual GenericPacket* getTypedPacket() { return this; }	///> Returns *this* as the most narrow type possble
};


class BatchEntityUpdatePacket : public GenericPacket {
protected:
	BatchEntityUpdatePacket() : GenericPacket() {}
	BatchEntityUpdatePacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr, inBuffer) {}
	BatchEntityUpdatePacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	BatchEntityUpdatePacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}
public:
	struct EntityUpdate {
		CFrame frame;
		String name;
		EntityUpdate(CFrame entityFrame, String entityName) {
			frame = entityFrame;
			name = entityName;
		}
		EntityUpdate() {}
	};
	enum NetworkUpdateType {
		NOOP,
		REPLACE_FRAME,
	};

	PacketType type() override { return BATCH_ENTITY_UPDATE; }
	BatchEntityUpdatePacket* getTypedPacket() override { return this; }

	void populate(uint32 frameNumber, Array<shared_ptr<Entity>> entities, NetworkUpdateType updateType);
	void populate(uint32 frameNumber, Array<shared_ptr<NetworkedEntity>> entities, NetworkUpdateType updateType);

protected:
	void serialize(BinaryOutput &outBuffer) override;
	void deserialize(BinaryInput &inBuffer) override;
	

public:
	Array<EntityUpdate> m_updates;
	uint32 m_frameNumber;
	NetworkUpdateType m_updateType;
};


class CreateEntityPacket : public GenericPacket {
protected:
	CreateEntityPacket() : GenericPacket() {}
	CreateEntityPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr, inBuffer) {}
	CreateEntityPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	CreateEntityPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}
	
public:
	PacketType type() override { return CREATE_ENTITY; }
	CreateEntityPacket* getTypedPacket() override { return this; }

	void populate(uint32 frameNumber, GUniqueID guid);

	uint32 m_frameNumber;
	GUniqueID m_guid;

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};


class DestroyEntityPacket : public GenericPacket {
protected:
	DestroyEntityPacket() : GenericPacket() {}
	DestroyEntityPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr, inBuffer) {}
	DestroyEntityPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	DestroyEntityPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return CREATE_ENTITY; }
	DestroyEntityPacket* getTypedPacket() override { return this; }

	void populate(uint32 frameNumber, GUniqueID guid);

	uint32 m_frameNumber;
	GUniqueID m_guid;

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};


class RegisterClientPacket : public GenericPacket {
protected:
	RegisterClientPacket() : GenericPacket() {}
	RegisterClientPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr, inBuffer) {}
	RegisterClientPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	RegisterClientPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}
	
public:
	PacketType type() override { return REGISTER_CLIENT; }
	RegisterClientPacket* getTypedPacket() override { return this; }

	void populate(ENetPeer* peer, GUniqueID guid, uint16 portNum);

	ENetPeer* m_peer;
	uint16 m_portNum;
	GUniqueID m_guid;

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};


class RegistrationReplyPacket : public GenericPacket {
protected:
	RegistrationReplyPacket() : GenericPacket() {}
	RegistrationReplyPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr, inBuffer) {}
	RegistrationReplyPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	RegistrationReplyPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return CLIENT_REGISTRATION_REPLY; }
	RegistrationReplyPacket* getTypedPacket() override { return this; }

	void populate(GUniqueID guid, uint8 status);

	GUniqueID m_guid;
	uint8 m_status;

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};


class HandshakePacket : public GenericPacket {
protected:
	HandshakePacket() : GenericPacket() {}
	HandshakePacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr, inBuffer) {}
	HandshakePacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	HandshakePacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return HANDSHAKE; }
	HandshakePacket* getTypedPacket() override { return this; }

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};

/* TODO: Combine this with just a handshake packet or Registration reply*/
class HandshakeReplyPacket : public GenericPacket {
protected:
	HandshakeReplyPacket() : GenericPacket() {}
	HandshakeReplyPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr, inBuffer) {}
	HandshakeReplyPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	HandshakeReplyPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return HANDSHAKE_REPLY; }
	HandshakeReplyPacket* getTypedPacket() override { return this; }

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};


class MoveClientPacket : public GenericPacket {
protected:
	MoveClientPacket() : GenericPacket() {}
	MoveClientPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr, inBuffer) {}
	MoveClientPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	MoveClientPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return MOVE_CLIENT; }
	MoveClientPacket* getTypedPacket() override { return this; }

	void populate(uint32 frameNumber, CFrame newPosition);

	uint32 m_frameNumber;
	CFrame m_newPosition;

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};


class ReportHitPacket : public GenericPacket {
protected:
	ReportHitPacket() : GenericPacket() {}
	ReportHitPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr, inBuffer) {}
	ReportHitPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	ReportHitPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return REPORT_HIT; }
	ReportHitPacket* getTypedPacket() override { return this; }

	void populate(uint32 frameNumber, GUniqueID shotID, GUniqueID shooterID);

	uint32 m_frameNumber;
	GUniqueID m_shotID;
	GUniqueID m_shooterID;

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};


class SetSpawnPacket : public GenericPacket {
protected:
	SetSpawnPacket() : GenericPacket() {}
	SetSpawnPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr, inBuffer) {}
	SetSpawnPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	SetSpawnPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return SET_SPAWN_LOCATION; }
	SetSpawnPacket* getTypedPacket() override { return this; }

	void populate(Point3 spawnPositionTranslation, float spawnHeading);

	Point3 m_spawnPositionTranslation;
	float m_spawnHeading;

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};


class RespawnClientPacket : public GenericPacket {
protected:
	RespawnClientPacket() : GenericPacket() {}
	RespawnClientPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr, inBuffer) {}
	RespawnClientPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	RespawnClientPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return RESPAWN_CLIENT; }
	RespawnClientPacket* getTypedPacket() override { return this; }

	void populate(uint32 frameNumber = 0);

	uint32 m_frameNumber;

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};


class ReadyUpClientPacket : public GenericPacket {
protected:
	ReadyUpClientPacket() : GenericPacket() {}
	ReadyUpClientPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr, inBuffer) {}
	ReadyUpClientPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	ReadyUpClientPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return READY_UP_CLIENT; }
	ReadyUpClientPacket* getTypedPacket() override { return this; }

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};


class StartSessionPacket : public GenericPacket {
protected:
	StartSessionPacket() : GenericPacket() {}
	StartSessionPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr, inBuffer) {}
	StartSessionPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	StartSessionPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return START_NETWORKED_SESSION; }
	StartSessionPacket* getTypedPacket() override { return this; }

	void populate(uint32 frameNumber);

	uint32 m_frameNumber;

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};


class PlayerInteractPacket : public GenericPacket {
protected:
	PlayerInteractPacket() : GenericPacket() {}
	PlayerInteractPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr, inBuffer) {}
	PlayerInteractPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	PlayerInteractPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return START_NETWORKED_SESSION; }
	PlayerInteractPacket* getTypedPacket() override { return this; }

	void populate(uint32 frameNumber, uint8 remoteAction, GUniqueID actorID);

	uint32 m_frameNumber;
	uint8 m_remoteAction;
	GUniqueID m_actorID;

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};

/*	
 *	The following 2 packets cannot be sent over the network but exist to 
 *	comunicate different events reported by the reliable enet peer
 */
class ReliableConnectPacket : public GenericPacket {
protected:
	ReliableConnectPacket(ENetAddress srcAddr) : GenericPacket() {
		m_type = RELIABLE_CONNECT;
		m_srcAddr = srcAddr;
		m_inbound = true;
		m_reliable = true;
	}

public:
	static shared_ptr<ReliableConnectPacket> createReceive(ENetAddress srcAddr) {
		return createShared<ReliableConnectPacket>(srcAddr);
	}

	PacketType type() override { return RELIABLE_CONNECT; }
	ReliableConnectPacket* getTypedPacket() override { return this; }

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};


class ReliableDisconnectPacket : public GenericPacket {
protected:
	ReliableDisconnectPacket(ENetAddress srcAddr) : GenericPacket() { 
		m_type = RELIABLE_DISCONNECT; 
		m_srcAddr = srcAddr;
		m_inbound = true;
		m_reliable = true;
	}

public:
	static shared_ptr<ReliableDisconnectPacket> createReceive(ENetAddress srcAddr) {
		return createShared<ReliableDisconnectPacket>(srcAddr);
	}

	PacketType type() override { return RELIABLE_DISCONNECT; }
	ReliableDisconnectPacket* getTypedPacket() override { return this; }

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};