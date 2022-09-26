#include "InputHandler.h"

G3D::InputHandler::NetworkInput::NetworkInput(uint8 playerID, float32 XMovement, float32 YMovement, float32 XMouseDelta, float32 YMouseDelta, Array<GKey>* pressCode, Array<GKey>* releaseCode) :
	m_playerID(playerID),
	m_XMovement(XMovement), m_YMovement(YMovement), m_XMouseDelta(XMouseDelta), 
	m_YMouseDelta(YMouseDelta), m_pressCode(pressCode), m_releaseCode(releaseCode)
{
}

G3D::InputHandler::NetworkInput::NetworkInput() :
	m_XMovement(0.0), m_YMovement(0.0), m_XMouseDelta(0.0), m_YMouseDelta(0.0), m_pressCode(nullptr), m_releaseCode(nullptr)

{
}

G3D::InputHandler::NetworkInput::~NetworkInput()
{
}

float32 G3D::InputHandler::NetworkInput::GetXMovement()
{
	return m_XMovement;
}

float32 G3D::InputHandler::NetworkInput::GetYMovement()
{
	return m_YMovement;
}

float32 G3D::InputHandler::NetworkInput::GetXMouseDelta()
{
	return m_XMouseDelta;
}

float32 G3D::InputHandler::NetworkInput::GetYMouseDelta()
{
	return m_YMouseDelta;
}

Vector2 G3D::InputHandler::NetworkInput::GetXYMouseDelta() {
	return Vector2(m_XMouseDelta, m_YMouseDelta);
}

Array<GKey>* G3D::InputHandler::NetworkInput::GetPressCode()
{
	return m_pressCode;
}

Array<GKey>* G3D::InputHandler::NetworkInput::GetReleaseCode()
{
	return m_releaseCode;
}


void G3D::InputHandler::NewNetworkInput(uint8 playerID, float32 XMovement, float32 YMovement, float32 XMouseDelta, float32 YMouseDelta, Array<GKey>* pressCode, Array<GKey>* releaseCode, int frame)
{
	//To-do fix client frame getting ahead of server
	NetworkInput* newInput = new NetworkInput(playerID, XMovement, YMovement, XMouseDelta, YMouseDelta, pressCode, releaseCode);
	m_networkInputs[0][m_leadingFrame - frame + 2]->append(*newInput);
	m_unreadFrameBuffer->append(*newInput);
}


G3D::InputHandler::InputHandler()
{
	for (int i = 0; i < m_frameCutoff + 2; i++) {
		m_networkInputs->append(new Array<NetworkInput>);
	}
}

bool G3D::InputHandler::AllClientsFrame(int frame, int clientsConnected)
{
	return m_networkInputs[0][m_leadingFrame - frame + 2]->length() == clientsConnected;
}

Array<G3D::InputHandler::NetworkInput>* G3D::InputHandler::GetFrameInputs(int frame)
{
	//if (m_networkInputs->empty) return NULL;
	return m_networkInputs[0][m_leadingFrame - frame + 2];
}

void G3D::InputHandler::NewLeadingFrame(int frame)
{
	//debugPrintf("%d\t%d\n", m_leadingFrame, frame);
	m_leadingFrame = frame;
	m_networkInputs->pop();
	m_networkInputs->append(new Array<NetworkInput>);
}

void G3D::InputHandler::FlushBuffer()
{
	m_unreadFrameBuffer->clear();
}

bool G3D::InputHandler::CheckFrameAcceptable(int frame)
{
	return (m_leadingFrame - frame + 2>= m_frameCutoff);
}

Array<G3D::InputHandler::NetworkInput>* G3D::InputHandler::GetFrameBuffer() {
	return m_unreadFrameBuffer;
}
