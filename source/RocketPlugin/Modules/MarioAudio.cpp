#include "pch.h"
#include "MarioAudio.h"

#define ATTEN_ROLLOFF_FACTOR 0.0003f

static SoLoud::Soloud* soloud = nullptr;

void onExit(void)
{
	if (soloud != nullptr)
	{
		// Ensure the process fully ends... not great but here we are
		abort();
		soloud->deinit();
	}
}

MarioAudio::MarioAudio()
{
	atexit(onExit);
	soloud = new SoLoud::Soloud();
	soloud->init(SoLoud::Soloud::FLAGS::LEFT_HANDED_3D);

	std::string soundDir = utils.GetBakkesmodFolderPath() + "data\\assets\\sound\\";
	for (auto i = 0; i < marioSounds.size(); i++)
	{
		std::string soundPath = soundDir + marioSounds[i].wavPath;
		marioSounds[i].wav.load(soundPath.c_str());
	}
	soloud->set3dListenerUp(0, 0, 1.0f);
}

MarioAudio::~MarioAudio()
{
	soloud->deinit();
	delete soloud;
	soloud = nullptr;
}

void MarioAudio::UpdateSounds(int soundMask,
	Vector sourcePos,
	Vector listenerPos,
	Vector listenerAt,
	float aVelX,
	float aVelY,
	float aVelZ)
{
	if (soundMask == 0)
		return;
	for (auto i = 0; i < marioSounds.size(); i++)
	{
		auto marioSound = &marioSounds[i];
		if (marioSound->mask & soundMask)
		{
			float distance = utils.Distance(sourcePos, listenerPos);
			float volume = 1.0f - pow(distance * ATTEN_ROLLOFF_FACTOR, 2);
			volume = volume <= 0.0f ? 0.0f : volume;

			int handle = soloud->play3d(marioSound->wav, sourcePos.X, sourcePos.Y, sourcePos.Z, aVelX, aVelY, aVelZ, volume);
			
			auto playbackSpeed = marioSound->playbackSpeed;
			if (marioSound->playbackSpeed == 0.0f)
			{
				// Pick a random number
				float randPercentage = (rand() % 101) / 100.0f;
				float speedChange = 0.07f * randPercentage;
				playbackSpeed = 1.05f + speedChange;
			}
			soloud->setRelativePlaySpeed(handle, playbackSpeed);
		}
	}
	soloud->set3dListenerPosition(listenerPos.X, listenerPos.Y, listenerPos.Z);
	soloud->set3dListenerAt(listenerAt.X, listenerAt.Y, listenerAt.Z);
	soloud->update3dAudio();
}


std::pair<size_t, size_t> MarioAudio::resample(double factor,
	float* inBuffer,
	size_t inBufferLen,
	bool lastFlag,
	float* outBuffer,
	size_t outBufferLen)
{
	size_t idone, odone;
	soxr_quality_spec_t q_spec;

	q_spec = soxr_quality_spec(SOXR_HQ, SOXR_VR);
	soxrHandle.reset(soxr_create(1, factor, 1, 0, 0, &q_spec, 0));
	soxr_process(soxrHandle.get(),
		inBuffer, (lastFlag ? ~inBufferLen : inBufferLen), &idone,
		outBuffer, outBufferLen, &odone);
	return { idone, odone };
}
