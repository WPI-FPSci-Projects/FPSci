#include "NetworkUtils.h"
#include "TargetEntity.h"
#include "LatentNetwork.h"
#include "FpsConfig.h"

#include <enet/enet.h>
/*
static void updateEntity(Entity entity, BinaryInput inBuffer) {
	NetworkUtils::NetworkUpdateType type = (NetworkUtils::NetworkUpdateType)inBuffer.readUInt8();
}*/

void NetworkUtils::updateEntity(Array <GUniqueID> ignoreIDs, shared_ptr<G3D::Scene> scene, BinaryInput& inBuffer, DataHandler* networkHandler) {
	GUniqueID entity_id;
	entity_id.deserialize(inBuffer);;
	shared_ptr<NetworkedEntity> entity = (*scene).typedEntity<NetworkedEntity>(entity_id.toString16());
	if (entity == nullptr && !ignoreIDs.contains(entity_id)) {
		debugPrintf("Recieved update for entity %s, but it doesn't exist\n", entity_id.toString16().c_str());
	}
	if (ignoreIDs.contains(entity_id)) { // don't let the server move ignored entities
		entity = nullptr;
	}
	updateEntity(entity, inBuffer, networkHandler, entity_id);
	// Allways call this even if the entity is ignored so we remove the data from the BinaryInput
}

// This duplication is made to enable updateEntity to update the local client player along with other NetworkedEntities
void NetworkUtils::updateEntity(GUniqueID localGUID, shared_ptr<Scene> scene, BinaryInput& inBuffer, DataHandler* networkHandler) {
	GUniqueID entity_id;
	entity_id.deserialize(inBuffer);
	shared_ptr<Entity> entity = (*scene).typedEntity<NetworkedEntity>(entity_id.toString16());
	if (entity == nullptr)
	{
		if (entity_id == localGUID)
		{
			entity = (*scene).typedEntity<PlayerEntity>("player");
			updateEntity(entity, inBuffer, networkHandler, entity_id);
			return;
		}
		else
		{
			debugPrintf("Received update for entity %s, but it doesn't exist\n", entity_id.toString16().c_str());
		}
	}
	updateEntity(entity, inBuffer, networkHandler, entity_id); // Allways call this even if the entity is ignored so we remove the data from the BinaryInput
}

void NetworkUtils::updateEntity(shared_ptr<Entity> entity, BinaryInput& inBuffer, DataHandler* networkHandler, GUniqueID playerID) {
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

void NetworkUtils::handleFireReport(BinaryInput& inBuffer, ServerDataHandler* networkHandler, uint16 frameNum){
	GUniqueID shooter_GUID;
	uint8 shooter_playerID;
	shooter_GUID.deserialize(inBuffer);
	shooter_playerID = inBuffer.readUInt8();
	networkHandler->UpdateFired(shooter_playerID, true, frameNum);
	debugPrintf("FIRE REPORTED: %s SHOT WITH THE CANDLESTICK IN THE LIBRARY\n", shooter_GUID.toString16());
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
		// TODO: Refactor this to be less bad
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
	case PacketType::SEND_PLAYER_CONFIG:
		return GenericPacket::createReceive<SendPlayerConfigPacket>(srcAddr, inBuffer);
		break;
	case PacketType::ADD_POINTS:
		return GenericPacket::createReceive<AddPointPacket>(srcAddr, inBuffer);
		break;
	case PacketType::RESET_CLIENT_ROUND:
		return GenericPacket::createReceive<ResetClientRoundPacket>(srcAddr, inBuffer);
		break;
	case PacketType::CLIENT_FEEDBACK_START:
		return GenericPacket::createReceive<ClientFeedbackStartPacket>(srcAddr, inBuffer);
		break;
	case PacketType::CLIENT_SESSION_END:
		return GenericPacket::createReceive<ClientSessionEndPacket>(srcAddr, inBuffer);
		break;
	case PacketType::CLIENT_ROUND_TIMEOUT:
		return GenericPacket::createReceive<ClientRoundTimeoutPacket>(srcAddr, inBuffer);
		break;
	case PacketType::CLIENT_FEEDBACK_SUBMITTED:
		return GenericPacket::createReceive<ClientFeedbackSubmittedPacket>(srcAddr, inBuffer);
		break;
	case PacketType::PING:
		return GenericPacket::createReceive<PingPacket>(srcAddr, inBuffer);
		break;
	case PacketType::PING_DATA:
		return GenericPacket::createReceive<PingDataPacket>(srcAddr, inBuffer);
		break;
	default:
		debugPrintf("WARNING: Could not create a typed packet of for type %d. Returning GenericPacket instead\n", type);
		return GenericPacket::createReceive<GenericPacket>(srcAddr, inBuffer);
		break;
	}
}

