#pragma once
#include <G3D/G3D.h>
#include <enet/enet.h>
#include <map>
#include "TargetEntity.h"
#include "PlayerEntity.h"
#include "Packet.h"
#include "DataHandler.h"
#include "Weapon.h"


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


struct ENetAddressCompare {
	bool operator()(const ENetAddress& a, const ENetAddress& b) const {
		return ((((long)a.host) << 16) + a.port) < ((((long)b.host) << 16) + b.port);
	}
};


class NetworkUtils
{
public:

	// Struct containing all the data needed to keep track of and comunicate with clients
	struct ConnectedClient {
		ENetPeer* peer;
		GUniqueID guid;
		ENetAddress unreliableAddress;
		uint32 frameNumber;
		uint8 playerID;
		shared_ptr<NetworkedEntity> entity;
		shared_ptr<Weapon> weapon;
		shared_ptr<Camera> camera;
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
	
	static int sendRegisterClient(GUniqueID id, uint16 port, ENetPeer* peer);
	static ConnectedClient registerClient(ENetEvent event, BinaryInput& inBuffer, uint8 playerID);
	static void serverBatchEntityUpdate(Array<shared_ptr<NetworkedEntity>> entities, Array<ConnectedClient> clients, ENetSocket sendSocket, uint16 frameNum);
	static int sendSetSpawnPos(G3D::Point3 position, float heading, ENetPeer* peer);
	static void handleSetSpawnPos(shared_ptr<PlayerEntity> player, BinaryInput& inBuffer);
	static int sendRespawnClient(ENetPeer* peer, uint16 frameNum);
	static void broadcastRespawn(ENetHost* serverHost, uint16 frameNum);
	
	static int sendReadyUpMessage(ENetPeer* serverPeer);
	static void broadcastStartSession(ENetHost* serverHost);
	
	
	static shared_ptr<GenericPacket> createTypedPacket(PacketType type, ENetAddress srcAddr, BinaryInput& inBuffer, ENetEvent* event = NULL);
	static shared_ptr<GenericPacket> receivePacket(ENetHost* host, ENetSocket* socket);
	static shared_ptr<GenericPacket> receivePing(ENetSocket* pingSocket);

	static void broadcastReliable(shared_ptr<GenericPacket> packet, ENetHost* localHost);
	static void broadcastUnreliable(shared_ptr<GenericPacket> packet, ENetSocket* srcSocket, Array<ENetAddress*> addresses);

	static void setAddressLatency(ENetAddress addr, int latency);
	static void removeAddressLatency(ENetAddress addr);
	static void setDefaultLatency(int latency);
	static void send(shared_ptr<GenericPacket> packet);

protected:
	static void sendPacketDelayed(shared_ptr<GenericPacket> packet, int delay);
	static int defaultLatency;
	static std::map<ENetAddress, int, ENetAddressCompare> latencyMap;
};
