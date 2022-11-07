#pragma once
#include <G3D/G3D.h>
#include <enet/enet.h>
#include "TargetEntity.h"
#include "FPSConfig.h"

enum PacketType {
	UNINTALIZED_TYPE,

	BATCH_ENTITY_UPDATE,
	CREATE_ENTITY,
	DESTROY_ENTITY,
	MOVE_CLIENT,


	REGISTER_CLIENT,
	CLIENT_REGISTRATION_REPLY,

	HANDSHAKE,
	HANDSHAKE_REPLY,

	REPORT_HIT,
	REPORT_FIRE,

	SET_SPAWN_LOCATION,
	RESPAWN_CLIENT,

	PING,
	PING_DATA,

	READY_UP_CLIENT,
	START_NETWORKED_SESSION,

	PLAYER_INTERACT,

	SEND_PLAYER_CONFIG,
	ADD_POINTS,
	RESET_CLIENT_ROUND,
	CLIENT_FEEDBACK_START,
	CLIENT_SESSION_END,
	CLIENT_ROUND_TIMEOUT,
	CLIENT_FEEDBACK_SUBMITTED,

	RELIABLE_CONNECT,			///< Packet type to represent an enet event type connect
	RELIABLE_DISCONNECT			///< Packet type to represent an enet event type disconnect
};

/** A Generic Packet type that contains only the type of packet and basic information for sending/receiving a packet
*
* This class serves as the base class for all more specific typed packets and
* provides basic functionality such as the packet type, source/desitnation
* information, sending a packet over the network, and creating more
* specifically typed packets.
*
* In order to add a new packet you will need to add the 4 constructors (no
* args, srcAddr and inBuffer, destPeer, and srcSocket and destAddr), a type
* method a getTypedPacket method, a populate method (and memeber variables),
* and a serialize/deserialize method. Existing packets can be used as an
* example for how to implement these functions and what they need for
* implementation.
*/

class GenericPacket : public ReferenceCountedObject {


protected:
	/** Contructor for creating a packet without any sending information (used for broadcasting) */
	GenericPacket();
	/** Constructor for creating a packet wtithout reading its type */
	GenericPacket(ENetAddress srcAddr);
	/** Constructor for receiving a packet */
	GenericPacket(ENetAddress srcAddr, BinaryInput& inBuffer);
	/** Constructor for seinding over the reliable channel to the peer */
	GenericPacket(ENetPeer* destPeer);
	/** Constructor for sending over the unreliable channel to the destAddr */
	GenericPacket(ENetSocket* srcSocket, ENetAddress* destAddr);

public:
	/** Create a packet without any destination information so it can be broadcast
	*
	* This function will also create more typed packets which can be done by
	* calling createForBroadcast<SpecificPacketClass>
	*/
	template <class packetType>
	static shared_ptr<packetType> createForBroadcast() {
		return createShared<packetType>();
	}
	/** Create a packet who's data is parsed from the BinaryInput
	*
	* This function will also create more typed packets which can be done by
	* calling createReceive<SpecificPacketClass>
	*/
	template <class packetType>
	static shared_ptr<packetType> createReceive(ENetAddress srcAddr, BinaryInput& inBuffer) {
		return createShared<packetType>(srcAddr, inBuffer);
	}
	/** Create an empty packet to be sent over the unreliable channel
	*
	* This will NOT inialize member varibales for the specificly typed packet
	* you must call populate on the returned object before sending
	*
	* This function will also create more typed packets which can be done by
	* calling createUnreliable<SpecificPacketClass>()
	*/
	template <class packetType>
	static shared_ptr<packetType> createUnreliable(ENetSocket* srcSocket, ENetAddress* destAddr) {
		return createShared<packetType>(srcSocket, destAddr);
	}
	/** Create a packet to be sent over the reliable channel
	*
	* This will NOT inialize member varibales for specificly typed packets
	* you must call populate on the returned object before sending
	*
	* This function will also create more typed packets which can be done by
	* calling createForBroadcast<SpecificPacketClass>()
	*/
	template <class packetType>
	static shared_ptr<packetType> createReliable(ENetPeer* destPeer) {
		return createShared<packetType>(destPeer);
	}

