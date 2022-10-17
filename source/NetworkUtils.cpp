#include "NetworkUtils.h"
#include "TargetEntity.h"
#include "FpsConfig.h"
#include <enet/enet.h>
/*
static void updateEntity(Entity entity, BinaryInput inBuffer) {
	NetworkUtils::NetworkUpdateType type = (NetworkUtils::NetworkUpdateType)inBuffer.readUInt8();
}*/

void NetworkUtils::updateEntity(Array <GUniqueID> ignoreIDs, shared_ptr<G3D::Scene> scene, BinaryInput& inBuffer) {
	GUniqueID entity_id;
	entity_id.deserialize(inBuffer);;
	shared_ptr<NetworkedEntity> entity = (*scene).typedEntity<NetworkedEntity>(entity_id.toString16());
	if (entity == nullptr && !ignoreIDs.contains(entity_id)) {
		debugPrintf("Recieved update for entity %s, but it doesn't exist\n", entity_id.toString16().c_str());
	}
	if (ignoreIDs.contains(entity_id)) { // don't let the server move ignored entities
		entity = nullptr;
	}
	updateEntity(entity, inBuffer); // Allways call this even if the entity is ignored so we remove the data from the BinaryInput

}

void NetworkUtils::updateEntity(shared_ptr<Entity> entity, BinaryInput& inBuffer) {
	NetworkUtils::NetworkUpdateType type = (NetworkUtils::NetworkUpdateType)inBuffer.readUInt8();
	if (type == NOOP) {
		return;
	}
	else if (type == NetworkUpdateType::REPLACE_FRAME) {
		CoordinateFrame* frame = new CoordinateFrame();
		frame->deserialize(inBuffer);
		if (entity != nullptr) {
			entity->setFrame(*frame);
		}
	}
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

void NetworkUtils::broadcastDestroyEntity(GUniqueID id, ENetHost* serverHost, uint16 frameNum) {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::DESTROY_ENTITY);
	outBuffer.writeUInt16(frameNum);
	id.serialize(outBuffer);		// Send the GUID as a byte string

	debugPrintf("sent destroy packet: %s\n", id.toString16());

	ENetPacket* packet = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length(), ENET_PACKET_FLAG_RELIABLE);
	enet_host_broadcast(serverHost, 0, packet);
}

int NetworkUtils::sendHitReport(GUniqueID shot_id, GUniqueID shooter_id, ENetPeer* serverPeer, uint16 frameNum) {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D::G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::REPORT_HIT);
	outBuffer.writeUInt16(frameNum);
	shot_id.serialize(outBuffer);
	shooter_id.serialize(outBuffer);
	ENetPacket* packet = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length() + 1, ENET_PACKET_FLAG_RELIABLE);
	return enet_peer_send(serverPeer, 0, packet);
}
void NetworkUtils::handleHitReport(ENetHost* serverHost, ENetPeer* clientPeer, uint16 frameNum) {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D::G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::ADD_POINTS);
	outBuffer.writeUInt16(frameNum);

	ENetPacket* packet = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length(), ENET_PACKET_FLAG_RELIABLE);

	enet_peer_send(clientPeer, 0, packet);
	NetworkUtils::broadcastRespawn(serverHost, frameNum);
}

int NetworkUtils::sendMoveClient(CFrame frame, ENetPeer* peer, uint16 frameNum) {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D::G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MOVE_CLIENT);
	outBuffer.writeUInt16(frameNum);
	outBuffer.writeUInt8(NetworkUpdateType::REPLACE_FRAME);
	frame.serialize(outBuffer);
	ENetPacket* packet = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length() + 1, ENET_PACKET_FLAG_RELIABLE);
	return enet_peer_send(peer, 0, packet);
}

int NetworkUtils::sendHandshake(ENetSocket socket, ENetAddress address) {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::HANDSHAKE);
	outBuffer.writeUInt16(0);	// Dummy frame num
	ENetBuffer hs_buff;
	hs_buff.data = (void*)outBuffer.getCArray();
	hs_buff.dataLength = outBuffer.length();
	return enet_socket_send(socket, &address, &hs_buff, 1);
}

int NetworkUtils::sendHandshakeReply(ENetSocket socket, ENetAddress address) {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D::G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::HANDSHAKE_REPLY);
	outBuffer.writeUInt16(0);	// Dummy frame num
	ENetBuffer buff;
	buff.data = (void*)outBuffer.getCArray();
	buff.dataLength = outBuffer.length();
	return enet_socket_send(socket, &address, &buff, 1);
}

NetworkUtils::ConnectedClient NetworkUtils::registerClient(ENetEvent event, BinaryInput& inBuffer) {
	/* get the clients information and create a ConnectedClient struct */
	ConnectedClient newClient;
	newClient.peer = event.peer;
	GUniqueID clientGUID;
	clientGUID.deserialize(inBuffer);
	newClient.guid = clientGUID;
	ENetAddress addr;
	addr.host = event.peer->address.host;
	addr.port = inBuffer.readUInt16();   // Set the port to what the client sends to us because we might loose the UDP handshake packet
	newClient.unreliableAddress = addr;
	debugPrintf("\tPort: %i\n", addr.port);
	debugPrintf("\tHost: %i\n", addr.host);
	/* Create Reply to the client */
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::CLIENT_REGISTRATION_REPLY);
	outBuffer.writeUInt16(0);	// Dummy frame num
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
	outBuffer.writeUInt16(0);	// Dummy frame num
	id.serialize(outBuffer);		// Send the GUID as a byte string to the server so it can identify the client

	outBuffer.writeUInt16(port); // Client socket port

	registerPacket = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length(), ENET_PACKET_FLAG_RELIABLE);
	return enet_peer_send(peer, 0, registerPacket);
}

