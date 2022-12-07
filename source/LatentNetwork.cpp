#include "LatentNetwork.h"
#include "NetworkUtils.h"


void LatentNetwork::networkThreadTick()
{
	std::unique_lock<std::mutex> lk(m_queueMutex);
	lk.unlock();

	auto tickDuration = std::chrono::microseconds(500);	
	std::chrono::time_point<std::chrono::high_resolution_clock> start_timer = std::chrono::high_resolution_clock::now();

	while (m_threadRunning) {
		// sleep til ready
		auto dT = std::chrono::high_resolution_clock::now() - start_timer;
		std::this_thread::sleep_until(std::chrono::system_clock::now() + (tickDuration - dT));
		start_timer = std::chrono::high_resolution_clock::now();

		// lock packet queue mutex
		lk.lock();
		// copy packet queue into local space
		decltype(m_sharedPacketQueue) sharedPacketQueue;
		sharedPacketQueue.swap(m_sharedPacketQueue, sharedPacketQueue);
		lk.unlock();

		// heapify the read packets
		for (shared_ptr<LatentPacket> packet : sharedPacketQueue) {
			m_packetHeap.push_back(packet);
			std::push_heap(m_packetHeap.begin(), m_packetHeap.end(), PacketSendtimeCompare());
		}

		// send the packets that need to be sent
		std::chrono::time_point<std::chrono::high_resolution_clock> now = std::chrono::high_resolution_clock::now();

		while (m_packetHeap.length() > 0 && m_packetHeap[0]->timeToSend <= now) {
			std::pop_heap(m_packetHeap.begin(), m_packetHeap.end(), PacketSendtimeCompare());
			NetworkUtils::addByteCount(m_packetHeap.back()->encapsulatedPacket->send());
			NetworkUtils::addPacketCount(1);
			m_packetHeap.pop_back();
		}
		//debugPrintf("Packet heap length: %d\n", m_packetHeap.length());
	}
}
/*
LatentNetwork::LatentNetwork()
{
	// Reserve some space in these arrays here
	m_sharedPacketQueue.reserve(5000);


	// Thread management
	m_threadRunning = true;
	m_thread = std::thread(&LatentNetwork::networkThreadTick, this);
}*/

LatentNetwork::~LatentNetwork()
{
	{
		std::lock_guard<std::mutex> lk(m_queueMutex);
		m_threadRunning = false;
	}
	//m_queueCV.notify_one();
	m_thread.join();
}
/*
void LatentNetwork::flush(bool blockUntilDone)
{
	// Not implemented. Make another condition variable if this is needed.
	assert(!blockUntilDone);

	{
		std::lock_guard<std::mutex> lk(m_queueMutex);
		m_flushNow = true;
	}
	m_queueCV.notify_one();
}
*/