#pragma once
#include <G3D/G3D.h>
#include <map>

namespace G3D {
	class DataInput // need to store X amount of last arrays
	{
	public:
		uint8 m_playerID;
		CoordinateFrame m_cframe;

	public:
		DataInput();
		DataInput(uint8 playerID, CoordinateFrame cframe);
		~DataInput();

		CoordinateFrame GetCFrame();
		void SetCFrame(CoordinateFrame cframe);

	};

	class ServerDataInput : public DataInput// need to store X amount of last arrays
	{
	public:
		bool m_fired;

	public:
		ServerDataInput();
		ServerDataInput(uint8 playerID, CoordinateFrame cframe, bool fired);
		~ServerDataInput();

		bool GetFired();
		void SetFired(bool fired);

	};

	class DataHandler {
	public:
		int m_pastFrames = 10;
		int m_futureFrames = 2;
		uint32 m_currentFrame = 0;
		Array<Array<DataInput>*>* m_DataInputs = new Array<Array<DataInput>*>; //hold some future frames //this will be full hopfully
	public:
		DataHandler();
		~DataHandler();
		void SetParameters(int frameCutoff, int futureFrames);
		bool AllClientsFrame(int frameNum, int clientsConnected);
		void NewCurrentFrame(int frameNum, int clientsConnected);
		bool CheckFrameAcceptable(int frameNum); //meant to keep out very old data points. how to incorperate that with client extrapolation? 

		void UpdateCframe(uint8 playerID, CoordinateFrame cframe, int frameNum);

		Array<DataInput>* GetFrameInputs(int frame);
	};

	class ServerDataHandler : public DataHandler {
	public:
		Array<Array<ServerDataInput>*>* m_DataInputs = new Array<Array<ServerDataInput>*>;
		//TODO: keep track of how many clients are present during a single frame
		Array<ServerDataInput>* m_unreadFrameBuffer = new Array<ServerDataInput>;//these will be half empty

	public:
		ServerDataHandler();
		~ServerDataHandler();
		void UpdateFired(uint8 playerID, bool fired, int frameNum);
		Array<ServerDataInput>* GetFrameBuffer();
		void FlushBuffer();
		CoordinateFrame GetCFrame(int frameNum, int playerID);
		void NewCurrentFrame(int frameNum, int clientsConnected);
		void UpdateCframe(uint8 playerID, CoordinateFrame cframe, int frameNum, bool fromClient);
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


