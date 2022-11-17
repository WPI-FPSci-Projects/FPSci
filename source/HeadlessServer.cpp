#include "Packet.h"
#include "NetworkUtils.h"
#include "LatentNetwork.h"
#include <G3D/G3D.h>

ENetHost* localHost;
ENetAddress reliableServerAddress;
ENetAddress unreliableServerAddress;
String serverAddressStr = "127.0.0.1";
int serverPort = 18821;
ENetPeer* serverPeer;
ENetSocket unreliableSocket;
bool enetConnected, socketConnected;
bool runningTrial;
GUniqueID playerGUID = GUniqueID::create();
int networkLatency = 100;
int localFrameNumber = 0;

void connectToServer() {
	if (enet_initialize() != 0) {
		// print an error and terminate if Enet does not initialize successfully
		throw std::runtime_error("Could not initialize ENet networking");

	}

	localHost = enet_host_create(NULL, 1, 2, 0, 0); // create a host on an arbitrary port which we use to contact the server
	if (localHost == nullptr)
	{
		throw std::runtime_error("Could not create a local host for the server to connect to");
	}
	enet_address_set_host(&reliableServerAddress, serverAddressStr.c_str());
	reliableServerAddress.port = serverPort;
	serverPeer = enet_host_connect(localHost, &reliableServerAddress, 2, 0);		//setup a peer with the server
	if (serverPeer == nullptr)
	{
		throw std::runtime_error("Could not create a connection to the server");
	}
	logPrintf("created a peer with the server at %s:%d (the connection may not have been accepeted by the server)", serverAddressStr, serverPort);

	unreliableServerAddress.port = serverPort + 1;
	unreliableSocket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
	enet_socket_set_option(unreliableSocket, ENET_SOCKOPT_NONBLOCK, 1); //Set socket to non-blocking

	// initialize variables to be reset by handshakes
	enetConnected = false;
	socketConnected = false;
}

void sendLocation() {
	//TODO
}

int main(int argc, const char* argv[]) {
	connectToServer();
	while (true) {
		/* Handle all incoming packets (ignore most of them) */
		shared_ptr<GenericPacket> inPacket = NetworkUtils::receivePacket(localHost, &unreliableSocket);
		while (inPacket != nullptr) {
			char ip[16];
			ENetAddress srcAddr = inPacket->srcAddr();
			enet_address_get_host_ip(&srcAddr, ip, 16);
			if (!inPacket->isReliable()) {
				switch (inPacket->type()) {
				case HANDSHAKE_REPLY: {
					socketConnected = true;
					debugPrintf("INFO: Received HANDSHAKE_REPLY from server\n");
					/* Tell the server we are ready as soon as we are connected */
					if (enetConnected && socketConnected) {
						debugPrintf("INFO: Sending a ready packet\n");
						shared_ptr<ReadyUpClientPacket> outPacket = GenericPacket::createReliable<ReadyUpClientPacket>(serverPeer);
						NetworkUtils::send(outPacket);
					}
					break;
				}
				}
			}
			else {
				switch (inPacket->type()) {
				case RELIABLE_CONNECT: {
					ENetAddress localAddress;
					enet_socket_get_address(unreliableSocket, &localAddress);
					char ipStr[16];
					enet_address_get_host_ip(&localAddress, ipStr, 16);
					debugPrintf("Registering client...\n");
					debugPrintf("\tPort: %i\n", localAddress.port);
					debugPrintf("\tHost: %s\n", ipStr);
					shared_ptr<RegisterClientPacket> registrationPacket = GenericPacket::createReliable<RegisterClientPacket>(serverPeer);
					registrationPacket->populate(serverPeer, playerGUID, localAddress.port);
					NetworkUtils::send(registrationPacket);
					break;
				}
				case RELIABLE_DISCONNECT: {
					debugPrintf("Disconnected from peer\n");
				}
				case CLIENT_REGISTRATION_REPLY: {
					RegistrationReplyPacket* typedPacket = static_cast<RegistrationReplyPacket*>(inPacket.get());
					debugPrintf("INFO: Received registration reply...\n");
					if (typedPacket->m_guid == playerGUID) {
						if (typedPacket->m_status == 0) {
							enetConnected = true;
							debugPrintf("INFO: Received registration from server\n");

							/* Set the amount of latency to add */
							NetworkUtils::setAddressLatency(unreliableServerAddress, networkLatency);
							NetworkUtils::setAddressLatency(typedPacket->srcAddr(), networkLatency);

							/* Tell the server we are ready as soon as we are connected */
							if (enetConnected && socketConnected) {
								shared_ptr<ReadyUpClientPacket> outPacket = GenericPacket::createReliable<ReadyUpClientPacket>(serverPeer);
								NetworkUtils::send(outPacket);
							}
							else {
								debugPrintf("WARNING: Reliable connection ready before unreliable\n");
							}
						}
						else {
							debugPrintf("WARN: Server connection refused (%i)\n", typedPacket->m_status);
						}
					}
					break;
				}
				case START_NETWORKED_SESSION: {
					StartSessionPacket* typedPacket = static_cast<StartSessionPacket*> (inPacket.get());
					localFrameNumber = typedPacket->m_frameNumber; // Set the frame number to sync with the server
					if (enetConnected && socketConnected) {
						debugPrintf("Starting a trial (sending data now)\n");
						runningTrial = true;
					}
					else {
						debugPrintf("WARNING: Supposed to be starting trial but not connected yet (%d,%d)\n", enetConnected, socketConnected);
					}
					break;
				}
				}
				shared_ptr<GenericPacket> inPacket = NetworkUtils::receivePacket(localHost, &unreliableSocket);
			}
		}

		/* If we are running a trial and there has been the right amount of time since the last frame send the next frame */
		if (runningTrial) {
			sendLocation();
			// TODO: All of this needs to be implemented
		}
	}
}