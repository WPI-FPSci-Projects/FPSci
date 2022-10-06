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


void G3D::InputHandler::NewNetworkInput(uint8 playerID, float32 XMovement, float32 YMovement, float32 XMouseDelta, float32 YMouseDelta, Array<GKey>* pressCode, Array<GKey>* releaseCode, int frameNum)
{
	//To-do fix client frameNum getting ahead of server
	NetworkInput* newInput = new NetworkInput(playerID, XMovement, YMovement, XMouseDelta, YMouseDelta, pressCode, releaseCode);
	m_DataInputs[0][m_leadingFrame - frameNum + 2]->append(*newInput);
	m_unreadFrameBuffer->append(*newInput);
}


G3D::InputHandler::InputHandler()
{
	for (int i = 0; i < m_pastFrames + 2; i++) {
		m_DataInputs->append(new Array<NetworkInput>);
	}
}

bool G3D::InputHandler::AllClientsFrame(int frameNum, int clientsConnected)
{
	return m_DataInputs[0][m_leadingFrame - frameNum + 2]->length() == clientsConnected;
}

Array<G3D::InputHandler::NetworkInput>* G3D::InputHandler::GetFrameInputs(int frameNum)
{
	//if (m_DataInputs->empty) return NULL;
	return m_DataInputs[0][m_leadingFrame - frameNum + 2];
}

void G3D::InputHandler::NewLeadingFrame(int frameNum)
{
	//debugPrintf("%d\t%d\n", m_leadingFrame, frameNum);
	m_leadingFrame = frameNum;
	m_DataInputs->pop();
	m_DataInputs->append(new Array<NetworkInput>);
}

void G3D::InputHandler::FlushBuffer()
{
	m_unreadFrameBuffer->clear();
}

bool G3D::InputHandler::CheckFrameAcceptable(int frameNum)
{
	return (m_leadingFrame - frameNum + 2>= m_pastFrames);
}

Array<G3D::InputHandler::NetworkInput>* G3D::InputHandler::GetFrameBuffer() {
	return m_unreadFrameBuffer;
}

/***********************************************************/
/***********************************************************/
/***********************************************************/
/********************DataHandler*************************/
/***********************************************************/
/***********************************************************/
/***********************************************************/

G3D::DataHandler::NetworkInput::NetworkInput(uint8 playerID, CoordinateFrame cframe, bool fired) :
	m_playerID(playerID), m_cframe(cframe), m_fired(fired)
{
}

G3D::DataHandler::NetworkInput::NetworkInput() :
	m_cframe(nullptr), m_fired(NULL)

{
}

void G3D::DataHandler::SetParameters(int frameCutoff, int futureFrames)
{
	m_pastFrames = frameCutoff;
	m_futureFrames = futureFrames;
}

G3D::DataHandler::NetworkInput::~NetworkInput()
{
}

G3D::CoordinateFrame G3D::DataHandler::NetworkInput::GetCFrame()
{
	return m_cframe;
}

bool G3D::DataHandler::NetworkInput::GetFired()
{
	return m_fired;
}

void G3D::DataHandler::NetworkInput::SetCFrame(CoordinateFrame cframe)
{
	m_cframe = cframe;
}

void G3D::DataHandler::NetworkInput::SetFired(bool fired)
{
	m_fired = fired;
}

void G3D::DataHandler::UpdateCframe(uint8 playerID, CoordinateFrame cframe, int frameNum)
{
	NetworkInput* input = new NetworkInput(playerID, cframe, false);
	m_DataInputs[0][m_currentFrame - frameNum + m_futureFrames][0][playerID].SetCFrame(cframe);
	m_unreadFrameBuffer->append(*input);
}

void G3D::DataHandler::UpdateFired(uint8 playerID, bool fired, int frameNum)
{
	NetworkInput* input = new NetworkInput(playerID, *new CoordinateFrame(), fired);
	m_DataInputs[0][m_currentFrame - frameNum + m_futureFrames][0][playerID].SetFired(fired);
	m_unreadFrameBuffer->append(*input);
}

G3D::DataHandler::DataHandler()
{
	for (int i = 0; i < m_pastFrames + 2; i++) {
		m_DataInputs->append(new Array<NetworkInput>);
	}
}

bool G3D::DataHandler::AllClientsFrame(int frameNum, int clientsConnected)
{
	return m_DataInputs[0][m_currentFrame - frameNum + 2]->length() == clientsConnected;
}

Array<G3D::DataHandler::NetworkInput>* G3D::DataHandler::GetFrameInputs(int frameNum)
{
	//if (m_DataInputs->empty) return NULL;
	return m_DataInputs[0][m_currentFrame - frameNum + 2];
}

void G3D::DataHandler::NewLeadingFrame(int frameNum, int clientsConnected)
{
	//Call per frame
	//debugPrintf("%d\t%d\n", m_leadingFrame, frameNum);
	m_currentFrame = frameNum;
	m_DataInputs->pop();
	Array<NetworkInput>* arr = new Array<NetworkInput>();
	//TODO: Does this actually make clientsConnected worth of network inputs or just the size of them
	arr->resize(clientsConnected, false);
	//convert to on function call
	m_DataInputs->append(arr);
}

void G3D::DataHandler::FlushBuffer()
{
	m_unreadFrameBuffer->clear();
}

bool G3D::DataHandler::CheckFrameAcceptable(int frameNum)
{
	return (m_currentFrame - frameNum + 2 >= m_pastFrames);
}

Array<G3D::DataHandler::NetworkInput>* G3D::DataHandler::GetFrameBuffer() {
	return m_unreadFrameBuffer;
}

G3D::DataHandler::NetworkInput* G3D::DataHandler::PredictFrame(int frameNum, uint8 playerID) {
	//dont worry about it
	Point3 translation_d = m_DataInputs[0][frameNum - m_currentFrame + m_futureFrames + 1][0][playerID].GetCFrame().translation - m_DataInputs[0][frameNum - m_currentFrame + m_futureFrames + 2][0][playerID].GetCFrame().translation;
	Matrix3 matrix_d = m_DataInputs[0][frameNum - m_currentFrame + m_futureFrames][0][playerID].GetCFrame().rotation - m_DataInputs[0][frameNum - m_currentFrame + m_futureFrames + 1][0][playerID].GetCFrame().rotation;
	CoordinateFrame* cframe = new CoordinateFrame(matrix_d, translation_d);
	NetworkInput* prediction = new NetworkInput(playerID, *cframe, m_DataInputs[0][frameNum - m_currentFrame + m_futureFrames][0][playerID].GetFired());
	m_DataInputs[0][frameNum - m_currentFrame + m_futureFrames][0][playerID] = *prediction;
	return prediction;
}