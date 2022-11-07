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

/*************************ClientDataHandler***********************************/
G3D::ClientDataInput::ClientDataInput() : DataInput() , m_exists(false){}
G3D::ClientDataInput::ClientDataInput(uint8 playerID, CoordinateFrame cframe, bool givenCFrame) : DataInput(playerID, cframe), m_exists(givenCFrame){}
G3D::ClientDataInput::~ClientDataInput() {}

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

void G3D::ServerDataHandler::UpdateCframe(uint8 playerID, CoordinateFrame cframe, int frameNum)
{
	if (CheckFrameAcceptable(frameNum)) {
		ServerDataInput* input = new ServerDataInput(playerID, cframe, false);
		m_DataInputs[0][m_currentFrame - frameNum + m_futureFrames][0][playerID].SetCFrame(cframe);
		m_unreadFrameBuffer->append(*input);
	}
}

G3D::ServerDataHandler::ServerDataHandler()
{
	for (int i = 0; i < m_pastFrames + m_futureFrames + 1; i++) {
		m_DataInputs->append(new Array<ServerDataInput>);
	}
}

G3D::ServerDataHandler::~ServerDataHandler() {}


/********************************ClientsDataHandler************************************/
//TODO: update on demand

G3D::ClientDataHandler::ClientDataHandler()
{
	for (int i = 0; i < m_pastFrames + m_futureFrames + 1; i++) {
		m_DataInputs->append(new Array<ClientDataInput>);
	}
}
	
void G3D::ClientDataHandler::NewCurrentFrame(int frameNum, int clientsConnected)
{
	//Call per frame
	//debugPrintf("%d\t%d\n", m_leadingFrame, frameNum);
	m_currentFrame = frameNum;
	m_DataInputs->pop();
	Array<ClientDataInput>* arr = new Array<ClientDataInput>();
	//TODO: Does this actually make clientsConnected worth of network inputs or just the size of them
	for (int i = 0; i < 8; i++) {
		arr->append(*new ClientDataInput());
	}
	//convert to on function call
	m_DataInputs->insert(0, arr);
}

void G3D::ServerDataHandler::UpdateCframe(uint8 playerID, CoordinateFrame cframe, int frameNum)
{
	if (CheckFrameAcceptable(frameNum)) {
		ClientDataInput* input = new ClientDataInput(playerID, cframe, false);
		m_DataInputs[0][m_currentFrame - frameNum + m_futureFrames][0][playerID].SetCFrame(cframe);
	}
}

G3D::ClientDataHandler::~ClientDataHandler() {}

bool G3D::ClientDataHandler::DataPointExists(int frameNum, uint8 playerID) {
	if (CheckFrameAcceptable(frameNum)) {
		return m_DataInputs[0][m_currentFrame - frameNum + m_futureFrames][0][playerID].m_exists;
	}
	else
		return false;
}

ClientDataInput* G3D::ClientDataHandler::PredictEntity(int frameNum, uint8 playerID){
	//check if current frame exists. If real do nothing. if not real predict
	ClientDataInput* prediction = new ClientDataInput();
	if (!DataPointExists(frameNum, playerID) && CheckFrameAcceptable(frameNum)) {//does not update if value if it is out of bounds or if it exists
		if (type == predictionType::NONE) {
			//todo: perpetuate current position
		}
		else if (type == predictionType::LINEAR && DataPointExists(frameNum + 1, playerID) && DataPointExists(frameNum + 2, playerID)) {
			prediction = PredictFrameLinear(frameNum, playerID);
		}
		else if (type == predictionType::QUADRATIC && DataPointExists(frameNum + 1, playerID) && DataPointExists(frameNum + 2, playerID) &&
			DataPointExists(frameNum + 3, playerID)) {
			prediction = PredictFrameQuadratic(frameNum, playerID);
		}
	}

	return prediction;
}

ClientDataInput* G3D::ClientDataHandler::PredictFrameLinear(int frameNum, uint8 playerID) {

	//p_new = p_current+(t_current-t_old)(p_current-p_old)
	//p_new = p_current + (t_current - t_old)(p_current - p_old) + ½ * ((p_current - p_old) - (p_old - p_old2) / old) * (t_current - t_old) ^ 2
	Point3 p1 = m_DataInputs[0][m_currentFrame - frameNum + m_futureFrames + 1][0][playerID].GetCFrame().translation;
	Point3 p2 = m_DataInputs[0][m_currentFrame - frameNum + m_futureFrames + 2][0][playerID].GetCFrame().translation;

	Matrix3 m1 = m_DataInputs[0][m_currentFrame - frameNum + m_futureFrames + 1][0][playerID].GetCFrame().rotation;
	Matrix3 m2 = m_DataInputs[0][m_currentFrame - frameNum + m_futureFrames + 2][0][playerID].GetCFrame().rotation;


	//new data point from last two datapoints
	Point3 p0 = 2 * p1 - p2;
	Matrix3 m0 = 2 * m1 - m2;
	//new cframe from calculated deltas
	CoordinateFrame* cframe = new CoordinateFrame(m0, p0);
	//new network input based on cframe, previous frames get fired (keep or change to false by default)
	ClientDataInput* prediction = new ClientDataInput(playerID, *cframe, true);
	//add predicted frame to datainputs
	m_DataInputs[0][m_currentFrame - frameNum + m_futureFrames][0][playerID] = *prediction;
	//return the created networkinput
	return prediction;//prediction;
}
ClientDataInput* G3D::ClientDataHandler::PredictFrameQuadratic(int frameNum, uint8 playerID) {
	//BUG TODO: accelerates to infinity if no new frame is found add max speed to calculations

	//p_new = p_current+(t_current-t_old)(p_current-p_old)
	//p_new = p_current + (t_current - t_old)(p_current - p_old) + ½ * ((p_current - p_old) - (p_old - p_old2) / (t_current - t_old)) * (t_old - t_old2) ^ 2
	Point3 p1 = m_DataInputs[0][m_currentFrame - frameNum + m_futureFrames + 1][0][playerID].GetCFrame().translation;
	Point3 p2 = m_DataInputs[0][m_currentFrame - frameNum + m_futureFrames + 2][0][playerID].GetCFrame().translation;
	Point3 p3 = m_DataInputs[0][m_currentFrame - frameNum + m_futureFrames + 3][0][playerID].GetCFrame().translation;

	Matrix3 m1 = m_DataInputs[0][m_currentFrame - frameNum + m_futureFrames + 1][0][playerID].GetCFrame().rotation;
	Matrix3 m2 = m_DataInputs[0][m_currentFrame - frameNum + m_futureFrames + 2][0][playerID].GetCFrame().rotation;
	Matrix3 m3 = m_DataInputs[0][m_currentFrame - frameNum + m_futureFrames + 3][0][playerID].GetCFrame().rotation;


	//form new data point from last 3 data points
	Point3 p0 = 2 * p1 - p2 + (.5* (p1 - 2 * p2 + p3));
	Matrix3 m0 = 2 * m1 - m2 + (.5 * (m1 - 2 * m2 + m3));
	//new cframe from calculated deltas
	CoordinateFrame* cframe = new CoordinateFrame(m0, p0);
	//new network input based on cframe, previous frames get fired (keep or change to false by default)
	ClientDataInput* prediction = new ClientDataInput(playerID, *cframe, true);
	//add predicted frame to datainputs
	m_DataInputs[0][m_currentFrame - frameNum + m_futureFrames][0][playerID] = *prediction;
	//return the created networkinput
	return prediction;//prediction;
}