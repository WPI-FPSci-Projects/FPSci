#pragma once
#include <G3D/G3D.h>
#include <map>

namespace G3D{
	class InputHandler {
	public:
		class NetworkInput // need to store X amount of last arrays
		{
		private:

			float32 m_XMovement;
			float32 m_YMovement;

			float32 m_XMouseDelta;
			float32 m_YMouseDelta;

			Array<GKey>* m_pressCode = new Array<GKey>;
			Array<GKey>* m_releaseCode = new Array<GKey>;

		public:
			NetworkInput();
			NetworkInput(float32 XMovement, float32 YMovement, float32 XMouseDelta, float32 YMouseDelta, Array<GKey>* pressCode, Array<GKey>* releaseCode);
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
		int m_frameCutoff = 10;
		int m_leadingFrame = 0;
		Array<Array<NetworkInput>*>* m_networkInputs = new Array<Array<NetworkInput>*>; //hold some future frames
	public:
		InputHandler();
		~InputHandler();
		void NewNetworkInput(float32 XMovement, float32 YMovement, float32 XMouseDelta, float32 YMouseDelta, Array<GKey>* pressCode, Array<GKey>* releaseCode, int frame);
		bool AllClientsFrame(int frame, int clientsConnected);
		void NewLeadingFrame(int frame);
		bool CheckFrameAcceptable(int frame);

		Array<NetworkInput>* GetFrameInputs(int frame);
	};

}
