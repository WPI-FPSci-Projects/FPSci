#include "DataHandler.h"

G3D::DataInput::DataInput(uint8 playerID, CoordinateFrame cframe) :
	m_playerID(playerID), m_cframe(cframe)
{
}

G3D::DataInput::DataInput() :
	m_cframe(CFrame()), m_playerID(0)
{
}

G3D::DataInput::~DataInput()
{
}

G3D::CoordinateFrame G3D::DataInput::GetCFrame()
{
	return m_cframe;
}

void G3D::DataInput::SetCFrame(CoordinateFrame cframe)
{
	m_cframe = cframe;
}

/**********************ServerDataInput********************/
G3D::ServerDataInput::ServerDataInput() : m_fired(false), DataInput()
{
}

G3D::ServerDataInput::ServerDataInput(uint8 playerID, CoordinateFrame cframe, bool fired) : m_fired(fired), DataInput(playerID, cframe){

}

G3D::ServerDataInput::~ServerDataInput() {}

bool G3D::ServerDataInput::GetFired() {
	return m_fired;
}
void G3D::ServerDataInput::SetFired(bool fired) {
	m_fired = fired;
}

/*******************DataHandler**************************/
void G3D::DataHandler::SetParameters(int frameCutoff, int futureFrames)
{
	m_pastFrames = frameCutoff;
	m_futureFrames = futureFrames;
}

void G3D::DataHandler::UpdateCframe(uint8 playerID, CoordinateFrame cframe, int frameNum)
{
	if (playerID == static_cast<uint8>(-1))
	{
		debugPrintf("Invalid playerID in UpdateCframe\n");
		return;
	}
	if (CheckFrameAcceptable(frameNum)) {
		DataInput* input = new DataInput(playerID, cframe);
		m_DataInputs[0][m_currentFrame - frameNum + m_futureFrames][0][playerID].SetCFrame(cframe);
	}
}

G3D::DataHandler::DataHandler()
{
	for (int i = 0; i < m_pastFrames + m_futureFrames + 1; i++) {
		m_DataInputs->append(new Array<DataInput>);
	}
}

G3D::DataHandler::~DataHandler() {}

bool G3D::DataHandler::AllClientsFrame(int frameNum, int clientsConnected)
{
	return m_DataInputs[0][m_currentFrame - frameNum + 2]->length() == clientsConnected;
}

Array<G3D::DataInput>* G3D::DataHandler::GetFrameInputs(int frameNum)
{
	//if (m_DataInputs->empty) return NULL;
	return m_DataInputs[0][m_currentFrame - frameNum + 2];
}

void G3D::DataHandler::NewCurrentFrame(int frameNum, int clientsConnected)
{
	//Call per frame
	//debugPrintf("%d\t%d\n", m_leadingFrame, frameNum);
	m_currentFrame = frameNum;
	m_DataInputs->pop();
	Array<DataInput>* arr = new Array<DataInput>();
	//TODO: Does this actually make clientsConnected worth of network inputs or just the size of them
	for (int i = 0; i < 8; i++) {
		arr->append(*new DataInput());
	}
	//convert to on function call
	m_DataInputs->insert(0, arr);
}

bool G3D::DataHandler::CheckFrameAcceptable(int frameNum)
{
	//i have a feeling this may cause issues in the future
	return (m_currentFrame - frameNum + m_futureFrames >= 0 && m_currentFrame - frameNum + m_futureFrames < m_pastFrames);
}


/**********************ServerDataHandler**************************/
//TODO: unqiue newgetFrameInputs // also clientdatahandler

void G3D::ServerDataHandler::UpdateFired(uint8 playerID, bool fired, int frameNum)
{
	if (CheckFrameAcceptable(frameNum)) {
		ServerDataInput* input = new ServerDataInput(playerID, *new CoordinateFrame(), fired);
		m_DataInputs[0][m_currentFrame - frameNum + m_futureFrames][0][playerID].SetFired(fired);
		m_unreadFrameBuffer->append(*input);
	}
}

Array<G3D::ServerDataInput>* G3D::ServerDataHandler::GetFrameBuffer() {
	//return buffer of all frames that have not yet been applied
	return m_unreadFrameBuffer;
}

void G3D::ServerDataHandler::FlushBuffer()
{
	//clears the buffer of unapplied networkInputs must be called every frame
	m_unreadFrameBuffer->clear();
}