void NetworkUtils::broadcastCreateEntity(GUniqueID guid, ENetHost* serverHost, uint16 frameNum, uint8 playerID)
{
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::CREATE_ENTITY);
	outBuffer.writeUInt16(frameNum);
	guid.serialize(outBuffer);		// Send the GUID as a byte string to the server so it can identify the client
	outBuffer.writeUInt8(playerID);

	ENetPacket* packet = enet_packet_create((void*)outBuffer.getCArray(), outBuffer.length(), ENET_PACKET_FLAG_RELIABLE);
	// update the other peers with new connection
	enet_host_broadcast(serverHost, 0, packet);
}

int NetworkUtils::sendCreateEntity(GUniqueID guid, ENetPeer* peer, uint16 frameNum, uint8 playerID)
{
	BinaryOutput outBuffer;
	outBuffer.setEndian(G3D_BIG_ENDIAN);
	outBuffer.writeUInt8(NetworkUtils::MessageType::CREATE_ENTITY);
	outBuffer.writeUInt16(frameNum);
	guid.serialize(outBuffer);		// Send the GUID as a byte string to the server so it can identify the client
	outBuffer.writeUInt8(playerID);
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

shared_ptr<GenericPacket> NetworkUtils::receivePing(ENetSocket* pingSocket) {
	ENetAddress srcAddr;
	ENetBuffer buff;
	void* data = malloc(ENET_HOST_DEFAULT_MTU);  //Allocate 1 mtu worth of space for the data from the packet
	buff.data = data;
	buff.dataLength = ENET_HOST_DEFAULT_MTU;
	shared_ptr<GenericPacket> packet = nullptr;
	if (enet_socket_receive(*pingSocket, &srcAddr, &buff, 1)) {
		BinaryInput inBuffer((const uint8*)buff.data, buff.dataLength, G3D_BIG_ENDIAN, false, true);
		packet = GenericPacket::createReceive<GenericPacket>(srcAddr, inBuffer); // Create a generic packet that reads just the type
		inBuffer.setPosition(0);
		packet = NetworkUtils::createTypedPacket(packet->type(), srcAddr, inBuffer); // Create a typed packet that reads all data based on type
		packet->m_reliable = false;
		free(data);
		return packet; // Return here so we only read from the socket and dont drop packets
	}
	else {
		free(data);
		return nullptr;
	}
}

void NetworkUtils::broadcastReliable(shared_ptr<GenericPacket> packet, ENetHost* localHost) {
	for (int i = 0; i < localHost->peerCount; i ++) {
		/* 
		 * Must loop through all the possible peers because ENet does not remove 
		 * a peer from the array when they disconnect 
		 */
		if (localHost->peers[i].state == ENET_PEER_STATE_CONNECTED) {
			shared_ptr<GenericPacket> packetCopy = packet->clone();
			packetCopy->setReliableDest(&localHost->peers[i]);
			NetworkUtils::send(packetCopy);
		}
	}
}

void NetworkUtils::broadcastUnreliable(shared_ptr<GenericPacket> packet, ENetSocket* srcSocket, Array<ENetAddress*> addresses) {
	for (ENetAddress* destAddr : addresses) {
		shared_ptr<GenericPacket> packetCopy = packet->clone();
		packetCopy->setUnreliableDest(srcSocket, destAddr);
		NetworkUtils::send(packetCopy);
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
	else {
		//debugPrintf("could not find address!\n");
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