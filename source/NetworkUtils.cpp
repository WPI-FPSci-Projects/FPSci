#include "NetworkUtils.h"
#include "TargetEntity.h"
#include "FpsConfig.h"
#include <enet/enet.h>
/*
static void updateEntity(Entity entity, BinaryInput inBuffer) {
	NetworkUtils::NetworkUpdateType type = (NetworkUtils::NetworkUpdateType)inBuffer.readUInt8();
}*/
shared_ptr<NetworkedEntity> NetworkUtils::updateEntity(Array <GUniqueID> ignoreIDs, shared_ptr<G3D::Scene> scene, BinaryInput& inBuffer) {
	GUniqueID entity_id;
	entity_id.deserialize(inBuffer);
	shared_ptr<NetworkedEntity> entity = (*scene).typedEntity<NetworkedEntity>(entity_id.toString16());
	if (entity == nullptr && !ignoreIDs.contains(entity_id)) {
		debugPrintf("Recieved update for entity %s, but it doesn't exist\n", entity_id.toString16().c_str());
		return nullptr;
	}
	if (ignoreIDs.contains(entity_id)) { // don't let the server move ignored entities
		entity = nullptr;
	}
	updateEntity(entity, inBuffer); // Allways call this even if the entity is ignored so we remove the data from the BinaryInput
	return entity;
}

shared_ptr<Entity> NetworkUtils::updateEntity(shared_ptr<Entity> entity, BinaryInput& inBuffer) {
	NetworkUtils::NetworkUpdateType type = (NetworkUtils::NetworkUpdateType)inBuffer.readUInt8();
	if (type == NOOP) {
		return entity;
	}
	else if (type == NetworkUpdateType::REPLACE_FRAME) {
		CoordinateFrame* frame = new CoordinateFrame();
		frame->deserialize(inBuffer);
		if (entity != nullptr) {
			entity->setFrame(*frame);
		}
	}
	return entity;
}

void NetworkUtils::createFrameUpdate(GUniqueID id, shared_ptr<Entity> entity, BinaryOutput& outBuffer) {
	id.serialize(outBuffer);
	outBuffer.writeUInt8(NetworkUpdateType::REPLACE_FRAME);
	entity->frame().serialize(outBuffer);
}

void NetworkUtils::handleDestroyEntity(shared_ptr<G3D::Scene> scene, BinaryInput& inBuffer) {
	GUniqueID entity_id;
	entity_id.deserialize(inBuffer);;
	shared_ptr<NetworkedEntity> entity = (*scene).typedEntity<NetworkedEntity>(entity_id.toString16());
	debugPrintf("removing %s...\n", entity_id.toString16());
	if (entity != nullptr) {
		(*scene).remove(entity);
	}
}

void NetworkUtils::broadcastDestroyEntity(GUniqueID id, ENetHost* serverHost, uint32 frameNum) {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::DESTROY_ENTITY);
	outBuffer.writeUInt32(frameNum);
	id.serialize(outBuffer);		// Send the GUID as a byte string

	debugPrintf("sent destroy packet: %s\n", id.toString16());

	ENetPacket* packet = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length(), ENET_PACKET_FLAG_RELIABLE);
	enet_host_broadcast(serverHost, 0, packet);
}

/* 
'PING' type packets are sent and processed outside of the main game thread and don't 
necessarily require frame numbers although they can be added for consistency later 
*/

int NetworkUtils::sendPingClient(ENetSocket socket, ENetAddress address) {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::PING);
	std::chrono::steady_clock::time_point rttStart = std::chrono::steady_clock::now();
	outBuffer.writeBytes(&rttStart, sizeof(std::chrono::steady_clock::time_point));

	ENetBuffer buff;
	buff.data = (void*)outBuffer.getCArray();
	buff.dataLength = outBuffer.length();
	return enet_socket_send(socket, &address, &buff, 1);
}

int NetworkUtils::sendPingReply(ENetSocket socket, ENetAddress address, ENetBuffer* buff) {
	/* Re-send the same data back to the client */
	return enet_socket_send(socket, &address, buff, 1);
}