void G3D::ServerDataHandler::NewCurrentFrame(int frameNum, int clientsConnected)
{
	//Call per frame
	//debugPrintf("%d\t%d\n", m_leadingFrame, frameNum);
	m_currentFrame = frameNum;
	m_DataInputs->pop();
	Array<ServerDataInput>* arr = new Array<ServerDataInput>();
	//TODO: Does this actually make clientsConnected worth of network inputs or just the size of them
	for (int i = 0; i < 8; i++) {
		arr->append(*new ServerDataInput());
	}
	//convert to on function call
	m_DataInputs->insert(0, arr);
}

void G3D::ServerDataHandler::UpdateCframe(uint8 playerID, CoordinateFrame cframe, int frameNum, bool fromClient)
{
	if (CheckFrameAcceptable(frameNum)) {
		ServerDataInput* input = new ServerDataInput(playerID, cframe, false);
		m_DataInputs[0][m_currentFrame - frameNum + m_futureFrames][0][playerID].SetCFrame(cframe);
		if (fromClient) {
			m_unreadFrameBuffer->append(*input);
		}
	}
}

G3D::ServerDataHandler::ServerDataHandler()
{
	for (int i = 0; i < m_pastFrames + m_futureFrames + 1; i++) {
		m_DataInputs->append(new Array<ServerDataInput>);
	}
}

G3D::ServerDataHandler::~ServerDataHandler() {}

CoordinateFrame G3D::ServerDataHandler::GetCFrame(int frameNum, int playerID) {
	if (CheckFrameAcceptable(frameNum)) {
		return m_DataInputs[0][m_currentFrame - frameNum + m_futureFrames + 2][0][playerID].GetCFrame();
	}
	else {
		return *new CoordinateFrame();
	}
}


/********************************ClientsDataHandler************************************/
//TODO: update on demand

G3D::ClientDataHandler::ClientDataHandler()
{
	for (int i = 0; i < static_cast<int>(m_type)+1; i++) {
		m_DataInputs->append(new Array<CoordinateFrame>);
	}

}

void G3D::ClientDataHandler::UpdateCframe(uint8 playerID, CoordinateFrame cframe, uint32 currentFrame, uint32 packetFrame)
{
	m_DataInputs[0][playerID]->pop();
	m_DataInputs[0][playerID]->insert(0, cframe);
	m_frameLag[0][playerID] = currentFrame - packetFrame;
	Vector3* newVector = new Vector3();
	Matrix3* newHeading = new Matrix3();
	switch (m_type)
	{
	case predictionType::LINEAR: {
		Point3 p1 = m_DataInputs[0][playerID][0][0].translation;
		Point3 p2 = m_DataInputs[0][playerID][0][1].translation;

		Matrix3 m1 = m_DataInputs[0][playerID][0][0].rotation;
		Matrix3 m2 = m_DataInputs[0][playerID][0][1].rotation;

		*newVector = p1 - p2;
		*newHeading = m1 - m2;
		break;
	}
	case predictionType::QUADRATIC: {
		Point3 p1 = m_DataInputs[0][playerID][0][0].translation;
		Point3 p2 = m_DataInputs[0][playerID][0][1].translation;
		Point3 p3 = m_DataInputs[0][playerID][0][2].translation;

		Matrix3 m1 = m_DataInputs[0][playerID][0][0].rotation;
		Matrix3 m2 = m_DataInputs[0][playerID][0][1].rotation;
		Matrix3 m3 = m_DataInputs[0][playerID][0][2].rotation;

		*newVector = p1 - p2 + (.5 * (p1 - 2 * p2 + p3));
		*newHeading = m1 - m2 + (.5 * (m1 - 2 * m2 + m3));
		break;
	}
	}
	m_clientVectors[0][playerID] = *newVector;
	m_clientHeading[0][playerID] = *newHeading;
	RecalulateClient(playerID, cframe);
}

G3D::ClientDataHandler::~ClientDataHandler() {}

CoordinateFrame* G3D::ClientDataHandler::PredictEntityFrame(CoordinateFrame currentKnownLocation, uint8 playerID, int moveRate) {
	//new cframe from adding vector to last known position
	//Possibility to fall out of map
	CoordinateFrame* prediction = new CoordinateFrame(
		currentKnownLocation.rotation + m_clientHeading[0][playerID],
		currentKnownLocation.translation +  m_clientVectors[0][playerID]
	);

	return prediction;
}

CoordinateFrame* G3D::ClientDataHandler::RecalulateClient(uint8 playerID, CoordinateFrame cframe) {
	CoordinateFrame* prediction = new CoordinateFrame(
		cframe.rotation + m_clientHeading[0][playerID] * m_frameLag[0][playerID],
		cframe.translation + m_frameLag[0][playerID] * m_clientVectors[0][playerID]
	);

	return prediction;
}
