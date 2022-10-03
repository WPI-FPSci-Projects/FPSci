#include "NetworkUtils.h"
#include "TargetEntity.h"

#include <enet/enet.h>
/*
static void updateEntity(Entity entity, BinaryInput inBuffer) {
	NetworkUtils::NetworkUpdateType type = (NetworkUtils::NetworkUpdateType)inBuffer.readUInt8();
}*/

void NetworkUtils::updateEntity(Array <GUniqueID> ignoreIDs, shared_ptr<G3D::Scene> scene, BinaryInput& inBuffer, NetworkHandler* networkHandler) {
	GUniqueID entity_id;
	entity_id.deserialize(inBuffer);;
	shared_ptr<NetworkedEntity> entity = (*scene).typedEntity<NetworkedEntity>(entity_id.toString16());
	if (entity == nullptr && !ignoreIDs.contains(entity_id)) {
		debugPrintf("Recieved update for entity %s, but it doesn't exist\n", entity_id.toString16().c_str());
	}
	if (ignoreIDs.contains(entity_id)) { // don't let the server move ignored entities
		entity = nullptr;
	}
	updateEntity(entity, inBuffer, networkHandler, entity_id); // Allways call this even if the entity is ignored so we remove the data from the BinaryInput
}

void NetworkUtils::updateEntity(shared_ptr<Entity> entity, BinaryInput& inBuffer, NetworkHandler* networkHandler, GUniqueID playerID) {
	NetworkUtils::NetworkUpdateType type = (NetworkUtils::NetworkUpdateType)inBuffer.readUInt8();
	uint16 frameNum = inBuffer.readUInt16();
	if (type == NOOP) {
		return;
	}
	else if (type == NetworkUpdateType::REPLACE_FRAME) {
		CoordinateFrame* frame = new CoordinateFrame();
		frame->deserialize(inBuffer);
		//fill networkInput structure
		if (networkHandler != nullptr) {
			networkHandler->UpdateCframe(playerID, *frame, (int) frameNum);
		}
		if (entity != nullptr) {
			entity->setFrame(*frame);
		}
	}
}

void NetworkUtils::createFrameUpdate(GUniqueID id, shared_ptr<Entity> entity, BinaryOutput& outBuffer, uint16 frameNum) {
	id.serialize(outBuffer);
	outBuffer.writeUInt8(NetworkUpdateType::REPLACE_FRAME);
	outBuffer.writeUInt16(frameNum);
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

int NetworkUtils::sendFireReport(GUniqueID shooter_GUID, uint8 shooter_playerID, ENetPeer* serverPeer, uint16 frameNum) {
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D::G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::REPORT_FIRE);
	outBuffer.writeUInt16(frameNum);
	shooter_GUID.serialize(outBuffer);
	outBuffer.writeUInt8(shooter_playerID);
	ENetPacket* packet = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length() + 1, ENET_PACKET_FLAG_RELIABLE);
	return enet_peer_send(serverPeer, 0, packet);
}

void NetworkUtils::handleHitReport(ENetHost* serverHost, BinaryInput& inBuffer, uint16 frameNum) {
	GUniqueID hit_entity, shooter;
	hit_entity.deserialize(inBuffer);
	shooter.deserialize(inBuffer);
	debugPrintf("HIT REPORTED: %s SHOT %s WITH THE CANDLESTICK IN THE LIBRARY\n", shooter.toString16(), hit_entity.toString16());
	NetworkUtils::broadcastRespawn(serverHost, frameNum);
}

void NetworkUtils::handleFireReport(BinaryInput& inBuffer, NetworkHandler* networkHandler, uint16 frameNum){
	GUniqueID shooter_GUID;
	uint8 shooter_playerID;
	shooter_GUID.deserialize(inBuffer);
	shooter_playerID = inBuffer.readUInt8();
	networkHandler->UpdateFired(shooter_playerID, true, frameNum);
	debugPrintf("HIT REPORTED: %s SHOT %s WITH THE CANDLESTICK IN THE LIBRARY\n", shooter_GUID.toString16(), shooter_GUID.toString16());
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

NetworkUtils::ConnectedClient NetworkUtils::registerClient(ENetEvent event, BinaryInput& inBuffer, uint8 playerID) {
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
	outBuffer.writeUInt8(playerID);	// Send the player ID to the client
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
		NetworkUtils::createFrameUpdate(guid, e, outBuffer, frameNum);
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

void NetworkUtils::serializeUserInput(ENetSocket socket, ENetAddress address, G3D::UserInput* ui, int frame, uint8 playerID) {
	// start
	BinaryOutput output;
	//G3D::serialize(userInput, output);

	output.setEndian(G3D_BIG_ENDIAN);
	output.writeUInt8(NetworkUtils::MessageType::USER_INPUT); //Message type
	output.writeInt32(frame);
	output.writeUInt8(playerID);
	output.writeFloat32(ui->getX());		// x-axis movement
	output.writeFloat32(ui->getY());		// y-axis movement

	output.writeFloat32(ui->mouseDX());	// mouseDX
	output.writeFloat32(ui->mouseDY());	// mouseDY


	Array<GKey>* pressCode = new Array<GKey>; //press keys
	Array<GKey>* releaseCode = new Array<GKey>; //release keys
	ui->pressedKeys(*pressCode); // get buffers
	ui->releasedKeys(*releaseCode);
	output.writeInt32(pressCode->size()); // get size
	output.writeInt32(releaseCode->size());
	for (G3D::GKey key: *pressCode) { //serialize all keys in pressCode
		key.serialize(output);
	}
	for (G3D::GKey key : *releaseCode) {//serialize all keys in releaseCode
		key.serialize(output);
	}

	
	// Send user input to server
	ENetBuffer enet_buff;
	enet_buff.data = (void*)output.getCArray();
	enet_buff.dataLength = output.length();
	//start
	int status;
	status = enet_socket_send(socket, &address, &enet_buff, 1);
	//end
}

void NetworkUtils::deserializeUserInput(G3D::BinaryInput* bi, InputHandler* inputHandler) {
	int frame = bi->readInt32();
	if (inputHandler->CheckFrameAcceptable(frame)) { //Frame farther back than rollback accepts
		return;
	}
	uint8 playerID = bi->readUInt8();
	float32 playerX = bi->readFloat32(); // read X movement
	float32 PlayerY = bi->readFloat32(); // read y movement

	float32 mouseX = bi->readFloat32(); // mouse x delta
	float32 mouseY = bi->readFloat32(); // mouse y  delta

	int32 pressCodeSize = bi->readInt32();
	int32 releaseCodeSize = bi->readInt32();
	
	Array<GKey>* pressCode = new Array<GKey>;
	Array<GKey>* releaseCode = new Array<GKey>;

	for (int i = 0; i < pressCodeSize; i++){ //make keys and add them to pressCode
		G3D::GKey* key = new GKey();
		key->deserialize(*bi);
		pressCode->push(*key);
	}
	for (int i = 0; i < releaseCodeSize; i++) { //make keys and add them to releaseCode
		G3D::GKey* key = new GKey();
		key->deserialize(*bi);
		releaseCode->push(*key);
	}
	

	inputHandler->NewNetworkInput(playerID, playerX, PlayerY, mouseX, mouseY, pressCode, releaseCode, frame);
}