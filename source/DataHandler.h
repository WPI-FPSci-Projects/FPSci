#pragma once
#include <G3D/G3D.h>
#include <map>

namespace G3D {

	class ServerDataInput
	{
	public:
		bool m_fired;
		CoordinateFrame m_cframe;
		bool m_valid;

	public:
		ServerDataInput();
		ServerDataInput(CoordinateFrame cframe, bool fired, bool valid);
		~ServerDataInput();

		bool GetFired();
		void SetFired(bool fired);
		CoordinateFrame GetCFrame();
		void SetCFrame(CoordinateFrame cframe);
		bool GetValid();
		void SetValid(bool valid);

	};


	class ServerDataHandler{
	public:
		int m_pastFrames = 10;
		uint32 m_currentFrame = 0;
		Table<String, Array<ServerDataInput>*>* m_DataInputs;
		Table<String, int>* m_clientLastValid;
		//TODO: keep track of how many clients are present during a single frame
		Array<ServerDataInput>* m_unreadFrameBuffer = new Array<ServerDataInput>;//these will be half empty

	public:
		ServerDataHandler();
		~ServerDataHandler();
	
		//used to access a cframe from a datapoint
		CoordinateFrame GetCFrame(int frameNum, String playerID);

		//Called per frame to delete oldest data point
		void NewCurrentFrame(int frameNum);

		//updateing values in Handler
		void UpdateCframe(String playerID, CoordinateFrame cframe, int frameNum, bool fromClient);
		void UpdateFired(String playerID, bool fired, int frameNum);
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
		Array<ServerDataInput>* GetFrameBuffer();
		void FlushBuffer();
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
	public:
		ClientDataHandler();
		~ClientDataHandler();
		CoordinateFrame* PredictEntityFrame(CoordinateFrame currentKnownLocation, String playerID);
		CoordinateFrame* RecalulateClient(String playerID, CoordinateFrame cframe);
		void UpdateCframe(String playerID, CoordinateFrame cframe, uint32 currentFrame, uint32 packetFrame);
		void AddNewClient(String playerID);

	};
};


