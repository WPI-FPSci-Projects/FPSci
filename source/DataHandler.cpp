#include "DataHandler.h"
/**********************ServerDataInput********************/
G3D::ServerDataInput::ServerDataInput() 
	: m_cframe(CoordinateFrame()), m_valid(false)
{
}

G3D::ServerDataInput::ServerDataInput(CoordinateFrame cframe, bool valid) 
	: m_cframe(cframe), m_valid(valid){

}

G3D::ServerDataInput::~ServerDataInput() {}

G3D::CoordinateFrame G3D::ServerDataInput::GetCFrame()
{
	return m_cframe;
}

void G3D::ServerDataInput::SetCFrame(CoordinateFrame cframe)
{
	m_cframe = cframe;
}
bool G3D::ServerDataInput::GetValid() {
	return m_valid;
}

void G3D::ServerDataInput::SetValid(bool valid) {
	m_valid = valid;
}

/**********************ServerDataHandler**************************/

void G3D::ServerDataHandler::NewCurrentFrame(int frameNum)
{
	//Call per frame
	m_currentFrame = frameNum;
	for (String clientName : m_clientLastValid->getKeys()) {
		m_DataInputs->get(clientName)->pop();
		ServerDataInput* newDataPoint = new ServerDataInput();
		m_DataInputs->get(clientName)->insert(0, *newDataPoint);
	}
}

void G3D::ServerDataHandler::UpdateFired(String playerID, int frameNum)
{
	if (CheckFrameAcceptable(frameNum)) {
		auto newFireInput = new ServerFireInput();
		newFireInput->m_fired = true;
		newFireInput->m_playerID = playerID;
		newFireInput->m_frameNum = frameNum;
		m_unreadFiredbuffer->push(*newFireInput);
	}
}

void G3D::ServerDataHandler::UpdateCframe(String playerID, CoordinateFrame cframe, int frameNum, bool fromClient)
{
	if (CheckFrameAcceptable(frameNum)) {
		ServerDataInput* input = new ServerDataInput(cframe, false);
		m_DataInputs->get(playerID)->getCArray()[m_currentFrame - frameNum].SetCFrame(cframe);
		if (fromClient) {
			m_unreadCFrameBuffer->push(*input);
		}
		if (frameNum > m_clientLatestFrame->get(playerID) || (!frameNum > m_clientLatestFrame->get(playerID) + 100000
			|| !frameNum > m_clientLatestFrame->get(playerID) - 100000)) {
			m_clientLatestFrame->set(playerID, frameNum);
		}
	}
}

void G3D::ServerDataHandler::ValidateData(String playerID, int frameNum) {
	m_DataInputs->get(playerID)->getCArray()[m_currentFrame - frameNum].SetValid(true);
	int i = m_clientLastValid->get(playerID);
	if (frameNum >= i){
		m_clientLastValid->get(playerID) = frameNum;
	}
}

int G3D::ServerDataHandler::lastValidFromFrameNum(String playerID, int frameNum){
	for (int i = m_currentFrame - frameNum; i < m_pastFrames; i++) {
		if (m_DataInputs->get(playerID)->getCArray()[i].GetValid()) {
			return m_currentFrame - i;
		}
	}
	return -1;
}

G3D::ServerDataHandler::ServerDataHandler()
{
	m_DataInputs = new Table<String, Array<ServerDataInput>*>;
	m_clientLastValid = new Table<String, int>;
	m_clientLatestFrame = new Table<String, int>;
	m_unreadCFrameBuffer = new Array<ServerDataInput>;
	m_unreadFiredbuffer = new Array<ServerFireInput>;
}

G3D::ServerDataHandler::~ServerDataHandler() {}

CoordinateFrame G3D::ServerDataHandler::GetCFrame(int frameNum, String playerID) {
	if (CheckFrameAcceptable(frameNum)) {
		return m_DataInputs->get(playerID)->getCArray()[m_currentFrame - frameNum].GetCFrame();
	}
	else {
		return *new CoordinateFrame();
	}
}

void G3D::ServerDataHandler::SetParameters(int frameCutoff)
{
	m_pastFrames = frameCutoff;
}

bool G3D::ServerDataHandler::CheckFrameAcceptable(int frameNum)
{
	//i have a feeling this may cause issues in the future
	return (m_currentFrame - frameNum >= 0 && m_currentFrame - frameNum <= m_pastFrames);
}

void G3D::ServerDataHandler::AddNewClient(String playerID) {
	m_DataInputs->set(playerID, new Array<ServerDataInput>);// ->resize(m_pastFrames, false);
	m_DataInputs->get(playerID)->resize(m_pastFrames, false);
	m_clientLastValid->set(playerID, 0);
	m_clientLatestFrame->set(playerID, 0);
}

void G3D::ServerDataHandler::DeleteClient(String playerID) {
	m_DataInputs->remove(playerID);
	m_clientLastValid->remove(playerID);
	m_clientLatestFrame->remove(playerID);
}

/********UnreadFrameBuffer***********/
Array<G3D::ServerDataInput>* G3D::ServerDataHandler::GetCFrameBuffer() {
	//return buffer of all frames that have not yet been applied
	return m_unreadCFrameBuffer;
}

void G3D::ServerDataHandler::FlushCFrameBuffer()
{
	//clears the buffer of unapplied networkInputs must be called every frame
	m_unreadCFrameBuffer->clear();
}

Array<ServerFireInput>* ServerDataHandler::GetFiredBuffer()
{
	//return buffer of all frames that have not yet been applied
	return m_unreadFiredbuffer;
}

void G3D::ServerDataHandler::FlushFiredBuffer()
{
	//clears the buffer of unapplied networkInputs must be called every frame
	m_unreadFiredbuffer->clear();
}


/********************************ClientsDataHandler************************************/

G3D::ClientDataHandler::ClientDataHandler()
{

}

void G3D::ClientDataHandler::AddNewClient(String playerID) {
	m_DataInputs->set(playerID, new Array<CoordinateFrame>);
	for (int i = 0; i < static_cast<int>(m_type) + 1; i++) {
		m_DataInputs->get(playerID)->push(*new CoordinateFrame());
	}
	m_frameLag->set(playerID, 0);
	m_clientVectors->set(playerID, *new Vector3());
	m_clientHeading->set(playerID, *new Matrix3());
}

void G3D::ClientDataHandler::UpdateCframe(String playerID, CoordinateFrame cframe, uint32 currentFrame, uint32 packetFrame)
{
	m_DataInputs->get(playerID)->pop();
	m_DataInputs->get(playerID)->insert(0, cframe);
	m_frameLag->get(playerID) = currentFrame - packetFrame;
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

CoordinateFrame* G3D::ClientDataHandler::PredictEntityFrame(CoordinateFrame currentKnownLocation, String playerID) {
	//new cframe from adding vector to last known position
	//Possibility to fall out of map
	CoordinateFrame* prediction = new CoordinateFrame(
		currentKnownLocation.rotation + m_clientHeading[0][playerID],
		currentKnownLocation.translation +  m_clientVectors[0][playerID]
	);

	return prediction;
}

CoordinateFrame* G3D::ClientDataHandler::RecalulateClient(String playerID, CoordinateFrame cframe) {
	CoordinateFrame* prediction = new CoordinateFrame(
		cframe.rotation + m_clientHeading[0][playerID] * m_frameLag[0][playerID],
		cframe.translation + m_frameLag[0][playerID] * m_clientVectors[0][playerID]
	);

	return prediction;
}