	/** Sets the reliable destination to the peer and marks packet as reliable */
	void setReliableDest(ENetPeer* peer) {
		m_destPeer = peer;
		m_reliable = true;
	}
	/** Sets the unreliable destination to the destAddr and marks packet as unreliable */
	void setUnreliableDest(ENetSocket* srcSocket, ENetAddress* destAddr) {
		m_srcSocket = srcSocket;
		m_destAddr = destAddr;
		m_reliable = false;
	}

	const ENetAddress* getDestinationAddress() {
		if (m_reliable) {
			return &m_destPeer->address;
		}
		else {
			return m_destAddr;
		}
	}

	/** Returns whether the packet was sent/received on the reliable channel */
	bool isReliable() { return m_reliable; }
	/** Returns the source address (only used on inbound packets */
	ENetAddress srcAddr() { return m_srcAddr; }
	/** Returns whether the packet is inbound */
	bool isInbound() { return m_inbound; }

	/** Returns the type of packet that this is (used to know how to handle a packet) */
	virtual PacketType type() { return m_type; };
	/** Returns the *this* (used to clone a packet when broadcasting) */
	virtual shared_ptr<GenericPacket> clone() { return createShared<GenericPacket>(*this); }

	/** Sends the packet over the network on the correct channel */
	int send();

	bool m_reliable;									///< which channel to send/was received on; also determines which ENet fields are defined

protected:
	virtual void serialize(BinaryOutput& outBuffer);	///< serialize the data in this packet
	virtual void deserialize(BinaryInput& inBuffer);	///< deserialize the data in this packet

	ENetPeer* m_destPeer;								///< reliable destination
	ENetSocket* m_srcSocket;							///< unreliable socket to send on
	ENetAddress* m_destAddr;							///< unreliable destination for the packet

	ENetAddress m_srcAddr;								///< Only used on inbound packets for the address of the sender
	bool m_inbound = false;								///< indicates if the packet is inbound or outbound (also determines if m_srcAddr is initalized)

private:
	PacketType m_type = UNINTALIZED_TYPE;				///< Type of packet
};

/** A Packet contining updates for multiple entities at once
*
* This packet allows the server or client to send a group of updates across the
* network. It also supports multiple different update types which are recorded
* in m_updateType. Implementation of these updates lies in the FPSciApp or
* FPSciServerApp
*/
class BatchEntityUpdatePacket : public GenericPacket {
protected:
	BatchEntityUpdatePacket() : GenericPacket() {}
	BatchEntityUpdatePacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr) { this->deserialize(inBuffer); }
	BatchEntityUpdatePacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	BatchEntityUpdatePacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}
public:
	/** Struct containing all information needed to update a single entity */
	struct EntityUpdate {
		CFrame frame;								///< CFrame to use in the update
		String name;								///< Name of the entity this update applies to
		uint8 playerID;								///< playerID of the entity this update applies to
		/** Constructor to create and populate an update */
		EntityUpdate(CFrame entityFrame, String entityName, uint8 PID) {
			frame = entityFrame;
			name = entityName;
			playerID = PID;
		}
		EntityUpdate() {}
	};
	/** Indicates what type of update this is (changes how updates are applied) */
	enum NetworkUpdateType {
		NOOP,										///< Do Nothing (No-Op)
		REPLACE_FRAME,								///< Replace the frame with the one in the update (absolute position)
	};

	PacketType type() override { return BATCH_ENTITY_UPDATE; }
	shared_ptr<GenericPacket> clone() override { return createShared<BatchEntityUpdatePacket>(*this); }

	/** Fills in the member varibales from the parameters (Must be called prior to calling send()) */
	void populate(uint32 frameNumber, Array<EntityUpdate> updates, NetworkUpdateType updateType);

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;


