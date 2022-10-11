#pragma once
#include <G3D/G3D.h>
#include <map>

namespace G3D{
	class InputHandler {
	public:
		class NetworkInput // need to store X amount of last arrays
		{
		private:
			uint8 m_playerID;

			float32 m_XMovement;
			float32 m_YMovement;

			float32 m_XMouseDelta;
			float32 m_YMouseDelta;

			Array<GKey>* m_pressCode = new Array<GKey>;
			Array<GKey>* m_releaseCode = new Array<GKey>;

		public:
			NetworkInput();
			NetworkInput(uint8 playerID, float32 XMovement, float32 YMovement, float32 XMouseDelta, float32 YMouseDelta, Array<GKey>* pressCode, Array<GKey>* releaseCode);
			~NetworkInput();
			float32 GetXMovement();
			float32 GetYMovement();
			float32 GetXMouseDelta();
			float32 GetYMouseDelta();
			Vector2 GetXYMouseDelta();
			Array<GKey>* GetPressCode();
			Array<GKey>* GetReleaseCode();
		};

	private:
	private:
		int m_pastFrames = 10;
		int m_leadingFrame = 0;
		Array<Array<NetworkInput>*>* m_DataInputs = new Array<Array<NetworkInput>*>; //hold some future frames
		Array<NetworkInput>* m_unreadFrameBuffer = new Array<NetworkInput>;
	public:
		InputHandler();
		~InputHandler();
		void NewNetworkInput(uint8 playerID, float32 XMovement, float32 YMovement, float32 XMouseDelta, float32 YMouseDelta, Array<GKey>* pressCode, Array<GKey>* releaseCode, int frame);
		bool AllClientsFrame(int frameNum, int clientsConnected);
		void NewLeadingFrame(int frameNum);
		bool CheckFrameAcceptable(int frameNum);
		Array<NetworkInput>* GetFrameBuffer();
		void FlushBuffer();

		Array<NetworkInput>* GetFrameInputs(int frameNum);
	};

}

namespace G3D {
	class DataHandler {
	public:
		class NetworkInput // need to store X amount of last arrays
		{
		private:
			uint8 m_playerID;

			CoordinateFrame m_cframe;
			bool m_fired;

		public:
			NetworkInput();
			NetworkInput(uint8 playerID, CoordinateFrame cframe, bool fired);
			~NetworkInput();

			bool GetFired();
			CoordinateFrame GetCFrame();

			void SetCFrame(CoordinateFrame cframe);
			void SetFired(bool fired);

		};
	private:
		int m_pastFrames = 10;
		int m_futureFrames = 2;
		int m_currentFrame = 0; 
		Array<Array<NetworkInput*>*>* m_DataInputs = new Array<Array<NetworkInput*>*>; //hold some future frames //this will be full hopfully
		//TODO: keep track of how many clients are present during a single frame
		Array<NetworkInput>* m_unreadFrameBuffer = new Array<NetworkInput>;//these will be half empty
	public:
		DataHandler();
		~DataHandler();
		void SetParameters(int frameCutoff, int futureFrames);
		bool AllClientsFrame(int frameNum, int clientsConnected);
		void NewCurrentFrame(int frameNum, int clientsConnected);
		bool CheckFrameAcceptable(int frameNum);
		Array<NetworkInput>* GetFrameBuffer();
		void FlushBuffer();

		void UpdateCframe(uint8 playerID, CoordinateFrame cframe, int frameNum);
		void UpdateFired(uint8 playerID, bool fired, int frameNum);

		NetworkInput* PredictFrame(int frameNum, uint8 playerID);

		Array<NetworkInput>* GetFrameInputs(int frame);
	};

}