void NetworkUtils::handlePingReply(BinaryInput& inBuffer, PingStatistics& stats) {
	std::chrono::steady_clock::time_point rttStart;
	inBuffer.readBytes((void*)&rttStart, sizeof(std::chrono::steady_clock::time_point));
	long long rtt = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - rttStart).count();
	stats.pingQueue.pushBack(rtt);

	// Update the simple moving average for RTT
	if (stats.pingQueue.length() > stats.smaRTTSize) {
		stats.pingQueue.popFront();
	}

	long long sum = 0;
	for (int i = 0; i < stats.pingQueue.length(); i++) {
		sum += stats.pingQueue[i];
	}

	if (stats.pingQueue.length() == stats.smaRTTSize) {
		stats.smaPing = sum / stats.smaRTTSize;
	}

	// Check the minimum and maximum recorded RTT values
	if (stats.minPing == -1 || rtt < stats.minPing) {
		stats.minPing = rtt;
	}
	if (rtt > stats.maxPing) {
		stats.maxPing = rtt;
	}

}

int NetworkUtils::sendPingData(ENetSocket socket, ENetAddress address, PingStatistics pingStats) {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::PING_DATA);
	outBuffer.writeUInt32(0); // Ping data is not part of integral game tick packets (null frame number)

	auto truncateToUInt16 = [](long long val) {
		return val >= UINT16_MAX ? UINT16_MAX : (uint16) val;
	};

	outBuffer.writeUInt16(truncateToUInt16(pingStats.pingQueue.last()));
	outBuffer.writeUInt16(truncateToUInt16(pingStats.smaPing));
	outBuffer.writeUInt16(truncateToUInt16(pingStats.minPing));
	outBuffer.writeUInt16(truncateToUInt16(pingStats.maxPing));

	ENetBuffer buff;
	buff.data = (void*)outBuffer.getCArray();
	buff.dataLength = outBuffer.length();
	return enet_socket_send(socket, &address, &buff, 1);
}

int NetworkUtils::sendHitReport(GUniqueID shot_id, GUniqueID shooter_id, ENetPeer* serverPeer, uint32 frameNum) {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D::G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::REPORT_HIT);
	outBuffer.writeUInt32(frameNum);
	shot_id.serialize(outBuffer);
	shooter_id.serialize(outBuffer);
	ENetPacket* packet = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length(), ENET_PACKET_FLAG_RELIABLE);
	return enet_peer_send(serverPeer, 0, packet);
}
GUniqueID NetworkUtils::handleHitReport(ENetHost* serverHost, BinaryInput& inBuffer, uint32 frameNum) {
	GUniqueID hit_entity, shooter;
	hit_entity.deserialize(inBuffer);
	shooter.deserialize(inBuffer);
	debugPrintf("HIT REPORTED: %s SHOT %s WITH THE CANDLESTICK IN THE LIBRARY\n", shooter.toString16(), hit_entity.toString16());
	//NetworkUtils::broadcastRespawn(serverHost, frameNum);
	return hit_entity;
}

int NetworkUtils::sendPlayerInteract(RemotePlayerAction remoteAction, ENetSocket sendSocket, ENetAddress destAddr, uint32 frameNum)
{
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D::G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::PLAYER_INTERACT);
	outBuffer.writeUInt32(frameNum);
	outBuffer.writeUInt8(remoteAction.actionType);
	remoteAction.guid.serialize(outBuffer);
	
	ENetBuffer buff;
	buff.data = (void*)outBuffer.getCArray();
	buff.dataLength = outBuffer.length();
	return enet_socket_send(sendSocket, &destAddr, &buff, 1);
}

NetworkUtils::RemotePlayerAction NetworkUtils::handlePlayerInteractServer(ENetSocket sendSocket, Array<ConnectedClient*> clients, BinaryInput& inBuffer, uint32 frameNum)
{
	uint8 action = inBuffer.readUInt8();
	GUniqueID guid;
	guid.deserialize(inBuffer);
	RemotePlayerAction remoteAction = RemotePlayerAction(guid, action);
	// Tell all the clients about this shot
	for (ConnectedClient* client : clients) {
		sendPlayerInteract(remoteAction, sendSocket, client->unreliableAddress, frameNum);
	}
	return remoteAction;
}

NetworkUtils::RemotePlayerAction NetworkUtils::handlePlayerInteractClient(BinaryInput& inBuffer) {
	RemotePlayerAction remoteAction;
	remoteAction.actionType = (PlayerActionType)inBuffer.readUInt8();
	remoteAction.guid.deserialize(inBuffer);
	return remoteAction;
}

int NetworkUtils::sendMoveClient(CFrame frame, ENetPeer* peer, uint32 frameNum) {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D::G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MOVE_CLIENT);
	outBuffer.writeUInt32(frameNum);
	outBuffer.writeUInt8(NetworkUpdateType::REPLACE_FRAME);
	frame.serialize(outBuffer);
	ENetPacket* packet = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length(), ENET_PACKET_FLAG_RELIABLE);
	return enet_peer_send(peer, 0, packet);
}

