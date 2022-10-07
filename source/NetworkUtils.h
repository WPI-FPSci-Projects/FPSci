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
			uint32: Frame Number
			UInt8: object_count # number of frames contained in this packet
			<DATA> * n: Opaque view of data, written to and from with NetworkUtils
			[DEPRECATED] GUID * n: ID of object
			[DEPRECATED] * n: CFrames of objects

			Type CREATE_ENTITY:
			UInt8: type (CREATE_ENTITY)
			uint32: Frame Number
			GUID: object ID
			Uint8: Entity Type
			... : misc. data for constructor, dependent on Entity type

			Type DESTROY_ENTITY:
			UInt8: type (DESTROY_ENTITY)
			uint32: Frame Number
			GUID: object ID

			Type REGISTER_CLIENT:
			UInt8: type (REGISTER_CLIENT)
			uint32: Frame Number
			GUID: player's ID
			UInt16: client's port
			? String: player metadata

			Type CLIENT_REGISTRATION_REPLY:
			UInt8: type (CLIENT_REGISTRATION_REPLY)
			uint32: Frame Number
			GUID: player's ID
			UInt8: status [0 = success, 1 = Failure, ....]


			Type HANDSHAKE
			UInt8: type (HANDSHAKE)
			uint32: Frame Number

			Type HANDSHAKE_REPLY
			UInt8: type (HANDSHAKE_REPLY)
			uint32: Frame Number

			Type MOVE_CLIENT:
			UInt 8: type (MOVE_CLIENT)
			uint32: Frame Number
			CFrame: new position


			Type REPORT_HIT:
			UInt8: type (REPORT_HIT)
			uint32: Frame Number
			GUID: entity shot
			GUID: shooter
			...

			Type NOTIFY_HIT:
			UInt8: type (NOTIFY_HIT)
			uint32: Frame Number
			GUID: entity shot
			GUID: shot by
			...

			Type SET_SPAWN_LOCATION:
			UInt8: type (SET_SPAWN_LOCATION)
			uint32: Frame Number
			Point3: Position translation
			float: Spawned Heading (Radians)

			Type RESPAWN_CLIENT:
			UInt8: type (RESPAWN_CLIENT)
			uint32: Frame Number
			...

			Type PING:
			UInt8: type (PING)
			std::chrono::steady_clock::time_point: Current system time point

			Type: PING_DATA:
			UInt8: type (PING_DATA)
			UInt32: Frame Number
			UInt16: capped latest RTT
			UInt16: capped SMA of RTT
			UInt16: capped minimum recorded RTT
			UInt16: capped maximum recorded RTT

			Type READY_UP_CLIENT:
			UInt8: type (READY_UP_CLIENT)
			uint32: Frame Number

			Type START_NETWORKED_SESSION:
			UInt8: type (START_NETWORKED_SESSION)
			uint32: Frame Number

			Type PLAYER_INTERACT:
			uint8: type (PLAYER_INTERACT)
			uint32: frame number
			uint8: action type
			GUID: Player

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

		PING,
		PING_DATA,

		READY_UP_CLIENT,
		START_NETWORKED_SESSION,

		PLAYER_INTERACT,

		SEND_PLAYER_CONFIG_TO_CLIENTS
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
		uint32 frameNumber;
	};

	enum PlayerActionType;
	struct RemotePlayerAction {
		GUniqueID guid;
		PlayerActionType actionType;

		RemotePlayerAction() {};

		RemotePlayerAction(uint8 action) {
			actionType = (PlayerActionType)action;
		}

		RemotePlayerAction(GUniqueID id, uint8 action) {
			debugPrintf("Created RemotePlayerAction with values (%s, %d)\n", id.toString16(), action);
			guid = id;
			actionType = (PlayerActionType)action;
		}
	};

	// Struct for storing ping statistics
	struct PingStatistics {
		// Moving Average size (configurable)
		int smaRTTSize = 5;
		// Queue of currently recorded RTTs
		Queue<long long> pingQueue;
		// Stored Simple Moving Average of pings
		long long smaPing = 0;
		// Maximum Recorded RTT
		long long maxPing = 0;
		// Minimum Recorded RTT
		long long minPing = -1;
	};

	static shared_ptr<NetworkedEntity> updateEntity(Array <GUniqueID> ignoreIDs, shared_ptr<G3D::Scene> scene, BinaryInput& inBuffer);
	static shared_ptr<Entity> updateEntity(shared_ptr<Entity> entity, BinaryInput& inBuffer);
	static void createFrameUpdate(GUniqueID id, shared_ptr<Entity> entity, BinaryOutput& outBuffer);

	static void handleDestroyEntity(shared_ptr<G3D::Scene> scene, BinaryInput& inBuffer);
	static void broadcastDestroyEntity(GUniqueID id, ENetHost* serverHost, uint32 frameNum);

	static int sendHitReport(GUniqueID shot_id, GUniqueID shooter_id, ENetPeer* serverPeer, uint32 frameNum);
	static GUniqueID handleHitReport(ENetHost* serverHost, BinaryInput& inBuffer, uint32 frameNum);

	static int sendPlayerInteract(RemotePlayerAction remoteAction, ENetSocket sendSocket, ENetAddress destAddr, uint32 frameNum);
	static RemotePlayerAction handlePlayerInteractServer(ENetSocket sendSocket, Array<ConnectedClient*> clients, BinaryInput& inBuffer, uint32 frameNum);
	static RemotePlayerAction handlePlayerInteractClient(BinaryInput& inBuffer);

	static int sendPingClient(ENetSocket socket, ENetAddress address);
	static int sendPingReply(ENetSocket socket, ENetAddress address, ENetBuffer* buff);
	static void handlePingReply(BinaryInput& inBuffer, PingStatistics& stats);

	static int sendPingData(ENetSocket socket, ENetAddress address, PingStatistics pingStats);


	static int sendMoveClient(CFrame frame, ENetPeer* peer, uint32 frameNum);
	static int sendHandshakeReply(ENetSocket socket, ENetAddress address);
	static int sendHandshake(ENetSocket socket, ENetAddress address);
	static int sendRegisterClient(GUniqueID id, uint16 port, ENetPeer* peer);
	static ConnectedClient* registerClient(ENetEvent event, BinaryInput& inBuffer, uint32 frameNum);
	static void broadcastCreateEntity(GUniqueID id, ENetHost* serverHost, uint32 frameNum);
	static int sendCreateEntity(GUniqueID guid, ENetPeer* peer, uint32 frameNum);
	static void broadcastBatchEntityUpdate(Array<shared_ptr<Entity>> entities, Array<ENetAddress> destinations, ENetSocket sendSocket, uint32 frameNum);
	static void serverBatchEntityUpdate(Array<shared_ptr<NetworkedEntity>> entities, Array<ConnectedClient*> clients, ENetSocket sendSocket, uint32 frameNum);
	static int sendSetSpawnPos(G3D::Point3 position, float heading, ENetPeer* peer);
	static void handleSetSpawnPos(shared_ptr<PlayerEntity> player, BinaryInput& inBuffer);
	static int sendRespawnClient(ENetPeer* peer, uint32 frameNum);
	static void broadcastRespawn(ENetHost* serverHost, uint32 frameNum);
	
	static int sendReadyUpMessage(ENetPeer* serverPeer);
	static void broadcastStartSession(ENetHost* serverHost, uint32 frameNum);
	static int sendPlayerConfigToClient(ENetHost* serverHost, ENetPeer* peer, PlayerConfig* playerConfig, bool broadcast);
};