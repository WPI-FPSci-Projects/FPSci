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

	class ClientDataInput : public DataInput {
	public:
		bool m_exists;
	public:
		ClientDataInput();
		ClientDataInput(uint8 playerID, CoordinateFrame cframe, bool givenCFrame);
		~ClientDataInput();

	};


	class DataHandler {
	public:
		int m_pastFrames = 10;
		int m_futureFrames = 2;
		int m_currentFrame = 0;
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
		void NewCurrentFrame(int frameNum, int clientsConnected);
		void UpdateCframe(uint8 playerID, CoordinateFrame cframe, int frameNum);
	};


	class ClientDataHandler : public DataHandler {
	public:
		enum class predictionType
		{
			NONE,
			LINEAR,
			QUADRATIC,
		};

		predictionType type = predictionType::NONE;
		Array<Array<ClientDataInput>*>* m_DataInputs = new Array<Array<ClientDataInput>*>;
	public:
		ClientDataHandler();
		void NewCurrentFrame(int frameNum, int clientsConnected);
		~ClientDataHandler();
		ClientDataInput* PredictEntity(int frameNum, uint8 playerID);
		ClientDataInput* PredictFrameLinear(int frameNum, uint8 playerID);
		ClientDataInput* PredictFrameQuadratic(int frameNum, uint8 playerID);
		bool DataPointExists(int frameNum, uint8 playerID);
	};
};


