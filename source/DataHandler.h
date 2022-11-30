#pragma once
#include <G3D/G3D.h>
#include <map>

/*READ ME
Server datahandler
	m_DataInputs is the overall storage mechanism for cframes and their validity
	m_clientLastValid is a table of frameNums for the newest validated position from server
	m_clientLatestFrame is a table of frameNum for newest postion from a client
	m_unreadCFrameBuffer is CFrames not yet checked by server
	m_unreadFiredbuffer is fire reports not yet checked by server

	getCFrame is how you access a cframe
	updateCframe is how you add a cframe to the datahandler fromClient is to recognize if a datapoint is from a client.
				 So if called in onNetwork it should be true that way it is added to unreadFrameBuffer.
				 Otherwise make it false so it is not added to unreadFrameBuffer and not checked again.
	

	ValidateData is how you check to make sure that a value form a client has been checked and validated by the server
	lastValidFromFrameNum lets you check any frame number and it looks backwards through time to find the most recent valid frame from the frameNum specified


	GetCFrameBuffer() / GetFiredBuffer() used to get the buffer
	FlushCFrameBuffer() / FlushFiredBuffer(); use after all values have been checked

Client datahandler
	prediction type is specified in experiment.any use to define how you want to calculate extrapolation 1 is none, 2 is linear, 3 quadratic

	m_DataInputs keeps track of same amount of data points as type
	m_clientHeading is transition matrix for each client from calculated result
	m_clientVectors is transition vector for each client from calculated result
	m_frameLag keeps track how far behind in time is each client to local clients
	m_historicalCFrames stores a history of past client CFrames associated with a frame number

	PredictEntityFrame is used to calculate 1 frames worth of movement based on client vector and heading
	RecalulateClient re-extraoplates the position based on new data and frame lag
	UpdateCframe adds new cframe to its data getting rid of the oldest for a client and recaluclates vector and matrix

	
*/

namespace G3D {

	class ServerDataInput
	{
	public:
		CoordinateFrame m_cframe;
		bool m_valid;

	public:
		ServerDataInput();
		ServerDataInput(CoordinateFrame cframe, bool valid);
		~ServerDataInput();

		CoordinateFrame GetCFrame();
		void SetCFrame(CoordinateFrame cframe);
		bool GetValid();
		void SetValid(bool valid);

	};

	struct ServerFireInput
	{
		bool m_fired;
		String m_playerID;
		int m_frameNum;
	};

	class ServerDataHandler{
	public:
		int m_pastFrames = 500;
		uint32 m_currentFrame = 0;
		Table<String, Array<ServerDataInput>*>* m_DataInputs;
		Table<String, int>* m_clientLastValid;
		Table<String, int>* m_clientLatestFrame;
		Array<ServerDataInput>* m_unreadCFrameBuffer;
		Array<ServerFireInput>* m_unreadFiredbuffer = new Array<ServerFireInput>;

	public:
		ServerDataHandler();
		~ServerDataHandler();
	
		//used to access a cframe from a datapoint
		CoordinateFrame GetCFrame(int frameNum, String playerID);

		//Called per frame to delete oldest data point
		void NewCurrentFrame(int frameNum);

		//updateing values in Handler
		void UpdateCframe(String playerID, CoordinateFrame cframe, int frameNum, bool fromClient);
		void UpdateFired(String playerID, int frameNum);
		void ValidateData(String playerID, int frameNum);
		int lastValidFromFrameNum(String playerID, int frameNum);

		//meant to keep out very old data points.
		bool CheckFrameAcceptable(int frameNum);

		//setup
		void SetParameters(int frameCutoff);

		//Add/delete clients
		void AddNewClient(String playerID);
		void DeleteClient(String playerID);

		//unreadFrameBuffer
		Array<ServerDataInput>* GetCFrameBuffer();
		void FlushCFrameBuffer();

		Array<ServerFireInput>* GetFiredBuffer();
		void FlushFiredBuffer();
	};


	class ClientDataHandler{
	public:
		enum class predictionType
		{
			NONE,
			LINEAR,
			QUADRATIC,
		};

		predictionType m_type = predictionType::NONE;
		Table<String, Array<CoordinateFrame>*>* m_DataInputs = new Table<String, Array<CoordinateFrame>*>;
		Table<String, Vector3>* m_clientVectors = new Table<String, Vector3>;
		Table<String, Matrix3>* m_clientHeading = new Table<String, Matrix3>;
		Table<String, int8>* m_frameLag = new Table<String, int8>;
		int m_pastFrames = 10;
		Table<uint32, CoordinateFrame*>* m_historicalCFrames = new Table<uint32, CoordinateFrame*>;

	public:
		ClientDataHandler();
		~ClientDataHandler();
		CoordinateFrame* PredictEntityFrame(CoordinateFrame currentKnownLocation, String playerID);
		CoordinateFrame* RecalulateClient(String playerID, CoordinateFrame cframe);
		void UpdateCframe(String playerID, CoordinateFrame cframe, uint32 currentFrame, uint32 packetFrame);
		void AddNewClient(String playerID);

	};
};