public:
	Array<EntityUpdate> m_updates;					///< Array of updates to be applied
	uint32 m_frameNumber;							///< Frame number that these updates apply to
	NetworkUpdateType m_updateType;					///< type of update (how to apply updates)
};

/** A Packet signaling the receiver to create a new entity
*
* This packet creates a new entity on the receiver on the frame number
* indicated and names it based on the GUID included in the packet
*/
class CreateEntityPacket : public GenericPacket {
protected:
	CreateEntityPacket() : GenericPacket() {}
	CreateEntityPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr) { this->deserialize(inBuffer); }
	CreateEntityPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	CreateEntityPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return CREATE_ENTITY; }
	shared_ptr<GenericPacket> clone() override { return createShared<CreateEntityPacket>(*this); }

	/** Fills in the member variables from the parameters (Must be called prior to calling send()) */
	void populate(uint32 frameNumber, GUniqueID guid, uint8 playerID);

	uint32 m_frameNumber;							///< Frame number that the entity was created on
	GUniqueID m_guid;								///< GUID of the new entity (used as Entity->name)
	uint8 m_playerID;								///< playerID of the new NetworkedEntity

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};

/** A Packet signaling the receiver to remove an entity
*
* This packet removes the entity with the name of the GUID included in the
* packet
*/
class DestroyEntityPacket : public GenericPacket {
protected:
	DestroyEntityPacket() : GenericPacket() {}
	DestroyEntityPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr) { this->deserialize(inBuffer); }
	DestroyEntityPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	DestroyEntityPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return DESTROY_ENTITY; }
	shared_ptr<GenericPacket> clone() override { return createShared<DestroyEntityPacket>(*this); }

	/** Fills in the member varibales from the parameters (Must be called prior to calling send()) */
	void populate(uint32 frameNumber, GUniqueID guid);

	uint32 m_frameNumber;							///< Frame number that the entity was destroyed on
	GUniqueID m_guid;								///< GUID of the destroyed entity

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};

/** A Packet to register a new client with the server
*
* This packet includes all the information needed for the server to create a
* new ConnectedClient so the server knows how to contact the client over both
* the reliable and unreliable channel
*/
class RegisterClientPacket : public GenericPacket {
protected:
	RegisterClientPacket() : GenericPacket() {}
	RegisterClientPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr) { this->deserialize(inBuffer); }
	RegisterClientPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	RegisterClientPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return REGISTER_CLIENT; }
	shared_ptr<GenericPacket> clone() override { return createShared<RegisterClientPacket>(*this); }

	/** Fills in the member varibales from the parameters (Must be called prior to calling send()) */
	void populate(ENetPeer* peer, GUniqueID guid, uint16 portNum);

	ENetPeer* m_peer;								///< Peer for the connecting client
	uint16 m_portNum;								///< Port number the client is listening on for the unreliable channel
	GUniqueID m_guid;								///< GUID of the client (Same as the name of the NetworkedTarget entity that represents them)

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};

/** A Packet telling the client if their registration was a success
*
* This packet indicates to the client if their registration request was
* successful or not. It also includes the GUID to ensure the server has the
* correct GUID recorded for this client
*/
class RegistrationReplyPacket : public GenericPacket {
protected:
	RegistrationReplyPacket() : GenericPacket() {}
	RegistrationReplyPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr) { this->deserialize(inBuffer); }
	RegistrationReplyPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	RegistrationReplyPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return CLIENT_REGISTRATION_REPLY; }
	shared_ptr<GenericPacket> clone() override { return createShared<RegistrationReplyPacket>(*this); }

	/** Fills in the member varibales from the parameters (Must be called prior to calling send()) */
	void populate(GUniqueID guid, uint8 status, uint8 playerID);

	GUniqueID m_guid;								///< GUID of the newly connected client
	uint8 m_status;									///< Status code of the connection (0 is success)
	uint8 m_playerID;								///< playerID of the newly connected client

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};

