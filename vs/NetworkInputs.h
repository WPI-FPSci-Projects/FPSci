#pragma once
#include <G3D/G3D.h>

namespace G3D{

	class NetworkInputs
	{
	private:
		NetworkInputs(int32 XMovement, int32 YMovement, float32 XMouseDelta, float32 YMouseDelta, Array<GKey>* pressCode, Array<GKey>* releaseCode);
		~NetworkInputs();

		int32* m_XMovement = new int32();
		int32* m_YMovement = new int32();

		float32* m_XMouseDelta = new float32();
		float32* m_YMouseDelta = new float32();

		Array<GKey>* m_pressCode = new Array<GKey>;
		Array<GKey>* m_releaseCode = new Array<GKey>;

	public:
		int32 GetXMovement();
		int32 GetYMovement();
		float32 GetXMouseDelta();
		float32 GetYMouseDelta();
		Array<GKey>* GetPressCode();
		Array<GKey>* GetReleaseCode();
	};

}

