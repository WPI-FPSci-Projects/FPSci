#include "NetworkUtils.h"
#include "TargetEntity.h"

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