/** A Packet used to ensure that the unreliable channel is working
*
* This packet contains no information but is sent over the unreliable channel
* to enusre that the server is listening and can respond on the unreliable
* channel. Becuase this has no data it has no populate method.
*/
class HandshakePacket : public GenericPacket {
protected:
	HandshakePacket() : GenericPacket() {}
	HandshakePacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr) { this->deserialize(inBuffer); }
	HandshakePacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	HandshakePacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return HANDSHAKE; }
	shared_ptr<GenericPacket> clone() override { return createShared<HandshakePacket>(*this); }

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};

/* TODO: Combine this with just a handshake packet or Registration reply*/
/** The same thing as a Handshake packet but sent back to the client
*
* This packet is identical to the Handshake packet but is sent from the server
*/
class HandshakeReplyPacket : public GenericPacket {
protected:
	HandshakeReplyPacket() : GenericPacket() {}
	HandshakeReplyPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr) { this->deserialize(inBuffer); }
	HandshakeReplyPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	HandshakeReplyPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return HANDSHAKE_REPLY; }
	shared_ptr<GenericPacket> clone() override { return createShared<HandshakeReplyPacket>(*this); }

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};

/** A Packet that forces the receiveing client to go to a specific location
*
* This packet forces the receiving client to overwrite the frame of thier
* PlayerEntity with the frame included in this packet
*/
class MoveClientPacket : public GenericPacket {
protected:
	MoveClientPacket() : GenericPacket() {}
	MoveClientPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr) { this->deserialize(inBuffer); }
	MoveClientPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	MoveClientPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return MOVE_CLIENT; }
	shared_ptr<GenericPacket> clone() override { return createShared<MoveClientPacket>(*this); }

	/** Fills in the member varibales from the parameters (Must be called prior to calling send()) */
	void populate(uint32 frameNumber, CFrame newPosition);

	uint32 m_frameNumber;							///< Frame Number that this client was moved
	CFrame m_newPosition;							///< New position for the client (absolute position)

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};

/** A Packet used to signal that a client has shot another entity
*
* This packet tells the server that a client (m_shooterID) has shot at and hit
* another client (m_shotID) on the frame number indicated
*/
class ReportHitPacket : public GenericPacket {
protected:
	ReportHitPacket() : GenericPacket() {}
	ReportHitPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr) { this->deserialize(inBuffer); }
	ReportHitPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	ReportHitPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return REPORT_HIT; }
	shared_ptr<GenericPacket> clone() override { return static_pointer_cast<GenericPacket> (createShared<ReportHitPacket>(*this)); }

	/** Fills in the member varibales from the parameters (Must be called prior to calling send()) */
	void populate(uint32 frameNumber, GUniqueID shotID, GUniqueID shooterID);

	uint32 m_frameNumber;							///< Frame Number that the hit occured
	GUniqueID m_shotID;								///< GUID of the client that was hit
	GUniqueID m_shooterID;							///< GUID of the client that fired the shot

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};

/** A Packet used to signal that a client has fired
*
* This packet tells the server that a client (m_shooterID) has shot
*/
class ReportFirePacket : public GenericPacket {
protected:
	ReportFirePacket() : GenericPacket() {}
	ReportFirePacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr) { this->deserialize(inBuffer); }
	ReportFirePacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	ReportFirePacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return REPORT_FIRE; }
	shared_ptr<GenericPacket> clone() override { return createShared<ReportFirePacket>(*this); }

	/** Fills in the member varibales from the parameters (Must be called prior to calling send()) */
	void populate(uint32 frameNumber, bool m_fired, GUniqueID m_shooterID);

	uint32 m_frameNumber;							///< Frame number that this action took place
	bool m_fired;									///< Whether the player has fired
	GUniqueID m_shooterID;							///< GUID of the client that fired the shot

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};

