#include "TargetingExperiment.h"
#include "App.h"

namespace AbstractFPS
{
	void TargetingExperiment::onInit() {
		// Perform the three steps of starting a psychophysics experiment instance: describe, add conditions, init.
		// Step 1. Describe.
		clear();
		AbstractFPS::Descriptor desc;
		desc.mDescList.insert(std::pair<std::string, std::string>("experimentName", "TargetingExperiment"));
		// TODO: There must be a simpler way.
		desc.mDescList.insert(std::pair<std::string, std::string>("expMode", m_app->m_experimentConfig.expMode.c_str()));
		desc.mDescList.insert(std::pair<std::string, std::string>("subjectID", m_app->m_user.subjectID.c_str()));
		desc.mDescList.insert(std::pair<std::string, std::string>("taskType", m_app->m_experimentConfig.taskType.c_str()));
		desc.mDescList.insert(std::pair<std::string, std::string>("expVersion", m_app->m_experimentConfig.expVersion.c_str()));
		desc.mDescList.insert(std::pair<std::string, std::string>("sceneName", m_app->m_experimentConfig.sceneName.c_str()));
		desc.mDescList.insert(std::pair<std::string, std::string>("appendingDescription", m_app->m_experimentConfig.appendingDescription.c_str()));
		desc.mParamList.insert(std::pair<std::string, float>("targetFrameRate", m_app->m_experimentConfig.targetFrameRate));
		describeExperiment(desc);

		// Step 2. Add conditions.

		// load the experiment background scene.
		m_app->loadScene(m_app->m_experimentConfig.sceneName);
	}

	void TargetingExperiment::onGraphics3D(RenderDevice* rd, Array<shared_ptr<Surface> >& surface)
	{

	}

	void TargetingExperiment::onSimulation(RealTime rdt, SimTime sdt, SimTime idt)
	{

	}

	void TargetingExperiment::onUserInput(UserInput* ui)
	{

	}

	void TargetingExperiment::onGraphics2D(RenderDevice* rd, Array<shared_ptr<Surface2D>>& posed2D)
	{

	}
}
