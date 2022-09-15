#pragma once
#include <G3D/G3D.h>
#include <enet/enet.h>
#include "TargetEntity.h"
#include "PlayerEntity.h"

/*
			PACKET STRUCTURE:
			UInt8: type
			UInt32: Frame Number
			...

			Type BATCH_ENTITY_UPDATE:
			UInt8: type (BATCH_ENTITY_UPDATE)
			UInt16: Frame Number
			UInt8: object_count # number of frames contained in this packet
			<DATA> * n: Opaque view of data, written to and from with NetworkUtils
			[DEPRECATED] GUID * n: ID of object
			[DEPRECATED] * n: CFrames of objects

			Type CREATE_ENTITY:
			UInt8: type (CREATE_ENTITY)
			UInt16: Frame Number
			GUID: object ID
			Uint8: Entity Type
			... : misc. data for constructor, dependent on Entity type

			Type DESTROY_ENTITY:
			UInt8: type (DESTROY_ENTITY)
			UInt16: Frame Number
			GUID: object ID

			Type REGISTER_CLIENT:
			UInt8: type (REGISTER_CLIENT)
			UInt16: Frame Number
			GUID: player's ID
			UInt16: client's port
			? String: player metadata

			Type CLIENT_REGISTRATION_REPLY:
			UInt8: type (CLIENT_REGISTRATION_REPLY)
			UInt16: Frame Number
			GUID: player's ID
			UInt8: status [0 = success, 1 = Failure, ....]


			Type HANDSHAKE
			UInt8: type (HANDSHAKE)
			UInt16: Frame Number

			Type HANDSHAKE_REPLY
			UInt8: type (HANDSHAKE_REPLY)
			UInt16: Frame Number

			Type MOVE_CLIENT:
			UInt 8: type (MOVE_CLIENT)
			UInt16: Frame Number
			CFrame: new position


			Type REPORT_HIT:
			UInt8: type (REPORT_HIT)
			UInt16: Frame Number
			GUID: entity shot
			GUID: shooter
			...

			Type NOTIFY_HIT:
			UInt8: type (NOTIFY_HIT)
			UInt16: Frame Number
			GUID: entity shot
			GUID: shot by
			...

			Type SET_SPAWN_LOCATION:
			UInt8: type (SET_SPAWN_LOCATION)
			UInt16: Frame Number
			Point3: Position translation
			float: Spawned Heading (Radians)

			Type RESPAWN_CLIENT:
			UInt8: type (RESPAWN_CLIENT)
			UInt16: Frame Number
			...

			Type READY_UP_CLIENT:
			UInt8: type (READY_UP_CLIENT)
			UInt16: Frame Number

			Type START_NETWORKED_SESSION:
			UInt8: type (START_NETWORKED_SESSION)
			UInt16: Frame Number

*/

class NetworkUtils
{
public:
	enum MessageType {
		CONTROL_MESSAGE,
		UPDATE_MESSAGE,

		BATCH_ENTITY_UPDATE,
		CREATE_ENTITY,
		DESTROY_ENTITY,
		MOVE_CLIENT,


		REGISTER_CLIENT,
		CLIENT_REGISTRATION_REPLY,

		HANDSHAKE,
		HANDSHAKE_REPLY,

		REPORT_HIT,
		NOTIFY_HIT,

		SET_SPAWN_LOCATION,
		RESPAWN_CLIENT,

		READY_UP_CLIENT,
		START_NETWORKED_SESSION
	};

	enum NetworkUpdateType {
		NOOP,
		REPLACE_FRAME,
	};

	// Struct containing all the data needed to keep track of and comunicate with clients
	struct ConnectedClient {
		ENetPeer* peer;
		GUniqueID guid;
		ENetAddress unreliableAddress;
		int frameNumber;
	};

	static void updateEntity(Array <GUniqueID> ignoreIDs, shared_ptr<G3D::Scene> scene, BinaryInput& inBuffer);
	static void updateEntity(shared_ptr<Entity> entity, BinaryInput& inBuffer);
	static void createFrameUpdate(GUniqueID id, shared_ptr<Entity> entity, BinaryOutput& outBuffer);

	static void handleDestroyEntity(shared_ptr<G3D::Scene> scene, BinaryInput& inBuffer);
	static void broadcastDestroyEntity(GUniqueID id, ENetHost* serverHost, uint32 frameNum);

	static int sendHitReport(GUniqueID shot_id, GUniqueID shooter_id, ENetPeer* serverPeer, uint32 frameNum);
	static void handleHitReport(ENetHost* serverHost, BinaryInput& inBuffer, uint32 frameNum);

	static int sendMoveClient(CFrame frame, ENetPeer* peer, uint32 frameNum);
	static int sendHandshakeReply(ENetSocket socket, ENetAddress address);
	static int sendHandshake(ENetSocket socket, ENetAddress address);
	static int sendRegisterClient(GUniqueID id, uint32 port, ENetPeer* peer);
	static ConnectedClient registerClient(ENetEvent event, BinaryInput& inBuffer);
	static void broadcastCreateEntity(GUniqueID id, ENetHost* serverHost, uint32 frameNum);
	static int sendCreateEntity(GUniqueID guid, ENetPeer* peer, uint32 frameNum);
	static void broadcastBatchEntityUpdate(Array<shared_ptr<Entity>> entities, Array<ENetAddress> destinations, ENetSocket sendSocket, uint32 frameNum);
	static void serverBatchEntityUpdate(Array<shared_ptr<NetworkedEntity>> entities, Array<ConnectedClient> clients, ENetSocket sendSocket, uint32 frameNum);
	static int sendSetSpawnPos(G3D::Point3 position, float heading, ENetPeer* peer);
	static void handleSetSpawnPos(shared_ptr<PlayerEntity> player, BinaryInput& inBuffer);
	static int sendRespawnClient(ENetPeer* peer, uint32 frameNum);
	static void broadcastRespawn(ENetHost* serverHost, uint32 frameNum);
	
	static int sendReadyUpMessage(ENetPeer* serverPeer);
	static void broadcastStartSession(ENetHost* serverHost);
};