/** A Packet used to set the spawn positon of the receiving client
*
* This packet updates the spawn location and heading of the receiving client
* but does not cause them to respawn to that position
*/
class SetSpawnPacket : public GenericPacket {
protected:
	SetSpawnPacket() : GenericPacket() {}
	SetSpawnPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr) { this->deserialize(inBuffer); }
	SetSpawnPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	SetSpawnPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return SET_SPAWN_LOCATION; }
	shared_ptr<GenericPacket> clone() override { return createShared<SetSpawnPacket>(*this); }

	/** Fills in the member varibales from the parameters (Must be called prior to calling send()) */
	void populate(Point3 spawnPositionTranslation, float spawnHeading);

	Point3 m_spawnPositionTranslation;				///< Translation component of new spawn location
	float m_spawnHeading;							///< Rotation component of new spawn location (radians)

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};

/** A Packet telling the receiving client to respawn
*
* This packet causes the receiving client to respawn by calling the respawn
* method in their PlayerEntity
*/
class RespawnClientPacket : public GenericPacket {
protected:
	RespawnClientPacket() : GenericPacket() {}
	RespawnClientPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr) { this->deserialize(inBuffer); }
	RespawnClientPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	RespawnClientPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return RESPAWN_CLIENT; }
	shared_ptr<GenericPacket> clone() override { return createShared<RespawnClientPacket>(*this); }

	/** Fills in the member varibales from the parameters (Must be called prior to calling send()) */
	void populate(uint32 frameNumber = 0);

	uint32 m_frameNumber;							///< Frame number that the client was respawned

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};

/** A Packet telling the server that a client is ready
*
* This packet has no data but tells the server that the client is ready for the
* trial to begin. Because this has no data there is no populate method
*/
class ReadyUpClientPacket : public GenericPacket {
protected:
	ReadyUpClientPacket() : GenericPacket() {}
	ReadyUpClientPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr) { this->deserialize(inBuffer); }
	ReadyUpClientPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	ReadyUpClientPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return READY_UP_CLIENT; }
	shared_ptr<GenericPacket> clone() override { return createShared<ReadyUpClientPacket>(*this); }

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};

/** A Packet telling the clients to start the trial and syncs frame numbers
*
* This packet indicates that there are enough players ready for the trial to
* begin and sets the client frame number to the frane number incuded in this
* packet so that frame numbers are roughly syncronized
*/
class StartSessionPacket : public GenericPacket {
protected:
	StartSessionPacket() : GenericPacket() {}
	StartSessionPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr) { this->deserialize(inBuffer); }
	StartSessionPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	StartSessionPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return START_NETWORKED_SESSION; }
	shared_ptr<GenericPacket> clone() override { return createShared<StartSessionPacket>(*this); }

	/** Fills in the member varibales from the parameters (Must be called prior to calling send()) */
	void populate(uint32 frameNumber);

	uint32 m_frameNumber;							///< Frame number of the server (used to sync frame numbers)

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};

/** A Packet telling the receiver about remote actions that happened
*
* This packet is used to tell the receiver about an action that has happened
* on a remote client. This is mostly used for logging these types of actions
*/
class PlayerInteractPacket : public GenericPacket {
protected:
	PlayerInteractPacket() : GenericPacket() {}
	PlayerInteractPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr) { this->deserialize(inBuffer); }
	PlayerInteractPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	PlayerInteractPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return PLAYER_INTERACT; }
	shared_ptr<GenericPacket> clone() override { return createShared<PlayerInteractPacket>(*this); }

	/** Fills in the member varibales from the parameters (Must be called prior to calling send()) */
	void populate(uint32 frameNumber, uint8 remoteAction, GUniqueID actorID);

	uint32 m_frameNumber;							///< Frame number that this action took place
	uint8 m_remoteAction;							///< Type of action (PlayerActionType)
	GUniqueID m_actorID;							///< GUID of the client who did this action

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};

