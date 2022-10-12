#include "NetworkUtils.h"
#include "TargetEntity.h"
#include "LatentNetwork.h"
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

NetworkUtils::ConnectedClient* NetworkUtils::registerClient(RegisterClientPacket* packet) {
	ConnectedClient* newClient = new ConnectedClient();
	newClient->peer = packet->m_peer;
	newClient->guid = packet->m_guid;
	ENetAddress addr;
	addr.host = packet->m_peer->address.host;
	addr.port = packet->m_portNum;
	newClient->unreliableAddress = addr;
	debugPrintf("\tPort: %i\n", addr.port);
	debugPrintf("\tHost: %i\n", addr.host);
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

shared_ptr<GenericPacket> NetworkUtils::createTypedPacket(PacketType type, ENetAddress srcAddr, BinaryInput& inBuffer, ENetEvent* event) {
	switch (type) {
	case PacketType::BATCH_ENTITY_UPDATE:
		return GenericPacket::createReceive<BatchEntityUpdatePacket>(srcAddr, inBuffer);
		break;
	case PacketType::CREATE_ENTITY:
		return GenericPacket::createReceive<CreateEntityPacket>(srcAddr, inBuffer);
		break;
	case PacketType::DESTROY_ENTITY:
		return GenericPacket::createReceive<DestroyEntityPacket>(srcAddr, inBuffer);
		break;
	case PacketType::MOVE_CLIENT:
		return GenericPacket::createReceive<MoveClientPacket>(srcAddr, inBuffer);
		break;
	case PacketType::REGISTER_CLIENT: {
		if (event == nullptr) {
			debugPrintf("WARNING: received a RegisterClientPacket on the unreliable channel\n");
			break;
		}
		shared_ptr<RegisterClientPacket> packet = GenericPacket::createReceive<RegisterClientPacket>(srcAddr, inBuffer);
		packet->m_peer = event->peer;
		return packet;
		break;
	}
	case PacketType::CLIENT_REGISTRATION_REPLY:
		return GenericPacket::createReceive<RegistrationReplyPacket>(srcAddr, inBuffer);
		break;
	case PacketType::HANDSHAKE:
		return GenericPacket::createReceive<HandshakePacket>(srcAddr, inBuffer);
		break;
	case PacketType::HANDSHAKE_REPLY:
		return GenericPacket::createReceive<HandshakeReplyPacket>(srcAddr, inBuffer);
		break;
	case PacketType::REPORT_HIT:
		return GenericPacket::createReceive<ReportHitPacket>(srcAddr, inBuffer);
		break;
	case PacketType::SET_SPAWN_LOCATION:
		return GenericPacket::createReceive<SetSpawnPacket>(srcAddr, inBuffer);
		break;
	case PacketType::RESPAWN_CLIENT:
		return GenericPacket::createReceive<RespawnClientPacket>(srcAddr, inBuffer);
		break;
	case PacketType::READY_UP_CLIENT:
		return GenericPacket::createReceive<ReadyUpClientPacket>(srcAddr, inBuffer);
		break;
	case PacketType::START_NETWORKED_SESSION:
		return GenericPacket::createReceive<StartSessionPacket>(srcAddr, inBuffer);
		break;
	case PacketType::PLAYER_INTERACT:
		return GenericPacket::createReceive<PlayerInteractPacket>(srcAddr, inBuffer);
		break;
	default:
		debugPrintf("WARNING: Could not create a typed packet of for type %d. Returning GenericPacket instead\n", type);
		return GenericPacket::createReceive<GenericPacket>(srcAddr, inBuffer);
		break;
	}
}

shared_ptr<GenericPacket> NetworkUtils::receivePacket(ENetHost* host, ENetSocket* socket) {
	ENetAddress srcAddr;
	ENetBuffer buff;
	void* data = malloc(ENET_HOST_DEFAULT_MTU);  //Allocate 1 mtu worth of space for the data from the packet
	buff.data = data;
	buff.dataLength = ENET_HOST_DEFAULT_MTU;
	shared_ptr<GenericPacket> packet = nullptr;
	if (enet_socket_receive(*socket, &srcAddr, &buff, 1)) {
		BinaryInput inBuffer((const uint8*)buff.data, buff.dataLength, G3D_BIG_ENDIAN, false, true);
		packet =  GenericPacket::createReceive<GenericPacket>(srcAddr, inBuffer); // Create a generic packet that reads just the type
		inBuffer.setPosition(0);
		packet = NetworkUtils::createTypedPacket(packet->type(), srcAddr, inBuffer); // Create a typed packet that reads all data based on type
		packet->m_reliable = false;
		return packet; // Return here so we only read from the socket and dont drop packets
	}
	ENetEvent event;
	if (enet_host_service(host, &event, 0)) {
		 switch( event.type){
		 case ENET_EVENT_TYPE_CONNECT: {
			 packet = ReliableConnectPacket::createReceive(event.peer->address);
			 packet->m_reliable = true;
			 break;
		 }
		 case ENET_EVENT_TYPE_DISCONNECT: {
			 packet = ReliableDisconnectPacket::createReceive(event.peer->address);
			 packet->m_reliable = true;
			 break;
		 }
		 case ENET_EVENT_TYPE_RECEIVE: {
			 BinaryInput inBuffer(event.packet->data, event.packet->dataLength, G3D_BIG_ENDIAN);
			 shared_ptr<GenericPacket> genPacket = GenericPacket::createReceive<GenericPacket>(event.peer->address, inBuffer);
			 inBuffer.setPosition(0);
			 packet = NetworkUtils::createTypedPacket(genPacket->type(), event.peer->address, inBuffer, &event);
			 packet->m_reliable = true;
			 break;
		 }
		 }
		 return packet;
		 enet_packet_destroy(event.packet);
	}
	free(data);
	// No new packets to receive
	return nullptr;
}

void NetworkUtils::broadcastReliable(shared_ptr<GenericPacket> packet, ENetHost* localHost) {
	for (int i = 0; i < localHost->peerCount; i ++) {
		packet->setReliableDest(&localHost->peers[i]);
		packet->send();
	}
}

void NetworkUtils::broadcastUnreliable(shared_ptr<GenericPacket> packet, ENetSocket* srcSocket, Array<ENetAddress*> addresses) {
	for (ENetAddress* destAddr : addresses) {
		packet->setUnreliableDest(srcSocket, destAddr);
		packet->send();
	}
}

void NetworkUtils::sendPacketDelayed(shared_ptr<GenericPacket> packet, int delay) {
	std::chrono::time_point<std::chrono::high_resolution_clock> timestamp = std::chrono::high_resolution_clock::now();
	timestamp = timestamp + std::chrono::milliseconds(delay);
	shared_ptr<LatentPacket> latentPacket = LatentPacket::create(packet, timestamp);
	LatentNetwork::getInstance().enqueuePacket(latentPacket);
}

void NetworkUtils::setAddressLatency(ENetAddress addr, int latency)
{
	NetworkUtils::latencyMap.erase(addr);
	NetworkUtils::latencyMap.insert({ addr, latency });
}

void NetworkUtils::removeAddressLatency(ENetAddress addr)
{
	NetworkUtils::latencyMap.erase(addr);
}

void NetworkUtils::setDefaultLatency(int latency)
{
	NetworkUtils::defaultLatency = latency;
}

void NetworkUtils::send(shared_ptr<GenericPacket> packet)
{
	int latency = NetworkUtils::defaultLatency;
	ENetAddress addr = *(packet->getDestinationAddress());
	auto searchResult = NetworkUtils::latencyMap.find(addr);
	if (searchResult != NetworkUtils::latencyMap.end()) {
		latency = searchResult->second;
	}

	if (latency == 0) { // don't bother the other thread if we don't want any delay
		packet->send();
	}
	else {
		sendPacketDelayed(packet, latency);
	}
}

// default latency is none by default:
int NetworkUtils::defaultLatency = 0;
std::map<ENetAddress, int, ENetAddressCompare> NetworkUtils::latencyMap;