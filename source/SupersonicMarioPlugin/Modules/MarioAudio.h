#pragma once

#include "soloud.h"
#include "soloud_wav.h"
#include "Utils.h"
#include "MarioConfig.h"

#define ASSETS_DIR utils.GetBakkesmodFolderPath() + "data\\assets\\"
#define SOUND_DIR ASSETS_DIR + "sound\\samples\\"

#define ACT_WALL_KICK_AIR								0x03000886
#define SOUND_MARIO_UH_INDEX							17
#define SOUND_MARIO_DOH_INDEX							18

#define ACT_FORWARD_ROLLOUT								0x010008A6
#define SOUND_ACTION_TERRAIN_BODY_HIT_GROUND_INDEX		20

#define SOUND_MARIO_YAH							0x00000001
#define SOUND_MARIO_WAH							0x00000002
#define SOUND_MARIO_HOO							0x00000004
#define SOUND_MARIO_YAHOO						0x00000008
#define SOUND_MARIO_HOOHOO						0x00000010
#define SOUND_MARIO_PUNCH_YAH					0x00000020
#define SOUND_MARIO_PUNCH_WAH					0x00000040
#define SOUND_MARIO_PUNCH_HOO					0x00000080
#define SOUND_ACTION_TERRAIN_STEP				0x00000100
#define SOUND_ACTION_SPIN						0x00000200
#define SOUND_ACTION_TERRAIN_HEAVY_LANDING      0x00000400
#define SOUND_MARIO_GROUND_POUND_WAH            0x00000800
#define SOUND_ACTION_SIDE_FLIP_UNK              0x00001000
#define SOUND_MARIO_HAHA                        0x00002000
#define SOUND_ACTION_TERRAIN_LANDING            0x00004000
#define SOUND_MOVING_TERRAIN_SLIDE              0x00008000
#define SOUND_ACTION_BONK						0x00010000
#define SOUND_MARIO_UH                          0x00020000
#define SOUND_MARIO_DOH                         0x00040000
#define SOUND_MARIO_OOOF                        0x00080000
#define SOUND_ACTION_TERRAIN_BODY_HIT_GROUND    0x00100000
#define SOUND_MARIO_ATTACKED                    0x00200000

typedef struct MarioSound_t
{
	uint32_t mask;
	std::string wavPath;
	float playbackSpeed = 1.0f;
	float volume = 1.0f;
	SoLoud::Wav wav;
	bool playing = false;
} MarioSound;

class MarioAudio
{
public:
	MarioAudio();
	~MarioAudio();
	void UpdateSounds(int soundMask,
		Vector sourcePos,
		Vector sourceVel,
		Vector listenerPos,
		Vector listenerAt,
		int *inSlideHandle,
		int *yahooHandle,
		uint32_t marioAction);
private:
	void loadSoundFiles();

public:
	int MasterVolume = 70;
private:
	SoLoud::Wav testWav;
	std::vector<MarioSound> marioSounds = {
		{ SOUND_MARIO_YAH,							"sfx_mario/02.wav",					0.91f},
		{ SOUND_MARIO_WAH,							"sfx_mario/01.wav",					0.85f},
		{ SOUND_MARIO_HOO,							"sfx_mario/00.wav",					1.08f},
		{ SOUND_MARIO_YAHOO,						"sfx_mario/04.wav",					1.0f}, // changed file, forgot values
		{ SOUND_MARIO_HOOHOO,						"sfx_mario_peach/01.wav",			1.0f}, // 1.0f then 1.21f
		{ SOUND_MARIO_PUNCH_YAH,					"sfx_mario/02.wav",					0.91f},
		{ SOUND_MARIO_PUNCH_WAH,					"sfx_mario/01.wav",					0.85f},
		{ SOUND_MARIO_PUNCH_HOO,					"sfx_mario_peach/09.wav",			1.05f},
		{ SOUND_ACTION_TERRAIN_STEP,				"sfx_terrain/01_step_grass.wav",	0.0f,		0.30f},
		{ SOUND_ACTION_SPIN,						"sfx_1/00_twirl.wav",				1.14f,		0.85f},
		{ SOUND_ACTION_TERRAIN_HEAVY_LANDING,		"sfx_1/05_heavy_landing.wav",		1.13f},
		{ SOUND_MARIO_GROUND_POUND_WAH,				"sfx_mario/07.wav",					0.86f},
		{ SOUND_ACTION_SIDE_FLIP_UNK,				"sfx_1/00_twirl.wav",				1.14f},
		{ SOUND_MARIO_HAHA,							"sfx_mario/03.wav",					1.0f},
		{ SOUND_ACTION_TERRAIN_LANDING,				"sfx_terrain/01_land_grass.wav",	1.0f,		0.30f}, // duplicated 01_step_grass.wav and resampled
		{ SOUND_MOVING_TERRAIN_SLIDE,				"sfx_4/00.wav",						1.05f,		0.30f},
		{ SOUND_ACTION_BONK,						"sfx_1/02_hand_touch.wav",			1.0f,		0.10f}, // Reverb stuff
		{ SOUND_MARIO_UH,							"sfx_mario/05.wav",					1.0f},
		{ SOUND_MARIO_DOH,							"sfx_mario/10.wav",					1.09f},
		{ SOUND_MARIO_OOOF,							"sfx_mario/0B.wav",					1.0f}, // -9% pitch
		{ SOUND_ACTION_TERRAIN_BODY_HIT_GROUND,		"sfx_terrain/01_land_grass.wav",	1.0f,		0.55f}, // duplicated 01_step_grass.wav and resampled
		{ SOUND_MARIO_ATTACKED,						"sfx_mario/0A.wav",					1.0f},
	};
	Utils utils;
};
