#include "Packet.h"
#include "NetworkUtils.h"
#include "LatentNetwork.h"
#include <G3D/G3D.h>
#include <chrono>
#include <iostream>
#include <fstream>
#include <sstream>

ENetHost* localHost;
ENetAddress reliableServerAddress;
ENetAddress unreliableServerAddress;
String serverAddressStr = "192.168.1.150";
int serverPort = 18821;
ENetPeer* serverPeer;
ENetSocket unreliableSocket;
bool enetConnected, socketConnected;
bool runningTrial;
GUniqueID playerGUID;
int networkLatency = 100;
int localFrameNumber = 0;
int framerate = 60;
String csvFile = "positions.csv";
std::fstream file;
bool notifiedReady = false;

//G3D_START_AT_MAIN();

void connectToServer() {
	if (enet_initialize() != 0) {
		// print an error and terminate if Enet does not initialize successfully
		throw std::runtime_error("Could not initialize ENet networking");

	}
	playerGUID = GUniqueID::create();

	localHost = enet_host_create(NULL, 1, 2, 0, 0); // create a host on an arbitrary port which we use to contact the server
	if (localHost == NULL)
	{
		throw std::runtime_error("Could not create a local host for the server to connect to");
	}
	enet_address_set_host(&reliableServerAddress, serverAddressStr.c_str());
	reliableServerAddress.port = serverPort;
	serverPeer = enet_host_connect(localHost, &reliableServerAddress, 2, 0);		//setup a peer with the server
	if (serverPeer == NULL)
	{
		throw std::runtime_error("Could not create a connection to the server");
	}
	printf("created a peer with the server at %s:%d (the connection may not have been accepeted by the server)", serverAddressStr.c_str(), serverPort);

	enet_address_set_host(&unreliableServerAddress, serverAddressStr.c_str());
	unreliableServerAddress.port = serverPort + 1;
	unreliableSocket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
	enet_socket_set_option(unreliableSocket, ENET_SOCKOPT_NONBLOCK, 1); //Set socket to non-blocking

	// initialize variables to be reset by handshakes
	enetConnected = false;
	socketConnected = false;
}

void sendLocation() {
	std::string line;
	if (file.is_open()) {
		if (std::getline(file, line)) {
			std::stringstream strstream = std::stringstream(line);
			std::string word;
			std::getline(strstream, word, ',');
			float pos_az, pos_el, pos_x, pos_y, pos_z;
			pos_az = std::stof(word);
			std::getline(strstream, word, ',');
			pos_el = std::stof(word);
			std::getline(strstream, word, ',');
			pos_x = std::stof(word);
			std::getline(strstream, word, ',');
			pos_y = std::stof(word);
			std::getline(strstream, word, ',');
			pos_z = std::stof(word);

			// TODO: make this include rotation information
			CFrame frame = G3D::CFrame(Point3(pos_x, pos_y, pos_z));
			
			shared_ptr<BatchEntityUpdatePacket> updatePacket = GenericPacket::createUnreliable<BatchEntityUpdatePacket>(&unreliableSocket, &unreliableServerAddress);
			BatchEntityUpdatePacket::EntityUpdate update = BatchEntityUpdatePacket::EntityUpdate();
			update.frame = frame;
			update.name = playerGUID.toString16();
			
			// TODO: make this use a real frame number
			updatePacket->populate(0, Array<BatchEntityUpdatePacket::EntityUpdate>(update), BatchEntityUpdatePacket::NetworkUpdateType::REPLACE_FRAME);
			NetworkUtils::send(updatePacket);
		}
		else {
			printf("Reached the end of the file quitting this client now\n");
			enet_peer_disconnect(serverPeer, NULL);
			enet_host_service(localHost, nullptr, NULL);
			Sleep(1000);
			exit(0);
		}
	}
	else {
		printf("ERROR: File is not open!\n");
		throw std::runtime_error("CSV file was not open");
	}
}