int NetworkUtils::sendHandshake(ENetSocket socket, ENetAddress address) {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::HANDSHAKE);
	outBuffer.writeUInt32(0);	// Dummy frame num
	ENetBuffer hs_buff;
	hs_buff.data = (void*)outBuffer.getCArray();
	hs_buff.dataLength = outBuffer.length();
	return enet_socket_send(socket, &address, &hs_buff, 1);
}

int NetworkUtils::sendHandshakeReply(ENetSocket socket, ENetAddress address) {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D::G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::HANDSHAKE_REPLY);
	outBuffer.writeUInt32(0);	// Dummy frame num
	ENetBuffer buff;
	buff.data = (void*)outBuffer.getCArray();
	buff.dataLength = outBuffer.length();
	return enet_socket_send(socket, &address, &buff, 1);
}

NetworkUtils::ConnectedClient* NetworkUtils::registerClient(ENetEvent event, BinaryInput& inBuffer, uint32 frameNum) {
	/* get the clients information and create a ConnectedClient struct */
	ConnectedClient* newClient = new ConnectedClient();
	newClient->peer = event.peer;
	GUniqueID clientGUID;
	clientGUID.deserialize(inBuffer);
	newClient->guid = clientGUID;
	ENetAddress addr;
	addr.host = event.peer->address.host;
	addr.port = inBuffer.readUInt16();   // Set the port to what the client sends to us because we might loose the UDP handshake packet
	newClient->unreliableAddress = addr;
	debugPrintf("\tPort: %i\n", addr.port);
	debugPrintf("\tHost: %i\n", addr.host);
	/* Create Reply to the client */
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::CLIENT_REGISTRATION_REPLY);
	outBuffer.writeUInt32(frameNum);	// Dummy frame num
	clientGUID.serialize(outBuffer);		// Send the GUID as a byte string to the client in confirmation
	outBuffer.writeUInt8(0);
	ENetPacket* replyPacket = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length(), ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(event.peer, 0, replyPacket);
	return newClient;
}

int NetworkUtils::sendRegisterClient(GUniqueID id, uint16 port, ENetPeer* peer) {
	ENetPacket* registerPacket;
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::REGISTER_CLIENT);
	outBuffer.writeUInt32(0);	// Dummy frame num
	id.serialize(outBuffer);		// Send the GUID as a byte string to the server so it can identify the client

	outBuffer.writeUInt16(port); // Client socket port

	registerPacket = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length(), ENET_PACKET_FLAG_RELIABLE);
	return enet_peer_send(peer, 0, registerPacket);
}

void NetworkUtils::broadcastCreateEntity(GUniqueID guid, ENetHost* serverHost, uint32 frameNum) {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::CREATE_ENTITY);
	outBuffer.writeUInt32(frameNum);
	guid.serialize(outBuffer);		// Send the GUID as a byte string to the server so it can identify the client

	ENetPacket* packet = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length(), ENET_PACKET_FLAG_RELIABLE);
	// update the other peers with new connection
	enet_host_broadcast(serverHost, 0, packet);
}

int NetworkUtils::sendCreateEntity(GUniqueID guid, ENetPeer* peer, uint32 frameNum) {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::CREATE_ENTITY);
	outBuffer.writeUInt32(frameNum);
	guid.serialize(outBuffer);		// Send the GUID as a byte string to the server so it can identify the client
	ENetPacket* packet = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length(), ENET_PACKET_FLAG_RELIABLE);
	return enet_peer_send(peer, 0, packet);
}

void NetworkUtils::broadcastBatchEntityUpdate(Array<shared_ptr<Entity>> entities, Array<ENetAddress> destinations, ENetSocket sendSocket, uint32 frameNum) {
	/* Setup the packet */
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::BATCH_ENTITY_UPDATE);
	outBuffer.writeUInt32(frameNum);
	outBuffer.writeUInt8(entities.size());
	/* Add the GUID and CFrame of each entity to the packet */
	for (shared_ptr<Entity> e : entities)
	{
		if (e->frame().translation[0] != e->frame().translation[0]) {
			debugPrintf("Oops, updated with a nan\n");
		}
		GUniqueID guid = GUniqueID::fromString16((*e).name().c_str());
		NetworkUtils::createFrameUpdate(guid, e, outBuffer);
	}
	ENetBuffer enet_buff;
	enet_buff.data = (void*)outBuffer.getCArray();
	enet_buff.dataLength = outBuffer.length();
	/* Send the packet to all clients over the unreliable connection */
	for (int i = 0; i < destinations.length(); i++) {
		ENetAddress toAddress = destinations[i];
		if (enet_socket_send(sendSocket, &toAddress, &enet_buff, 1) <= 0) {
			logPrintf("Failed to send at least one packet to a client");
		}
	}
}

