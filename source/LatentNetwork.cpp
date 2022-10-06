#include "LatentNetwork.h"


void LatentNetwork::networkThreadTick()
{
	std::unique_lock<std::mutex> lk(m_queueMutex);
	lk.unlock();

	auto tickDuration = std::chrono::nanoseconds(500);
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
			std::push_heap(m_sharedPacketQueue.begin(), m_sharedPacketQueue.end(), packetTimeCompare);
		}

		// send the packets that need to be sent
		std::chrono::time_point<std::chrono::high_resolution_clock> now = std::chrono::high_resolution_clock::now();
		while (m_packetHeap[0]->timeToSend <= now) {
			std::pop_heap(m_packetHeap.begin(), m_packetHeap.end(), packetTimeCompare);
			m_packetHeap.back()->encapsulatedPacket.send();
			m_packetHeap.pop_back();
		}


		/*
		m_queueCV.wait(lk, [this] {
			return !m_running || m_flushNow || getTotalQueueBytes() >= m_bufferLimit;
			});

		// Move all the queues into temporary local copies.
		// This is so we can release the lock and allow the queues to grow again while writing out the results.
		// Also allocate new storage for each.
		decltype(m_frameInfo) frameInfo;
		frameInfo.swap(m_frameInfo, frameInfo);
		m_frameInfo.reserve(frameInfo.size() * 2);

		decltype(m_playerActions) playerActions;
		playerActions.swap(m_playerActions, playerActions);
		m_playerActions.reserve(playerActions.size() * 2);

		decltype(m_remotePlayerActions) remotePlayerActions;
		remotePlayerActions.swap(m_remotePlayerActions, remotePlayerActions);
		m_remotePlayerActions.reserve(remotePlayerActions.size() * 2);

		decltype(m_questions) questions;
		questions.swap(m_questions, questions);
		m_questions.reserve(questions.size() * 2);

		decltype(m_targetLocations) targetLocations;
		targetLocations.swap(m_targetLocations, targetLocations);
		m_targetLocations.reserve(targetLocations.size() * 2);

		decltype(m_targets) targets;
		targets.swap(m_targets, targets);
		m_targets.reserve(targets.size() * 2);

		decltype(m_trials) trials;
		trials.swap(m_trials, trials);
		m_trials.reserve(trials.size() * 2);

		decltype(m_users) users;
		users.swap(m_users, users);
		m_users.reserve(users.size() * 2);

		decltype(m_networkedClients) networkedClients;
		networkedClients.swap(m_networkedClients, networkedClients);
		m_networkedClients.reserve(networkedClients.size() * 2);

		// Unlock all the now-empty queues and write out our temporary copies
		lk.unlock();

		recordFrameInfo(frameInfo);
		recordPlayerActions(playerActions);
		recordRemotePlayerActions(remotePlayerActions);
		recordTargetLocations(targetLocations);

		recordNetworkedClients(networkedClients);

		insertRowsIntoDB(m_db, "Questions", questions);
		insertRowsIntoDB(m_db, "Targets", targets);
		insertRowsIntoDB(m_db, "Users", users);
		insertRowsIntoDB(m_db, "Trials", trials);

		lk.lock();*/
	}
}

LatentNetwork::LatentNetwork()
{
	// Reserve some space in these arrays here
	m_sharedPacketQueue.reserve(5000);


	// Thread management
	m_threadRunning = true;
	m_thread = std::thread(&LatentNetwork::networkThreadTick, this);
}

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