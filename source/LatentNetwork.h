#pragma once

#include <chrono>
#include <G3D/G3D.h>
#include "Packet.h"

struct LatentPacket : public ReferenceCountedObject {
	std::chrono::time_point<std::chrono::high_resolution_clock> timeToSend;
	shared_ptr<GenericPacket> encapsulatedPacket;

	LatentPacket(shared_ptr<GenericPacket> packet, std::chrono::time_point<std::chrono::high_resolution_clock> time) {
		timeToSend = time;
		encapsulatedPacket = packet;
	}

	static shared_ptr<LatentPacket> create(shared_ptr<GenericPacket> packet, std::chrono::time_point<std::chrono::high_resolution_clock> time) {
		return createShared<LatentPacket>(packet, time);
	}
};

struct PacketSendtimeCompare {
	bool operator()(const shared_ptr<LatentPacket>& a, const shared_ptr<LatentPacket>& b) const {
		return b->timeToSend < a->timeToSend;
	}
};

class LatentNetwork {

protected:
	Array<shared_ptr<LatentPacket>> m_sharedPacketQueue;
	Array<shared_ptr<LatentPacket>> m_packetHeap;

	bool m_threadRunning;
	std::thread m_thread;
	std::mutex m_queueMutex;
	//std::condition_variable m_queueCV;

	void networkThreadTick();

public:
	static LatentNetwork& getInstance()
	{
		static LatentNetwork    instance; // Guaranteed to be destroyed.
							  // Instantiated on first use.
		return instance;
	}

private:
	LatentNetwork() { // constructor
		// Reserve some space in these arrays here
		m_sharedPacketQueue.reserve(5000);


		// Thread management
		m_threadRunning = true;
		m_thread = std::thread(&LatentNetwork::networkThreadTick, this);
	}

		// C++ 03
		// ========
		// Don't forget to declare these two. You want to make sure they
		// are inaccessible(especially from outside), otherwise, you may accidentally get copies of
		// your singleton appearing.
	LatentNetwork(LatentNetwork const&);              // Don't Implement
	void operator=(LatentNetwork const&); // Don't implement

	~LatentNetwork();

public:


	void enqueuePacket(shared_ptr<LatentPacket> packet) {
		{
			std::lock_guard<std::mutex> lk(m_queueMutex);
			m_sharedPacketQueue.push_back(packet);
		}
	};
};