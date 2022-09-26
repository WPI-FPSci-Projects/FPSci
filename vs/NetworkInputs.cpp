#include "NetworkInputs.h"

G3D::NetworkInputs::NetworkInputs(int32 XMovement, int32 YMovement, float32 XMouseDelta, float32 YMouseDelta, Array<GKey>* pressCode, Array<GKey>* releaseCode) :
	m_XMovement(&XMovement), m_YMovement(&YMovement), m_XMouseDelta(&XMouseDelta), m_YMouseDelta(&YMouseDelta), m_pressCode(pressCode), m_releaseCode(releaseCode)
{
}

G3D::NetworkInputs::~NetworkInputs()
{
}

int32 G3D::NetworkInputs::GetXMovement()
{
	return *m_XMovement;
}

int32 G3D::NetworkInputs::GetYMovement()
{
	return *m_YMovement;
}

float32 G3D::NetworkInputs::GetXMouseDelta()
{
	return *m_XMouseDelta;
}

float32 G3D::NetworkInputs::GetYMouseDelta()
{
	return *m_YMouseDelta;
}

Array<GKey>* G3D::NetworkInputs::GetPressCode()
{
	return m_pressCode;
}

Array<GKey>* G3D::NetworkInputs::GetReleaseCode()
{
	return m_releaseCode;
}
