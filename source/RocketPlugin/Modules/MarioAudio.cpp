#include "pch.h"
#include "MarioAudio.h"

#define ATTEN_ROLLOFF_FACTOR_EXP 0.0003f
#define ATTEN_ROLLOFF_FACTOR_LIN 0

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

	loadSoundFiles();
	soloud->set3dListenerUp(0, 0, 1.0f);
}

MarioAudio::~MarioAudio()
{
	//soloud->deinit();
	//delete soloud;
	soloud = nullptr;
}

static volatile float speedPlaybackFactor = 0.01f;
void MarioAudio::UpdateSounds(int soundMask,
	Vector sourcePos,
	Vector sourceVel,
	Vector listenerPos,
	Vector listenerAt,
	int *inSlideHandle,
	int *yahooHandle,
	uint32_t marioAction)
{
	if (marioAction == ACT_WALL_KICK_AIR)
	{
		marioSounds[SOUND_MARIO_UH_INDEX].wav.stop();
		marioSounds[SOUND_MARIO_DOH_INDEX].wav.stop();
	}

	int slideHandle = -1;
	for (auto i = 0; i < marioSounds.size(); i++)
	{
		auto marioSound = &marioSounds[i];
		if (marioSound->mask & soundMask)
		{
			float distance = utils.Distance(sourcePos, listenerPos);
			float volume = marioSound->volume - (distance * ATTEN_ROLLOFF_FACTOR_LIN) - pow(distance * ATTEN_ROLLOFF_FACTOR_EXP, 2);
			volume = volume <= 0.0f ? 0.0f : volume;
			volume *= 0.7f;

			if (marioSound->mask == SOUND_MOVING_TERRAIN_SLIDE)
			{
				// Handle sliding as a special case since it loops
				if (*inSlideHandle < 0)
				{
					slideHandle = soloud->play3d(marioSound->wav, sourcePos.X, sourcePos.Y, sourcePos.Z, 0, 0, 0, volume);
				}
				else
				{
					slideHandle = *inSlideHandle;
				}

				float speed = sqrt(sourceVel.X * sourceVel.X +
					sourceVel.Y * sourceVel.Y +
					sourceVel.Z * sourceVel.Z);
				auto playbackSpeed = marioSound->playbackSpeed;
				playbackSpeed += (speed * speedPlaybackFactor);
				soloud->setRelativePlaySpeed(slideHandle, marioSound->playbackSpeed);

			}
			else
			{
				int handle = soloud->play3d(marioSound->wav, sourcePos.X, sourcePos.Y, sourcePos.Z, 0, 0, 0, volume);

				auto playbackSpeed = marioSound->playbackSpeed;
				if (marioSound->playbackSpeed == 0.0f)
				{
					// Pick a random number
					float randPercentage = (rand() % 101) / 100.0f;
					float speedChange = 0.07f * randPercentage;
					playbackSpeed = 1.05f + speedChange;
				}

				if (marioSound->mask == SOUND_MARIO_YAHOO)
				{
					if (*yahooHandle >= 0)
					{
						soloud->stop(*yahooHandle);
					}
					*yahooHandle = handle;
				}

				soloud->setRelativePlaySpeed(handle, playbackSpeed);
			}

		}
	}

	if (*inSlideHandle >= 0 && slideHandle < 0)
	{
		soloud->stop(*inSlideHandle);
	}

	soloud->set3dListenerPosition(listenerPos.X, listenerPos.Y, listenerPos.Z);
	soloud->set3dListenerAt(listenerAt.X, listenerAt.Y, listenerAt.Z);
	soloud->update3dAudio();

	*inSlideHandle = slideHandle;
}

void MarioAudio::CheckAndModulateSounds()
{
	// Check if first sound exists, and assume all do if the first does
	//std::string soundPath = SOUND_DIR + marioSounds[0].wavPath;
	//if (utils.FileExists(soundPath))
	//	return;
	//
	//// Sound file doesn't exist, let's extract the sounds from the ROM
	//std::string extractScript = EXTRACT_ASSETS_SCRIPT;
	//int result = system(extractScript.c_str());
	//
	//loadSoundFiles();
}

void MarioAudio::loadSoundFiles()
{
	std::string soundDir = SOUND_DIR;
	for (auto i = 0; i < marioSounds.size(); i++)
	{
		std::string soundPath = soundDir + marioSounds[i].wavPath;
		marioSounds[i].wav.load(soundPath.c_str());
	}
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