void NetworkUtils::broadcastCreateEntity(GUniqueID guid, ENetHost* serverHost, uint16 frameNum) {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::CREATE_ENTITY);
	outBuffer.writeUInt16(frameNum);
	guid.serialize(outBuffer);		// Send the GUID as a byte string to the server so it can identify the client

	ENetPacket* packet = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length(), ENET_PACKET_FLAG_RELIABLE);
	// update the other peers with new connection
	enet_host_broadcast(serverHost, 0, packet);
}

int NetworkUtils::sendCreateEntity(GUniqueID guid, ENetPeer* peer, uint16 frameNum) {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::CREATE_ENTITY);
	outBuffer.writeUInt16(frameNum);
	guid.serialize(outBuffer);		// Send the GUID as a byte string to the server so it can identify the client
	ENetPacket* packet = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length(), ENET_PACKET_FLAG_RELIABLE);
	return enet_peer_send(peer, 0, packet);
}

void NetworkUtils::broadcastBatchEntityUpdate(Array<shared_ptr<Entity>> entities, Array<ENetAddress> destinations, ENetSocket sendSocket, uint16 frameNum) {
	/* Setup the packet */
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::BATCH_ENTITY_UPDATE);
	outBuffer.writeUInt16(frameNum);
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

void NetworkUtils::serverBatchEntityUpdate(Array<shared_ptr<NetworkedEntity>> entities, Array<ConnectedClient> clients, ENetSocket sendSocket, uint16 frameNum) {
	Array<shared_ptr<Entity>> genericEntities;
	for (shared_ptr<NetworkedEntity> e : entities) {
		genericEntities.append((shared_ptr<Entity>) e);
	}
	Array<ENetAddress> addresses;
	for (ConnectedClient client : clients) {
		addresses.append(client.unreliableAddress);
	}
	NetworkUtils::broadcastBatchEntityUpdate(genericEntities, addresses, sendSocket, frameNum);
}

int NetworkUtils::sendSetSpawnPos(G3D::Point3 position, float heading, ENetPeer* peer) {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::SET_SPAWN_LOCATION);
	outBuffer.writeUInt16(0);	// Dummy frame num
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

int NetworkUtils::sendRespawnClient(ENetPeer* clientPeer, uint16 frameNum) {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::RESPAWN_CLIENT);
	outBuffer.writeUInt16(frameNum);
	ENetPacket* packet = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length(), ENET_PACKET_FLAG_RELIABLE);
	return enet_peer_send(clientPeer, 0, packet);
}

void NetworkUtils::broadcastRespawn(ENetHost* serverHost, uint16 frameNum) {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::RESPAWN_CLIENT);
	outBuffer.writeUInt16(frameNum);
	ENetPacket* packet = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length(), ENET_PACKET_FLAG_RELIABLE);
	enet_host_broadcast(serverHost, 0, packet);
}

int NetworkUtils::sendReadyUpMessage(ENetPeer* serverPeer) {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D::G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::READY_UP_CLIENT);
	outBuffer.writeUInt16(0);	// Dummy frame num (haven't started a trial yet)
	ENetPacket* packet = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length() + 1, ENET_PACKET_FLAG_RELIABLE);
	return enet_peer_send(serverPeer, 0, packet);
}

void NetworkUtils::broadcastStartSession(ENetHost* serverHost) {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::START_NETWORKED_SESSION);
	outBuffer.writeUInt16(1);	// Dummy frame num (this is where we sync and reset the client frame numbers)
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
	outBuffer.writeFloat32(selectedConfig.restrictionBoxAngle);

	outBuffer.writeBool8(selectedConfig.counterStrafing);

	ENetPacket* packet = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length(), ENET_PACKET_FLAG_RELIABLE);
	if(!broadcast)
		return enet_peer_send(clientPeer, 0, packet);
	enet_host_broadcast(serverHost, 0, packet);
	return 0;
}

int NetworkUtils::sendSessionTimeoutMessage(ENetPeer* serverPeer, uint16 frameNum)
{
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D::G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::CLIENT_ROUND_TIMEOUT);
	outBuffer.writeUInt16(frameNum);
	
	ENetPacket* packet = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length(), ENET_PACKET_FLAG_RELIABLE);
	return enet_peer_send(serverPeer, 0, packet);
}

void NetworkUtils::broadcastResetRound(ENetHost* serverHost, uint16 frameNum)
{
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::RESET_CLIENT_ROUND);
	outBuffer.writeUInt16(frameNum);
	ENetPacket* packet = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length(), ENET_PACKET_FLAG_RELIABLE);
	enet_host_broadcast(serverHost, 0, packet);
}

void NetworkUtils::broadcastRoundFeedback(ENetHost* serverHost, uint16 frameNum)
{
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::CLIENT_FEEDBACK_START);
	outBuffer.writeUInt16(frameNum);
	ENetPacket* packet = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length(), ENET_PACKET_FLAG_RELIABLE);
	enet_host_broadcast(serverHost, 0, packet);
}

int NetworkUtils::sendFeedbackSubmittedMessage(ENetPeer* serverPeer, uint16 frameNum)
{
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D::G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::CLIENT_FEEDBACK_SUBMITTED);
	outBuffer.writeUInt16(frameNum);

	ENetPacket* packet = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length(), ENET_PACKET_FLAG_RELIABLE);
	return enet_peer_send(serverPeer, 0, packet);
}
