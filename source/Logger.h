#pragma once
#include <G3D/G3D.h>
#include "sqlHelpers.h"
#include "UserConfig.h"
#include "Session.h"
#include "NetworkedSession.h"
#include "Dialogs.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>

using RowEntry = Array<String>;
using Columns = Array<Array<String>>;

struct TargetLocation;
struct PlayerAction;
struct RemotePlayerAction;
struct FrameInfo;
struct NetworkedClient;

template<typename ItemType> static size_t queueBytes(Array<ItemType>& queue)
{
	return queue.size() * sizeof(ItemType);
}

/** Used to log data from experiments, sessions, trials and users
	Uses SQLITE database output. */
class FPSciLogger : public ReferenceCountedObject {
public:
	using TargetInfo = RowEntry;
	using QuestionResult = RowEntry;
	using TrialValues = RowEntry;
	using UserValues = RowEntry;
	using PlayerValues = RowEntry;

protected:
	sqlite3* m_db = nullptr;						///< The db used for logging
	
	String m_openTimeStr;							///< Time string for database creation

	// state of current database entries (not yet used)
	long long int m_sessionRowID;
	long long int m_userRowID;
	long long int m_trialRowID;

	const size_t m_bufferLimit = 1024 * 1024;		///< Flush every this many bytes
	const LoggerConfig& m_config;					/// Logger configuration

	bool m_running = false;
	bool m_flushNow = false;
	std::thread m_thread;
	std::mutex m_queueMutex;
	std::condition_variable m_queueCV;

	// Output queues for reported data storage
	Array<FrameInfo> m_frameInfo;						///< Storage for frame info (sdt, idt, rdt)
	Array<PlayerAction> m_playerActions;				///< Storage for player action (hit, miss, aim)
	Array<RemotePlayerAction> m_remotePlayerActions;
	Array<QuestionResult> m_questions;
	Array<TargetLocation> m_targetLocations;			///< Storage for target trajectory (vector3 cartesian)
	Array<TargetInfo> m_targets;
	Array<TrialValues> m_trials;						///< Trial ID, start/end time etc.
	Array<UserValues> m_users;
	Array<NetworkedClient> m_networkedClients;
	Array<PlayerValues> m_playerConfigs;

	size_t getTotalQueueBytes()
	{
		return queueBytes(m_frameInfo) +
			queueBytes(m_playerActions) +
			queueBytes(m_remotePlayerActions) +
			queueBytes(m_questions) +
			queueBytes(m_targetLocations) +
			queueBytes(m_targets) +
			queueBytes(m_trials) + 
			queueBytes(m_networkedClients) +
			queueBytes(m_playerConfigs);
	}

	template<typename ItemType> void addToQueue(Array<ItemType>& queue, const ItemType& item)
	{
		{
			std::lock_guard<std::mutex> lk(m_queueMutex);
			queue.push_back(item);
		}

		// Wake up the logging thread if it needs to write out the results
		size_t pendingBytes = getTotalQueueBytes();
		if (pendingBytes >= m_bufferLimit) {
			m_queueCV.notify_one();
		}
	}

	void loggerThreadEntry();

	/** Record an array of frame timing info */
	void recordFrameInfo(const Array<FrameInfo>& info);

	/** Record an array of player actions */
	void recordPlayerActions(const Array<PlayerAction>& actions);

	/** Record an array of *remote* player actions */
	void recordRemotePlayerActions(const Array<RemotePlayerAction>& actions);

	/** Record an array of target locations */
	void recordTargetLocations(const Array<TargetLocation>& locations);


	void recordNetworkedClients(const Array<NetworkedClient>& clients);

	/** Open a results file, or create it if it doesn't exist */
	void initResultsFile(const String& filename, 
		const String& subjectID, 
		const String& expConfigFilename,
		const shared_ptr<SessionConfig>& sessConfig, 
		const String& description);

	/** Close the results file */
	void closeResultsFile(void);

	// Functions that set up the database schema
	/** Create a session table with columns as specified by the provided sessionConfig */
	void createExperimentsTable(const String& expConfigFilename);
	void createSessionsTable(const shared_ptr<SessionConfig>& sessConfig);
	void createTargetTypeTable();
	void createTargetsTable();
	void createTrialsTable();
	void createTargetTrajectoryTable();
	void createPlayerActionTable();
	void createRemotePlayerActionTable();
	void createFrameInfoTable();
	void createQuestionsTable();
	void createUsersTable();
	void createNetworkedClientTable();
	void createPlayerConfigTable();

	// Functions that assume the schema from above
	//void insertSession(sessionInfo);
	//void updateSession(sessionInfo);
	// ... 

public:

	FPSciLogger(const String& filename, const String& subjectID, const String& expConfigFilename, const shared_ptr<SessionConfig>& sessConfig, const String& description);
	virtual ~FPSciLogger();
	
	static shared_ptr<FPSciLogger> create(const String& filename, 
		const String& subjectID, 
		const String& expConfigFilename,
		const shared_ptr<SessionConfig>& sessConfig,
		const String& description="None") 
	{
		return createShared<FPSciLogger>(filename, subjectID, expConfigFilename, sessConfig, description);
	}

	void updateSessionEntry(bool complete, int trialCount);

	void logFrameInfo(const FrameInfo& frameInfo) { addToQueue(m_frameInfo, frameInfo); }
	void logPlayerAction(const PlayerAction& playerAction) { addToQueue(m_playerActions, playerAction); }
	void logRemotePlayerAction(const RemotePlayerAction& playerAction) { addToQueue(m_remotePlayerActions, playerAction); }
	void logQuestionResult(const QuestionResult& questionResult) { addToQueue(m_questions, questionResult); }
	void logTargetLocation(const TargetLocation& targetLocation) { addToQueue(m_targetLocations, targetLocation); }
	void logTargetInfo(const TargetInfo& targetInfo) { addToQueue(m_targets, targetInfo); }
	void logTrial(const TrialValues& trial) { addToQueue(m_trials, trial); }

	void logUserConfig(const UserConfig& userConfig, const String& sessId, const Vector2& sessTurnScale);
	void logTargetTypes(const Array<shared_ptr<TargetConfig>>& targets);

	void logNetworkedClient(const NetworkedClient& client) { addToQueue(m_networkedClients, client); }
	void logPlayerConfig(const PlayerConfig& playerConfig, const GUniqueID& id, int trialNumber);

	/** Wakes up the logging thread and flushes even if the buffer limit is not reached yet. */
	void flush(bool blockUntilDone);
	
	/** Generate a timestamp for logging */
	static String genUniqueTimestamp();

	static FILETIME getFileTime();
	static String formatFileTime(FILETIME ft);

	/** Genearte a timestamp for filenames */
	static String genFileTimestamp();

	/** Record a question and its response */
	void addQuestion(Question question, String session, const shared_ptr<DialogBase>& dialog);

	/** Add a target to an experiment */
	void addTarget(const String& name, const shared_ptr<TargetConfig>& targetConfig, const String& spawnTime, const float& size, const Point2& spawnEcc);

	/** Write to CSV failsafe **/
	void writeToFile(String folderName, String fileName, RowEntry data);
};
