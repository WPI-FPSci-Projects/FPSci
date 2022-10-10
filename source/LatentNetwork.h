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

bool packetTimeCompare(const shared_ptr<LatentPacket>& a, const shared_ptr<LatentPacket>& b) {
	return b->timeToSend > a->timeToSend;
}

class LatentNetwork : public ReferenceCountedObject {

public:
	//things

protected:
	Array<shared_ptr<LatentPacket>> m_sharedPacketQueue;
	Array<shared_ptr<LatentPacket>> m_packetHeap;

	bool m_threadRunning;
	std::thread m_thread;
	std::mutex m_queueMutex;
	//std::condition_variable m_queueCV;

	void networkThreadTick();

public:
	LatentNetwork();

	static shared_ptr<LatentNetwork> create()
	{
		return createShared<LatentNetwork>();
	}

	void enqueuePacket(shared_ptr<LatentPacket> packet) {
		{
			std::lock_guard<std::mutex> lk(m_queueMutex);
			m_sharedPacketQueue.push_back(packet);
		}
	};
};