int main(int argc, const char* argv[]) {
	if (argc < 4) {
		printf("Wrong number of arguments provided. Run this progam as follows: \n\
\tHeadlessClient.exe <server IP> <framerate> <path to CSV data> [network latency]\n");
		exit(-1);
	}
	else {
		serverAddressStr = argv[1];
		framerate = std::stoi(argv[2]);
		csvFile = argv[3];
		if (argc == 5) {
			networkLatency = std::stoi(argv[4]);
		}
	}
	std::chrono::nanoseconds interframeTime = std::chrono::microseconds(1000000) / framerate;
	auto nextUpdate = std::chrono::high_resolution_clock::now() + interframeTime;
	file = std::fstream(csvFile.c_str(), std::ios::in);

	connectToServer();
	while (true) {
		if (!socketConnected) {
			// If we haven't received a response yet send another handshake packet
			shared_ptr<HandshakePacket> outPacket = GenericPacket::createUnreliable<HandshakePacket>(&unreliableSocket, &unreliableServerAddress);
			outPacket->send();
		}
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
					printf("INFO: Received HANDSHAKE_REPLY from server\n");
					/* Tell the server we are ready as soon as we are connected */
					if (enetConnected && socketConnected && !notifiedReady) {
						printf("INFO: Sending a ready packet\n");
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
					printf("Registering client...\n");
					printf("\tPort: %i\n", localAddress.port);
					printf("\tHost: %s\n", ipStr);
					shared_ptr<RegisterClientPacket> registrationPacket = GenericPacket::createReliable<RegisterClientPacket>(serverPeer);
					registrationPacket->populate(serverPeer, playerGUID, localAddress.port);
					NetworkUtils::send(registrationPacket);
					break;
				}
				case RELIABLE_DISCONNECT: {
					printf("Disconnected from peer\n");
					exit(-1);
				}
				case CLIENT_REGISTRATION_REPLY: {
					RegistrationReplyPacket* typedPacket = static_cast<RegistrationReplyPacket*>(inPacket.get());
					printf("INFO: Received registration reply...\n");
					if (typedPacket->m_guid == playerGUID) {
						if (typedPacket->m_status == 0) {
							enetConnected = true;
							printf("INFO: Received registration from server\n");

							/* Set the amount of latency to add */
							NetworkUtils::setAddressLatency(unreliableServerAddress, networkLatency);
							NetworkUtils::setAddressLatency(typedPacket->srcAddr(), networkLatency);

							/* Tell the server we are ready as soon as we are connected */
							if (enetConnected && socketConnected && !notifiedReady) {
								printf("INFO: Sending a ready packet\n");
								shared_ptr<ReadyUpClientPacket> outPacket = GenericPacket::createReliable<ReadyUpClientPacket>(serverPeer);
								NetworkUtils::send(outPacket);
							}
							else {
								printf("WARNING: Reliable connection ready before unreliable\n");
							}
						}
						else {
							printf("WARN: Server connection refused (%i)\n", typedPacket->m_status);
						}
					}
					break;
				}
				case START_NETWORKED_SESSION: {
					StartSessionPacket* typedPacket = static_cast<StartSessionPacket*> (inPacket.get());
					localFrameNumber = typedPacket->m_frameNumber; // Set the frame number to sync with the server
					if (enetConnected && socketConnected && !runningTrial) {
						printf("Starting a trial (sending data now)\n");
						runningTrial = true;
					}
					else {
						printf("WARNING: Supposed to be starting trial but not connected yet (%d,%d)\n", enetConnected, socketConnected);
					}
					break;
				}
				}
			}
			inPacket = NetworkUtils::receivePacket(localHost, &unreliableSocket);
		}

		/* If we are running a trial and there has been the right amount of time since the last frame send the next frame */
		auto now = std::chrono::high_resolution_clock::now();
		if (now >= nextUpdate) {
			nextUpdate += interframeTime;
			if (runningTrial) {
				sendLocation();
			}
		}
	std::this_thread::sleep_until(nextUpdate);
	}
}