/** A Packet for sending the client a Player Config
*
* This packet is used to send the client a player config so that the server
* can control the settings of a remote client for experiments
*/
class SendPlayerConfigPacket : public GenericPacket {
protected:
	SendPlayerConfigPacket() : GenericPacket() {}
	SendPlayerConfigPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr) { this->deserialize(inBuffer); }
	SendPlayerConfigPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	SendPlayerConfigPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return SEND_PLAYER_CONFIG; }
	shared_ptr<GenericPacket> clone() override { return createShared<SendPlayerConfigPacket>(*this); }

	/** Fills in the member varibales from the parameters (Must be called prior to calling send()) */
	void populate(PlayerConfig playerConfig, float sessionProgress);

	shared_ptr<PlayerConfig> m_playerConfig;					///< playerConfig to be sent
	float m_networkedSessionProgress;							///< Networked Session progression to be sent

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};

/** TODO: Add comment here for packet description

*/

class AddPointPacket : public GenericPacket {
protected:
	AddPointPacket() : GenericPacket() {}
	AddPointPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr) { this->deserialize(inBuffer); }
	AddPointPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	AddPointPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return ADD_POINTS; }
	shared_ptr<GenericPacket> clone() override { return createShared<AddPointPacket>(*this); }

	/** This Packet has no content and as such does not have a populate method */

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};

/** TODO: Add comment here for packet description

*/

class ResetClientRoundPacket : public GenericPacket {
protected:
	ResetClientRoundPacket() : GenericPacket() {}
	ResetClientRoundPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr) { this->deserialize(inBuffer); }
	ResetClientRoundPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	ResetClientRoundPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return RESET_CLIENT_ROUND; }
	shared_ptr<GenericPacket> clone() override { return createShared<ResetClientRoundPacket>(*this); }

	/** This Packet has no content and as such does not have a populate method */

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};


/** TODO: Add comment here for packet description

*/

class ClientFeedbackStartPacket : public GenericPacket {
protected:
	ClientFeedbackStartPacket() : GenericPacket() {}
	ClientFeedbackStartPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr) { this->deserialize(inBuffer); }
	ClientFeedbackStartPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	ClientFeedbackStartPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return CLIENT_FEEDBACK_START; }
	shared_ptr<GenericPacket> clone() override { return createShared<ClientFeedbackStartPacket>(*this); }

	/** This Packet has no content and as such does not have a populate method */

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};


/** TODO: Add comment here for packet description

*/

class ClientSessionEndPacket : public GenericPacket {
protected:
	ClientSessionEndPacket() : GenericPacket() {}
	ClientSessionEndPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr) { this->deserialize(inBuffer); }
	ClientSessionEndPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	ClientSessionEndPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return CLIENT_SESSION_END; }
	shared_ptr<GenericPacket> clone() override { return createShared<ClientSessionEndPacket>(*this); }

	/** This Packet has no content and as such does not have a populate method */

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};


/** TODO: Add comment here for packet description

*/

class ClientRoundTimeoutPacket : public GenericPacket {
protected:
	ClientRoundTimeoutPacket() : GenericPacket() {}
	ClientRoundTimeoutPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr) { this->deserialize(inBuffer); }
	ClientRoundTimeoutPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	ClientRoundTimeoutPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return CLIENT_ROUND_TIMEOUT; }
	shared_ptr<GenericPacket> clone() override { return createShared<ClientRoundTimeoutPacket>(*this); }

	/** This Packet has no content and as such does not have a populate method */

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};


/** TODO: Add comment here for packet description

*/