void NetworkUtils::serverBatchEntityUpdate(Array<shared_ptr<NetworkedEntity>> entities, Array<ConnectedClient*> clients, ENetSocket sendSocket, uint32 frameNum) {
	Array<shared_ptr<Entity>> genericEntities;
	for (shared_ptr<NetworkedEntity> e : entities) {
		genericEntities.append((shared_ptr<Entity>) e);
	}
	Array<ENetAddress> addresses;
	for (ConnectedClient* client : clients) {
		addresses.append(client->unreliableAddress);
	}
	NetworkUtils::broadcastBatchEntityUpdate(genericEntities, addresses, sendSocket, frameNum);
}

int NetworkUtils::sendSetSpawnPos(G3D::Point3 position, float heading, ENetPeer* peer) {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::SET_SPAWN_LOCATION);
	outBuffer.writeUInt32(0);	// Dummy frame num
	position.serialize(outBuffer);
	outBuffer.writeFloat32(heading);
	ENetPacket* packet = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length(), ENET_PACKET_FLAG_RELIABLE);
	return enet_peer_send(peer, 0, packet);
}

void NetworkUtils::handleSetSpawnPos(shared_ptr<PlayerEntity> player, BinaryInput& inBuffer) {
	Point3 position;
	float heading;
	position.deserialize(inBuffer);
	heading = inBuffer.readFloat32();
	player->setRespawnPosition(position);
	player->setRespawnHeadingDegrees(heading);
}

int NetworkUtils::sendRespawnClient(ENetPeer* clientPeer, uint32 frameNum) {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::RESPAWN_CLIENT);
	outBuffer.writeUInt32(frameNum);
	ENetPacket* packet = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length(), ENET_PACKET_FLAG_RELIABLE);
	return enet_peer_send(clientPeer, 0, packet);
}

void NetworkUtils::broadcastRespawn(ENetHost* serverHost, uint32 frameNum) {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::RESPAWN_CLIENT);
	outBuffer.writeUInt32(frameNum);
	ENetPacket* packet = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length(), ENET_PACKET_FLAG_RELIABLE);
	enet_host_broadcast(serverHost, 0, packet);
}

int NetworkUtils::sendReadyUpMessage(ENetPeer* serverPeer) {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D::G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::READY_UP_CLIENT);
	outBuffer.writeUInt32(0);	// Dummy frame num (haven't started a trial yet)
	ENetPacket* packet = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length(), ENET_PACKET_FLAG_RELIABLE);
	return enet_peer_send(serverPeer, 0, packet);
}

void NetworkUtils::broadcastStartSession(ENetHost* serverHost, uint32 frameNum) {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::START_NETWORKED_SESSION);
	outBuffer.writeUInt32(frameNum);	// this is where we sync and reset the client frame numbers
	ENetPacket* packet = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length(), ENET_PACKET_FLAG_RELIABLE);
	enet_host_broadcast(serverHost, 0, packet);
}

int NetworkUtils::sendPlayerConfigToClient(ENetHost* serverHost, ENetPeer* clientPeer, PlayerConfig *playerConfig, bool broadcast) {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::SEND_PLAYER_CONFIG_TO_CLIENTS);
	outBuffer.writeUInt16(0);	// Dummy frame num (haven't started a trial yet)
	PlayerConfig selectedConfig = *playerConfig;

	if ((*playerConfig).readFromFile == true) {
		int index = (*playerConfig).selectedClientIdx ? index = 0 : index = 1;
		selectedConfig = (*playerConfig).clientPlayerConfigs[index];
		(*playerConfig).readFromFile = false;
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

	outBuffer.writeFloat32(selectedConfig.movementRestrictionX);
	outBuffer.writeFloat32(selectedConfig.movementRestrictionZ);
	outBuffer.writeBool8(selectedConfig.restrictedMovementEnabled);

	outBuffer.writeBool8(selectedConfig.counterStrafing);

	ENetPacket* packet = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length(), ENET_PACKET_FLAG_RELIABLE);
	if(!broadcast)
		return enet_peer_send(clientPeer, 0, packet);
	enet_host_broadcast(serverHost, 0, packet);
	return 0;
}
