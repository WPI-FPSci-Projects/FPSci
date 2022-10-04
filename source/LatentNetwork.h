#pragma once

#include <chrono>
#include <G3D/G3D.h>
#include "Packet.h"

struct LatentPacket {
	std::chrono::time_point<std::chrono::high_resolution_clock> timeToSend;
	GenericPacket encapsulatedPacket;
	LatentPacket(std::chrono::time_point<std::chrono::high_resolution_clock> time, GenericPacket packet) {
		timeToSend = time;
		encapsulatedPacket = packet;
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
	void enqueuePacket(shared_ptr<LatentPacket> packet) {
		{
			std::lock_guard<std::mutex> lk(m_queueMutex);
			m_sharedPacketQueue.push_back(packet);
		}
	};
};