class ClientFeedbackSubmittedPacket : public GenericPacket {
protected:
	ClientFeedbackSubmittedPacket() : GenericPacket() {}
	ClientFeedbackSubmittedPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr) { this->deserialize(inBuffer); }
	ClientFeedbackSubmittedPacket(ENetPeer* destPeer) : GenericPacket(destPeer) {}
	ClientFeedbackSubmittedPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return CLIENT_FEEDBACK_SUBMITTED; }
	shared_ptr<GenericPacket> clone() override { return createShared<ClientFeedbackSubmittedPacket>(*this); }

	/** This Packet has no content and as such does not have a populate method */

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};



/** A Packet representing an incoming connection on the reliable channel
*
* This packet cannot be sent over the network but insted represents an
* ENetEvent that is generated when a client connects so that it can be handled
* like a normal packet
*/
class ReliableConnectPacket : public GenericPacket {
protected:
	ReliableConnectPacket(ENetAddress srcAddr) : GenericPacket() {
		m_srcAddr = srcAddr;
		m_inbound = true;
		m_reliable = true;
	}

public:
	/** Creates a typed packet given just source address becasuse there isn't a binary input */
	static shared_ptr<ReliableConnectPacket> createReceive(ENetAddress srcAddr) {
		return createShared<ReliableConnectPacket>(srcAddr);
	}

	PacketType type() override { return RELIABLE_CONNECT; }
	shared_ptr<GenericPacket> clone() override { return createShared<ReliableConnectPacket>(*this); }
};

/** A Packet indicating that a client has disconnected from the reliable connection
*
* This packet cannot be sent over the network but instead represents an
* ENetEvent that is generated when a client disconnects so that it can be
* handled like a normal packet
*/
class ReliableDisconnectPacket : public GenericPacket {
protected:
	ReliableDisconnectPacket(ENetAddress srcAddr) : GenericPacket() {
		m_srcAddr = srcAddr;
		m_inbound = true;
		m_reliable = true;
	}

public:
	/** Creates a typed packet given just source address becasuse there isn't a binary input */
	static shared_ptr<ReliableDisconnectPacket> createReceive(ENetAddress srcAddr) {
		return createShared<ReliableDisconnectPacket>(srcAddr);
	}

	PacketType type() override { return RELIABLE_DISCONNECT; }
	shared_ptr<GenericPacket> clone() override { return createShared<ReliableDisconnectPacket>(*this); }
};

/** A Packet containing information for calculating the RTT or ping of the sending client
* 
* Contains the client's current system timestamp 
*/
class PingPacket : public GenericPacket {
protected:
	PingPacket() : GenericPacket() {}
	PingPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr) { this->deserialize(inBuffer); }
	PingPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return PING; }
	shared_ptr<GenericPacket> clone() override { return createShared<PingPacket>(*this); }

	void populate(std::chrono::steady_clock::time_point time);

	std::chrono::steady_clock::time_point m_rttStart;						/// RTT start time

	// Allocate only in deserialization
	long long m_RTT;														/// RTT value after deserialization

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;
};

/** A server-bound Packet containing current information on a client's ping statistics
* 
*/
class PingDataPacket : public GenericPacket {
protected:
	PingDataPacket() : GenericPacket() {}
	PingDataPacket(ENetAddress srcAddr, BinaryInput& inBuffer) : GenericPacket(srcAddr) { this->deserialize(inBuffer); }
	PingDataPacket(ENetSocket* srcSocket, ENetAddress* destAddr) : GenericPacket(srcSocket, destAddr) {}

public:
	PacketType type() override { return PING_DATA; }
	shared_ptr<GenericPacket> clone() override { return createShared<PingDataPacket>(*this); }

	void populate(uint16 rttStats[]);

	uint16 m_latestRTT;												/// Latest recorded RTT
	uint16 m_smaRTT;												/// Simple Moving Average of RTT
	uint16 m_minRTT;												/// Minimum recorded RTT
	uint16 m_maxRTT;												/// Maximum recorded RTT

protected:
	void serialize(BinaryOutput& outBuffer) override;
	void deserialize(BinaryInput& inBuffer) override;

};