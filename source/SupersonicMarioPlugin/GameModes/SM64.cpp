#include "SM64.h"

#include <thread>
#include <chrono>
#include <functional>
#include <WinUser.h>

// CONSTANTS
#define RL_YAW_RANGE 64692
#define CAR_OFFSET_Z 45.0f
#define SM64_TEXTURE_SIZE (4 * SM64_TEXTURE_WIDTH * SM64_TEXTURE_HEIGHT)
#define WINGCAP_VERTEX_INDEX 750
#define ATTACK_BOOST_DAMAGE 0.20f
#define GROUND_POUND_BALL_RADIUS 200.0f
#define GROUND_POUND_PINCH_VELOCITY 2708.0f
#define ATTACK_BALL_RADIUS 261.0f
#define KICK_BALL_VEL_HORIZ 583.0f
#define KICK_BALL_VEL_VERT 305.0f
#define PUNCH_BALL_VEL_HORIZ 1388.0f
#define PUNCH_BALL_VEL_VERT 250.0f
#define DIVE_BALL_VEL_HORIZ 639.0f
#define DIVE_BALL_VEL_VERT 166.6f
#define FLY_BALL_VEL 500.0f
#define IM_COL32_ERROR_BANNER (ImColor(211,  47,  47, 255))

#define OCTANE_ID 23
#define BREAKOUT_ID 22
#define DOMINUS_ID 403
#define FENNEC_ID 4284

inline void tickMarioInstance(SM64MarioInstance* marioInstance,
	CarWrapper car,
	SM64* instance);

void MessageReceived(char* buf, int len);

SM64* self = nullptr;

SM64::SM64(std::shared_ptr<GameWrapper> gw, std::shared_ptr<CVarManagerWrapper> cm, BakkesMod::Plugin::PluginInfo exports)
{
	using namespace std::placeholders;
	gameWrapper = gw;
	cvarManager = cm;

	InitSM64();
	gameWrapper->RegisterDrawable(std::bind(&SM64::OnRender, this, _1));
	gameWrapper->HookEventPost("Function TAGame.EngineShare_TA.EventPostPhysicsStep", bind(&SM64::moveCarToMario, this, _1));
	gameWrapper->HookEventPost("Function TAGame.NetworkInputBuffer_TA.ClientAckFrame", bind(&SM64::moveCarToMario, this, _1));
	gameWrapper->HookEventPost("Function GameEvent_Soccar_TA.PostGoalScored.Tick", bind(&SM64::onGoalScored, this, _1));
	gameWrapper->HookEventPost("Function TAGame.GameEvent_Soccar_TA.EventMatchWinnerSet", bind(&SM64::onMatchWinnerSet, this, _1));

	attackBoostDamage = ATTACK_BOOST_DAMAGE;

	groundPoundPinchVel = GROUND_POUND_PINCH_VELOCITY;
	attackBallRadius = ATTACK_BALL_RADIUS;
	kickBallVelHoriz = KICK_BALL_VEL_HORIZ;
	kickBallVelVert = KICK_BALL_VEL_VERT;
	punchBallVelHoriz = PUNCH_BALL_VEL_HORIZ;
	punchBallVelVert = PUNCH_BALL_VEL_VERT;
	diveBallVelHoriz = DIVE_BALL_VEL_HORIZ;
	diveBallVelVert = DIVE_BALL_VEL_VERT;
	flyBallVel = FLY_BALL_VEL;

	matchSettings.bljSetup.bljState = SM64_BLJ_STATE_DISABLED;
	matchSettings.bljSetup.bljVel = 0;

	typeIdx = std::make_unique<std::type_index>(typeid(*this));

	HookEventWithCaller<CarWrapper>(
		vehicleInputCheck,
		[this](const CarWrapper& caller, void* params, const std::string&) {
			onSetVehicleInput(caller, params);
		});

	HookEventWithCaller<ServerWrapper>(
		nameplateTickCheck,
		[this](const ServerWrapper& caller, void* params, const std::string&) {
			onNameplateTick(caller, params);
		});

	HookEventWithCaller<ServerWrapper>(
		initialCharacterSpawnCheck,
		[this](const ServerWrapper& caller, void* params, const std::string&) {
			onCharacterSpawn(caller);
		});
	HookEventWithCaller<ServerWrapper>(
		CharacterSpawnCheck,
		[this](const ServerWrapper& caller, void* params, const std::string&) {
			onCharacterSpawn(caller);
		});
	HookEventWithCaller<ServerWrapper>(
		endPreGameTickCheck,
		[this](const ServerWrapper& caller, void* params, const std::string&) {
			onCountdownEnd(caller);
		});
	HookEventWithCaller<ServerWrapper>(
		clientEndPreGameTickCheck,
		[this](const ServerWrapper& caller, void* params, const std::string&) {
			onCountdownEnd(caller);
		});
	HookEventWithCaller<ServerWrapper>(
		overtimeGameCheck,
		[this](const ServerWrapper& caller, void* params, const std::string&) {
			onOvertimeStart(caller);
		});
	HookEventWithCaller<ServerWrapper>(
		clientOvertimeGameCheck,
		[this](const ServerWrapper& caller, void* params, const std::string&) {
			onOvertimeStart(caller);
		});
	HookEventWithCaller<ServerWrapper>(
		playerLeaveOrJoinCheck,
		[this](const ServerWrapper& caller, void* params, const std::string&) {
			sendSettingsIfHost(caller);
		});
	HookEventWithCaller<ServerWrapper>(
		playerJoinedTeamCheck,
		[this](const ServerWrapper& caller, void* params, const std::string&) {
			sendSettingsIfHost(caller);
		});
	HookEventWithCaller<ServerWrapper>(
		playerLeftTeamCheck,
		[this](const ServerWrapper& caller, void* params, const std::string&) {
			sendSettingsIfHost(caller);
		});
	HookEventWithCaller<ServerWrapper>(
		menuPushCheck,
		[this](const ServerWrapper& caller, void* params, const std::string&) {
			menuPushed(caller);
		});
	HookEventWithCaller<ServerWrapper>(
		menuPopCheck,
		[this](const ServerWrapper& caller, void* params, const std::string&) {
			menuPopped(caller);
		});

	self = this;

	// Register callback to receiving TCP data from server/clients
	Networking::RegisterCallback(MessageReceived);
}

SM64::~SM64()
{

	gameWrapper->UnregisterDrawables();
	gameWrapper->UnhookEventPost("Function TAGame.EngineShare_TA.EventPostPhysicsStep");
	gameWrapper->UnhookEventPost("Function TAGame.NetworkInputBuffer_TA.ClientAckFrame");
	gameWrapper->UnhookEventPost("Function GameEvent_Soccar_TA.PostGoalScored.Tick");

	remoteMariosSema.release();
	matchSettingsSema.release();
	marioModelPoolSema.release();
	DestroySM64();
	for (int i = 0; i < remoteMarios.size(); i++)
	{
		remoteMarios[i]->sema.release();
		delete remoteMarios[i];
	}
}

void SM64::OnGameLeft(bool deleteMario)
{
	// Cleanup after a game session has been left
	matchSettingsSema.acquire();
	matchSettings.isInSm64Game = false;
	matchSettingsSema.release();
	if (localMario.model != nullptr)
	{
		localMario.model->RenderUpdateVertices(0, nullptr);
	}

	if (localMario.marioId >= 0)
	{
		if (deleteMario)
		{
			sm64_mario_delete(localMario.marioId);
			localMario.marioId = -2;
		}
		else
		{
			localMario.marioBodyState.marioState.position[0] = 0.0f;
			localMario.marioBodyState.marioState.position[1] = 0.0f;
			localMario.marioBodyState.marioState.position[2] = 0.0f;
		}

		if (localMario.model != nullptr)
		{
			localMario.model->RenderUpdateVertices(0, nullptr);
			if (deleteMario)
			{
				addModelToPool(localMario.model);
				localMario.model = nullptr;
			}
		}
		if (isHost && deleteMario && localMario.colorIndex >= 0)
		{
			addColorIndexToPool(localMario.colorIndex);
			localMario.colorIndex = -1;
		}

	}

	remoteMariosSema.acquire();
	for (const auto& remoteMario : remoteMarios)
	{
		SM64MarioInstance* marioInstance = remoteMario.second;
		marioInstance->sema.acquire();
		if (marioInstance->model != nullptr)
		{
			marioInstance->model->RenderUpdateVertices(0, nullptr);
			if (deleteMario)
			{
				addModelToPool(marioInstance->model);
				marioInstance->model = nullptr;
			}

		}
		if (deleteMario && marioInstance->marioId >= 0)
		{
			sm64_mario_delete(marioInstance->marioId);
			marioInstance->marioId = -2;
		}
		if (isHost && deleteMario && marioInstance->colorIndex >= 0)
		{
			addColorIndexToPool(marioInstance->colorIndex);
			marioInstance->colorIndex = -1;
		}
		if (!deleteMario)
		{
			marioInstance->marioBodyState.marioState.position[0] = 0.0f;
			marioInstance->marioBodyState.marioState.position[1] = 0.0f;
			marioInstance->marioBodyState.marioState.position[2] = 0.0f;
		}
		marioInstance->sema.release();
	}
	if (deleteMario)
	{
		remoteMarios.clear();
		Activate(false);
	}

	remoteMariosSema.release();

	if (ballModel != nullptr)
		ballModel->Disabled = true;
	if (octaneModel != nullptr)
		octaneModel->Disabled = true;
	if (dominusModel != nullptr)
		dominusModel->Disabled = true;
	if (fennecModel != nullptr)
		fennecModel->Disabled = true;
	if (mapModel != nullptr)
		mapModel->Disabled = true;
}

void SM64::moveCarToMario(std::string eventName)
{
	auto inGame = gameWrapper->IsInGame() || gameWrapper->IsInReplay() || gameWrapper->IsInOnlineGame();
	if (!inGame || isHost)
	{
		return;
	}
	matchSettingsSema.acquire();
	bool inSm64Game = matchSettings.isInSm64Game;
	matchSettingsSema.release();
	if (!inSm64Game)
	{
		return;
	}

	auto camera = gameWrapper->GetCamera();
	if (camera.IsNull()) return;

	auto server = gameWrapper->GetGameEventAsServer();
	if (server.IsNull())
	{
		server = gameWrapper->GetCurrentGameState();
	}

	if (server.IsNull()) return;

	auto car = gameWrapper->GetLocalCar();
	if (car.IsNull()) return;

	auto marioInstance = &localMario;

	marioInstance->sema.acquire();

	if (!marioInstance->isCar && marioInstance->marioId >= 0)
	{
		auto marioState = &marioInstance->marioBodyState.marioState;
		auto marioYaw = (int)(-marioState->faceAngle * (RL_YAW_RANGE / 6)) + (RL_YAW_RANGE / 4);
		if( marioState->position[0] == 0 && marioState->position[1] == 0 && marioState->position[2] == 0 )
		{
			marioInstance->sema.release();
			return;
		}
		auto carPosition = Vector(marioState->interpolatedPosition[0], marioState->interpolatedPosition[2], marioState->interpolatedPosition[1] + CAR_OFFSET_Z);
		car.SetLocation(carPosition);
		car.SetVelocity(Vector(marioState->velocity[0], marioState->velocity[2], marioState->velocity[1]));
		auto carRot = car.GetRotation();
		carRot.Yaw = marioYaw;
		carRot.Roll = carRotation.Roll;
		carRot.Pitch = carRotation.Pitch;
		car.SetRotation(carRot);
	}

	marioInstance->sema.release();


	if (!isHost)
	{
		remoteMariosSema.acquire();
		for (CarWrapper car : server.GetCars())
		{
			PriWrapper player = car.GetPRI();
			if (player.IsNull()) return;
			auto isLocalPlayer = player.IsLocalPlayerPRI();
			if (isLocalPlayer) continue;
			auto playerId = player.GetPlayerID();

			if (remoteMarios.count(playerId) > 0)
			{
				marioInstance = remoteMarios[playerId];
				marioInstance->sema.acquire();

				if (!marioInstance->isCar && marioInstance->marioId >= 0)
				{
					auto marioState = &marioInstance->marioBodyState.marioState;
					auto marioYaw = (int)(-marioState->faceAngle * (RL_YAW_RANGE / 6)) + (RL_YAW_RANGE / 4);
					auto carPosition = Vector(marioState->interpolatedPosition[0], marioState->interpolatedPosition[2], marioState->interpolatedPosition[1] + CAR_OFFSET_Z);
					car.SetLocation(carPosition);
					car.SetVelocity(Vector(marioState->velocity[0], marioState->velocity[2], marioState->velocity[1]));
					auto carRot = car.GetRotation();
					carRot.Yaw = marioYaw;
					carRot.Roll = carRotation.Roll;
					carRot.Pitch = carRotation.Pitch;
					car.SetRotation(carRot);
				}

				marioInstance->sema.release();
			}
		}
		remoteMariosSema.release();

	}
}


void SM64::onGoalScored(std::string eventName)
{
	matchSettingsSema.acquire();
	bool inSm64Game = matchSettings.isInSm64Game;
	matchSettingsSema.release();
	if (inSm64Game)
	{
		OnGameLeft(false);
	}
}

void SM64::onMatchWinnerSet(std::string eventName)
{
	if (isHost)
	{
		SetTimeout([this](GameWrapper*) {
			matchSettingsSema.acquire();
			bool inSm64Game = matchSettings.isInSm64Game;
			matchSettingsSema.release();
			if (inSm64Game)
			{
				SupersonicMarioPluginModule::Outer()->NextGameInMatch();
			}
		}, 15.0f);
	}
}

void SM64::menuPushed(ServerWrapper server)
{
	menuStackCount++;
}

void SM64::menuPopped(ServerWrapper server)
{
	if(menuStackCount > 0)
		menuStackCount--;
}

void SM64::MarioMessageReceived(char* buf, int len)
{
	auto marioMsgLen = sizeof(struct SM64MarioBodyState) + sizeof(int);
	uint8_t* targetData = (uint8_t*)buf;
	if (len != marioMsgLen) return;

	int playerId = *((int*)buf);

	SM64MarioInstance* marioInstance = nullptr;
	self->remoteMariosSema.acquire();
	if (self->remoteMarios.count(playerId) == 0)
	{
		// Initialize mario for this player
		marioInstance = new SM64MarioInstance();
		self->remoteMarios[playerId] = marioInstance;
	}
	else
	{
		marioInstance = self->remoteMarios[playerId];
	}
	self->remoteMariosSema.release();

	marioInstance->sema.acquire();
	memcpy(&marioInstance->marioBodyState, targetData + sizeof(int), marioMsgLen - sizeof(int));
	marioInstance->sema.release();

	self->matchSettingsSema.acquire();
	self->matchSettings.isInSm64Game = true;
	self->matchSettingsSema.release();
}

void SM64::MatchSettingsMessageReceived(char* buf, int len)
{
	auto settingsMsgLen = sizeof(MatchSettings) + sizeof(int);
	if (len != settingsMsgLen) return;

	self->matchSettingsSema.acquire();
	memcpy(&self->matchSettings, buf + sizeof(int), settingsMsgLen - sizeof(int));
	self->matchSettingsSema.release();

	// Sync player colors
	self->remoteMariosSema.acquire();
	self->localMario.sema.acquire();
	int localMarioPlayerId = self->localMario.playerId;
	self->localMario.sema.release();
	for (int i = 0; i < self->matchSettings.playerCount; i++)
	{
		int playerId = self->matchSettings.playerIds[i];
		int colorIndex = self->matchSettings.playerColorIndices[i];
		bool isCar = self->matchSettings.playerIsCarFlags[i];
		SM64MarioInstance* marioInstance = nullptr;
		if (self->remoteMarios.count(playerId) > 0)
		{
			marioInstance = self->remoteMarios[playerId];
		}
		else if (playerId == localMarioPlayerId)
		{
			marioInstance = &self->localMario;
		}

		if (marioInstance != nullptr)
		{
			marioInstance->sema.acquire();
			marioInstance->colorIndex = colorIndex;
			marioInstance->isCar = isCar;
			marioInstance->sema.release();
		}
	}

	self->remoteMariosSema.release();

	if( self->matchSettings.joinGame )
	{
		OnGameLeft(true);

		static const char pswdBuf[64] = "";
		static const int MAX_WAIT_CYCLES = 4;
        static Networking::HostStatus hostOnline = Networking::HostStatus::HOST_UNKNOWN;
		static std::shared_ptr<std::string> joinIP = std::make_shared<std::string>();
		static std::shared_ptr<int> joinPort = std::make_shared<int>(DEFAULT_PORT);

		hostOnline = Networking::HostStatus::HOST_UNKNOWN;
		*joinIP = cvarManager->getCvar("mp_ip").getStringValue();
		*joinPort = cvarManager->getCvar("mp_port").getIntValue();

		int waits = 0;
		while( hostOnline != Networking::HostStatus::HOST_ONLINE && waits <= MAX_WAIT_CYCLES)
		{
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
		waits++;
		Networking::PingHost(*joinIP, static_cast<unsigned short>(*joinPort), &hostOnline, true);
		}

		Execute([this](GameWrapper*) {
			SupersonicMarioPluginModule::Outer()->JoinGame(pswdBuf);
        });

	}
}

void MessageReceived(char* buf, int len)
{
	if (len < sizeof(int)) return;
	int messageId = *((int*)buf);
	if (messageId == -1)
	{
		self->MatchSettingsMessageReceived(buf, len);
	}
	else
	{
		self->MarioMessageReceived(buf, len);
	}
}

// Assumes matchSettingsSema is already acquired. Don't acquire here!
void SM64::SendSettingsToClients()
{
	int messageId = -1;

	if (matchSettings.isInSm64Game)
	{
		matchSettings.playerCount = 0;
		localMario.sema.acquire();
		if (localMario.colorIndex >= 0)
		{
			matchSettings.playerIds[matchSettings.playerCount] = localMario.playerId;
			matchSettings.playerColorIndices[matchSettings.playerCount] = localMario.colorIndex;
			matchSettings.playerIsCarFlags[matchSettings.playerCount] = localMario.isCar;
			matchSettings.playerCount++;
		}
		localMario.sema.release();

		remoteMariosSema.acquire();
		for (auto const& [playerId, marioInstance] : remoteMarios)
		{
			marioInstance->sema.acquire();
			if (marioInstance->colorIndex >= 0)
			{
				matchSettings.playerIds[matchSettings.playerCount] = playerId;
				matchSettings.playerColorIndices[matchSettings.playerCount] = marioInstance->colorIndex;
				matchSettings.playerIsCarFlags[matchSettings.playerCount] = marioInstance->isCar;
				matchSettings.playerCount++;
			}
			marioInstance->sema.release();
		}
		remoteMariosSema.release();
	}

	memcpy(self->netcodeOutBuf, &messageId, sizeof(int));
	memcpy(self->netcodeOutBuf + sizeof(int), &matchSettings, sizeof(MatchSettings));
	Networking::SendBytes(self->netcodeOutBuf, sizeof(MatchSettings) + sizeof(int));
}

void SM64::sendSettingsIfHost(ServerWrapper server)
{
	if (isHost)
	{
		matchSettingsSema.acquire();
		SendSettingsToClients();
		matchSettingsSema.release();
	}
}

void SM64::SendJoinCommandToClients()
{
	matchSettingsSema.acquire();
	matchSettings.joinGame = true;
	SendSettingsToClients();
	matchSettings.joinGame = false;
	matchSettingsSema.release();
}

void SM64::LoadStaticSurfaces(Model* model)
{
	if (model == nullptr)
	{
		// Load default map surfaces
		sm64_static_surfaces_load(surfaces, surfaces_count);
	}
	else
	{
		std::vector<Vertex> vertices;
		for (int i = 0; i < model->modelIndicesArr.size(); i++)
		{
			auto indices = model->modelIndicesArr[i];
			auto modelVertices = model->modelVerticesArr[i];
			for (int k = 0; k < indices.size(); k++)
			{
				vertices.push_back(modelVertices[indices[k]]);
			}
		}

		if (mapModel != nullptr)
		{
			mapModel->Disabled = true;
		}

		int numSurfaces = vertices.size() / 3;
		struct SM64Surface* staticSurfaces = (SM64Surface*)malloc(sizeof(struct SM64Surface) * numSurfaces);
		mapVertices.clear();

		for (int i = 0; i < vertices.size(); i++)
		{
			Vertex v;

			v.pos.x = vertices[i].pos.x;
			v.pos.y = -vertices[i].pos.y;
			v.pos.z = -vertices[i].pos.z;

			v.normal.x = vertices[i].pos.x;
			v.normal.y = -vertices[i].pos.y;
			v.normal.z = -vertices[i].pos.z;

			mapVertices.push_back(v);
		}

		for (int i = 0; i < vertices.size() / 3; i++)
		{
			struct SM64Surface* surface = &staticSurfaces[i];
			Vertex* surfaceVertices = &vertices[i * 3];
			surface->type = SURFACE_DEFAULT;
			surface->force = 0;
			surface->terrain = TERRAIN_GRASS;

			surface->vertices[2][0] = (int16_t)surfaceVertices[0].pos.x;
			surface->vertices[2][1] = -(int16_t)surfaceVertices[0].pos.z;
			surface->vertices[2][2] = -(int16_t)surfaceVertices[0].pos.y;

			surface->vertices[1][0] = (int16_t)surfaceVertices[1].pos.x;
			surface->vertices[1][1] = -(int16_t)surfaceVertices[1].pos.z;
			surface->vertices[1][2] = -(int16_t)surfaceVertices[1].pos.y;

			surface->vertices[0][0] = (int16_t)surfaceVertices[2].pos.x;
			surface->vertices[0][1] = -(int16_t)surfaceVertices[2].pos.z;
			surface->vertices[0][2] = -(int16_t)surfaceVertices[2].pos.y;
		}
		sm64_static_surfaces_load(staticSurfaces, numSurfaces);
		mapInitialized = false;
	}
}

/// <summary>Renders the available options for the game mode.</summary>
void SM64::RenderOptions()
{
	ImGui::SliderFloat("Attack Boost Damage", &attackBoostDamage, 0.0f, 1.0f);

	ImGui::SliderFloat("Ground Pound Pinch Velocity", &groundPoundPinchVel, 0.0f, 15000.0f);
	ImGui::SliderFloat("Attack Ball Radius", &attackBallRadius, 0.0f, 600.0f);

	ImGui::SliderFloat("Kick Ball Vel Horiz", &kickBallVelHoriz, 0.0f, 10000.0f);
	ImGui::SliderFloat("Kick Ball Vel Vert", &kickBallVelVert, 0.0f, 10000.0f);

	ImGui::SliderFloat("Punch Ball Vel Horiz", &punchBallVelHoriz, 0.0f, 10000.0f);
	ImGui::SliderFloat("Punch Ball Vel Vert", &punchBallVelVert, 0.0f, 10000.0f);

	ImGui::SliderFloat("Dive Ball Vel Horiz", &diveBallVelHoriz, 0.0f, 10000.0f);
	ImGui::SliderFloat("Dive Ball Vel Vert", &diveBallVelVert, 0.0f, 10000.0f);

	ImGui::SliderFloat("Aerial Ball Vel", &flyBallVel, 0.0f, 10000.0f);

	ImGui::NewLine();

	ImGui::Text("BLJ Configuration");
	std::string bljLabel[3];
	bljLabel[0] = "BLJ Disabled";
	bljLabel[1] = "BLJ Enabled (Press)";
	bljLabel[2] = "BLJ Enabled (Hold)";
	matchSettingsSema.acquire();
	std::string currentBljLabel = bljLabel[matchSettings.bljSetup.bljState];
	if (ImGui::BeginCombo("BLJ Mode Select", currentBljLabel.c_str()))
	{
		for (int i = 0; i < 3; i++)
		{
			bool isSelected = i == matchSettings.bljSetup.bljState;
			if (ImGui::Selectable(bljLabel[i].c_str(), isSelected))
			{
				matchSettings.bljSetup.bljState = (SM64BljState)i;
				SendSettingsToClients();
			}
			if (isSelected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	if( 0 != matchSettings.bljSetup.bljState )
	{
		int velocity = matchSettings.bljSetup.bljVel;
		ImGui::SliderInt("BLJ Velocity", &velocity, 0, 10);
		matchSettings.bljSetup.bljVel = (uint8_t)velocity;
	}


	ImGui::NewLine();

	auto inGame = gameWrapper->IsInGame() || gameWrapper->IsInReplay() || gameWrapper->IsInOnlineGame();
	bool inSm64Game = matchSettings.isInSm64Game;
	bool needToSendMatchUpdate = false;
	if (inGame && inSm64Game)
	{
		auto server = gameWrapper->GetGameEventAsServer();
		if (server.IsNull())
		{
			server = gameWrapper->GetCurrentGameState();
			if (server.IsNull()) return;
		}

		remoteMariosSema.acquire();
		for (CarWrapper car : server.GetCars())
		{
			PriWrapper player = car.GetPRI();
			if (player.IsNull()) continue;
			int playerId = player.GetPlayerID();

			SM64MarioInstance* marioInstance = nullptr;
			if (localMario.playerId == playerId)
			{
				marioInstance = &localMario;
			}
			else if (remoteMarios.count(playerId) > 0)
			{
				marioInstance = remoteMarios[playerId];
			}

			if (marioInstance == nullptr) continue;

			std::string playerName = player.GetPlayerName().ToString() + " Car Mode";

			marioInstance->sema.acquire();
			bool oldIsCar = marioInstance->isCar;
			ImGui::Checkbox(playerName.c_str(), &marioInstance->isCar);

			if (oldIsCar != marioInstance->isCar)
			{
				needToSendMatchUpdate = true;
			}
			marioInstance->sema.release();
		}
		remoteMariosSema.release();

	}

	if (needToSendMatchUpdate)
	{
		SendSettingsToClients();
	}

	matchSettingsSema.release();
}

void SM64::RenderPreferences()
{
	MarioAudio* marioAudio = &MarioAudio::getInstance();
	MarioConfig* marioConfig = &MarioConfig::getInstance();

	ImGui::TextUnformatted("Preferences");

	ImGui::SliderInt("Mario Volume", &marioAudio->MasterVolume, 0, 100);
	if (ImGui::IsItemDeactivatedAfterChange())
	{
		MarioConfig::getInstance().SetVolume(marioAudio->MasterVolume);
	}
	matchSettingsSema.acquire();
	bool inSm64Game = matchSettings.isInSm64Game;
	matchSettingsSema.release();

	if (!inSm64Game)
	{
		std::string romPath = marioConfig->GetRomPath();
		if (ImGui::InputText("ROM Path", &romPath)) {
			marioConfig->SetRomPath(romPath);
			if (Sm64Initialized)
			{
				DestroySM64();
			}
			InitSM64();
		}
		if (!Sm64Initialized)
		{
			ImGui::Banner(
				"Could not load the SM64 ROM or is not a valid SM64 US version ROM.",
				IM_COL32_ERROR_BANNER);
		}
	}
	bool disabled = Update::getInstance().CheckingForUpdates();
	if (disabled)
	{
		ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
	}
	if (ImGui::Button("Check for Updates"))
	{
		Update::getInstance().CheckForUpdates();
	}
	if (disabled)
	{
		ImGui::PopItemFlag();
		ImGui::PopStyleVar();
	}
}

bool SM64::IsActive()
{
	return isActive;
}

/// <summary>Gets the game modes name.</summary>
/// <returns>The game modes name</returns>
std::string SM64::GetGameModeName()
{
	return "SM64";
}

/// <summary>Activates the game mode.</summary>
void SM64::Activate(const bool active)
{
	if (active && !isActive) {
		isHost = true;
		HookEventWithCaller<ServerWrapper>(
			gameTickCheck,
			[this](const ServerWrapper& caller, void* params, const std::string&) {
				onTick(caller);
			});


	}
	else if (!active && isActive) {
		isHost = false;

		UnhookEvent(gameTickCheck);
	}

	isActive = active;
}


constexpr XXH128_hash_t ROM_HASH = { 0x8a90daa33e09a265, 0xc2d257a56ce0d963 };
void SM64::InitSM64()
{
	size_t romSize;
	std::string romPath = MarioConfig::getInstance().GetRomPath();
	uint8_t* rom = Utils::readFileAlloc(romPath, &romSize);
	if (rom == NULL)
	{
		return;
	}

	const auto romHash = XXH3_128bits(rom, romSize);
	if (!XXH128_isEqual(romHash, ROM_HASH))
	{
		return;
	}

	MarioAudio::getInstance().CheckReinit();

	texture = (uint8_t*)malloc(SM64_TEXTURE_SIZE);

	if (!Sm64Initialized)
	{
		sm64_global_init(rom, texture, NULL, NULL);
		LoadStaticSurfaces();
	}

	cameraPos[0] = 0.0f;
	cameraPos[1] = 0.0f;
	cameraPos[2] = 0.0f;
	cameraRot = 0.0f;

	locationInit = false;

	Sm64Initialized = true;

	Update::getInstance().CheckForUpdates();
}

void SM64::DestroySM64()
{
	if (localMario.marioId >= 0)
	{
		sm64_mario_delete(localMario.marioId);
	}
	localMario.marioId = -2;
	sm64_global_terminate();
	free(texture);
	Sm64Initialized = false;
}

void SM64::onSetVehicleInput(CarWrapper car, void* params)
{
	PriWrapper player = car.GetPRI();
	if (player.IsNull()) return;
	auto isLocalPlayer = player.IsLocalPlayerPRI();

	auto inGame = gameWrapper->IsInGame() || gameWrapper->IsInReplay() || gameWrapper->IsInOnlineGame();
	matchSettingsSema.acquire();
	bool inSm64Game = matchSettings.isInSm64Game;
	matchSettingsSema.release();
	if (!inGame || !inSm64Game)
	{
		return;
	}

	auto playerId = player.GetPlayerID();

	if (isHost || isLocalPlayer)
	{
		SM64MarioInstance* marioInstance = nullptr;

		ControllerInput* contInput = (ControllerInput*)params;
		if (isLocalPlayer)
		{
			marioInstance = &localMario;
			auto boostComponent = car.GetBoostComponent();
			if (!boostComponent.IsNull())
			{
				currentBoostAount = car.GetBoostComponent().GetCurrentBoostAmount();
			}

			if (menuStackCount > 0 &&
				(contInput->Jump || contInput->Handbrake || contInput->Throttle || contInput->Steer || contInput->Pitch))
			{
				// Reset the menu stack on car input just in case our stack count gets desynced
				menuStackCount = 0;
			}
		}
		else
		{
			remoteMariosSema.acquire();
			if (remoteMarios.count(playerId) > 0)
			{
				marioInstance = remoteMarios[playerId];
			}
			remoteMariosSema.release();
		}

		if (marioInstance == nullptr) return;

		marioInstance->sema.acquire();

		if (marioInstance->isCar)
		{
			car.SetHidden2(FALSE);
			car.SetbHiddenSelf(FALSE);
		}
		else
		{
			if (marioInstance->marioId >= 0)
			{
				car.SetHidden2(TRUE);
				car.SetbHiddenSelf(TRUE);
				auto marioState = &marioInstance->marioBodyState.marioState;
				auto marioYaw = (int)(-marioState->faceAngle * (RL_YAW_RANGE / 6)) + (RL_YAW_RANGE / 4);
				if (marioState->position[0] == 0 && marioState->position[1] == 0 && marioState->position[2] == 0)
				{
					marioInstance->sema.release();
					return;
				}
				auto carPosition = Vector(marioState->interpolatedPosition[0], marioState->interpolatedPosition[2], marioState->interpolatedPosition[1] + CAR_OFFSET_Z);
				car.SetLocation(carPosition);
				car.SetVelocity(Vector(marioState->velocity[0], marioState->velocity[2], marioState->velocity[1]));
				auto carRot = car.GetRotation();
				carRot.Yaw = marioYaw;
				carRot.Roll = carRotation.Roll;
				carRot.Pitch = carRotation.Pitch;
				car.SetRotation(carRot);
				contInput->Jump = 0;
				contInput->Handbrake = 0;
				contInput->Throttle = 0;
				contInput->Steer = 0;
				contInput->Pitch = 0;
			}

			// If attacked flag is set, decrement boost and demo if out of boost
			if (marioInstance->marioBodyState.marioState.isUpdateFrame)
			{
				auto boostComponent = car.GetBoostComponent();
				if (marioInstance->marioBodyState.marioState.isAttacked && !boostComponent.IsNull())
				{
					marioInstance->marioInputs.attackInput.isAttacked = false;
					marioInstance->marioBodyState.marioState.isAttacked = false;
					float curBoostAmt = boostComponent.GetCurrentBoostAmount();
					if (curBoostAmt >= 0.01f)
					{
						curBoostAmt -= attackBoostDamage;
						curBoostAmt = curBoostAmt < 0 ? 0 : curBoostAmt;
					}
					boostComponent.SetCurrentBoostAmount(curBoostAmt);
					if (curBoostAmt < 0.01f)
					{
						car.Demolish();
					}
				}
			}


			// Check if mario attacked ball and set velocity if so
			if (isHost)
			{
				auto server = gameWrapper->GetGameEventAsServer();
				if (server.IsNull())
				{
					server = gameWrapper->GetCurrentGameState();
				}

				if (!server.IsNull())
				{
					auto ball = server.GetBall();
					if (!ball.IsNull())
					{
						Vector marioVector(marioInstance->marioBodyState.marioState.position[0],
							marioInstance->marioBodyState.marioState.position[2],
							marioInstance->marioBodyState.marioState.position[1]);
						Vector ballVector = ball.GetLocation();
						Vector ballVelocity = ball.GetVelocity();
						float distance = Utils::Distance(marioVector, ballVector);

						float dx = ballVector.X - marioVector.X;
						float dy = ballVector.Y - marioVector.Y;

						bool attackedRecently = marioInstance->tickCount - marioInstance->lastBallInteraction < (10 * interpolationFactor);

						float angleToBall = atan2f(dy, dx);
						if (attackedRecently)
						{
							// do nothing
						}
						else if (distance < GROUND_POUND_BALL_RADIUS &&
							marioInstance->marioBodyState.action == ACT_GROUND_POUND_LAND)
						{
							ballVelocity.X += groundPoundPinchVel * cosf(angleToBall);
							ballVelocity.Y += groundPoundPinchVel * sinf(angleToBall);
							ball.SetVelocity(ballVelocity);
							marioInstance->lastBallInteraction = marioInstance->tickCount;
						}
						else if (marioInstance->marioBodyState.action == ACT_JUMP_KICK &&
							distance < attackBallRadius)
						{
							ballVelocity.X += kickBallVelHoriz * cosf(angleToBall);
							ballVelocity.Y += kickBallVelHoriz * sinf(angleToBall);
							ballVelocity.Z += kickBallVelVert;
							ball.SetVelocity(ballVelocity);
							marioInstance->lastBallInteraction = marioInstance->tickCount;
						}
						else if (marioInstance->marioBodyState.action == ACT_MOVE_PUNCHING &&
							distance < attackBallRadius)
						{
							ballVelocity.X += punchBallVelHoriz * cosf(angleToBall);
							ballVelocity.Y += punchBallVelHoriz * sinf(angleToBall);
							ballVelocity.Z += punchBallVelVert;
							ball.SetVelocity(ballVelocity);
							marioInstance->lastBallInteraction = marioInstance->tickCount;
						}
						else if ((marioInstance->marioBodyState.action == ACT_DIVE || marioInstance->marioBodyState.action == ACT_DIVE_SLIDE) &&
							distance < attackBallRadius)
						{
							ballVelocity.X += diveBallVelHoriz * cosf(angleToBall);
							ballVelocity.Y += diveBallVelHoriz * sinf(angleToBall);
							ballVelocity.Z += diveBallVelVert;
							ball.SetVelocity(ballVelocity);
							marioInstance->lastBallInteraction = marioInstance->tickCount;
						}
						else if (marioInstance->marioBodyState.action == ACT_FLYING &&
							distance < attackBallRadius)
						{
							float dz = ballVector.Z - marioVector.Z;
							float dxVert = atan2f(dz, dx);
							auto zFactor = sinf(dxVert);

							ballVelocity.X += flyBallVel * cosf(angleToBall) * abs(zFactor);
							ballVelocity.Y += flyBallVel * sinf(angleToBall) * abs(zFactor);
							ballVelocity.Z += flyBallVel * zFactor;
							ball.SetVelocity(ballVelocity);
							marioInstance->lastBallInteraction = marioInstance->tickCount;
						}


						marioInstance->tickCount++;
					}
				}

			}
		}


		marioInstance->sema.release();

	}

}

void SM64::onNameplateTick(ServerWrapper caller, void* params)
{
	PrimitiveComponentWrapper nameplate = static_cast<PrimitiveComponentWrapper>(caller.memory_address);
	nameplate.SetbIgnoreOwnerHidden(true);
	auto translation = nameplate.GetTranslation();
}

void SM64::onCharacterSpawn(ServerWrapper server)
{
	HookEventWithCaller<ServerWrapper>(
		preGameTickCheck,
		[this](const ServerWrapper& caller, void* params, const std::string&) {
			onTick(caller);
		});
	matchSettingsSema.acquire();
	matchSettings.isPreGame = true;
	if (isHost && isActive)
	{
		matchSettings.isInSm64Game = true;
	}
	matchSettingsSema.release();
	sendSettingsIfHost(server);
}

void SM64::onCountdownEnd(ServerWrapper server)
{
	UnhookEvent(preGameTickCheck);
	matchSettingsSema.acquire();
	matchSettings.isPreGame = false;
	matchSettingsSema.release();
}

void SM64::onTick(ServerWrapper server)
{
	if (!isHost) return;
	matchSettingsSema.acquire();
	matchSettings.isInSm64Game = true;
	matchSettingsSema.release();

	int localMarioPlayerId = -1;
	for (CarWrapper car : server.GetCars())
	{
		PriWrapper player = car.GetPRI();
		if (player.IsNull()) continue;
		if (player.IsLocalPlayerPRI())
		{
			localMarioPlayerId = player.GetPlayerID();
			remoteMariosSema.acquire();
			tickMarioInstance(&localMario, car, this);
			remoteMariosSema.release();
		}

	}

}

void SM64::onOvertimeStart(ServerWrapper server)
{
	matchSettingsSema.acquire();
	bool inSm64Game = matchSettings.isInSm64Game;
	matchSettingsSema.release();
	if (inSm64Game)
	{
		OnGameLeft(false);
	}
}

inline void tickMarioInstance(SM64MarioInstance* marioInstance,
	CarWrapper car,
	SM64* instance)
{
	if (car.IsNull()) return;
	instance->carLocation = car.GetLocation();
	auto x = (int16_t)(instance->carLocation.X);
	auto y = (int16_t)(instance->carLocation.Y);
	auto z = (int16_t)(instance->carLocation.Z);

	marioInstance->sema.acquire();
	if (marioInstance->isCar)
	{
		marioInstance->sema.release();
		return;
	}
	if (marioInstance->marioId < 0)
	{
		// Unreal swaps coords
		instance->carRotation = car.GetRotation();
		marioInstance->marioId = sm64_mario_create(x, z, y);
		if (marioInstance->marioId < 0)
		{
			marioInstance->sema.release();
			return;
		}
	}
	else if (marioInstance->marioBodyState.marioState.position[0] == 0.0f &&
		marioInstance->marioBodyState.marioState.position[1] == 0.0f &&
		marioInstance->marioBodyState.marioState.position[2] == 0.0f)
	{
		 sm64_mario_delete(marioInstance->marioId);
		 marioInstance->marioId = sm64_mario_create(x, z, y);
		 if (marioInstance->marioId < 0)
		 {
		 	marioInstance->sema.release();
		 	return;
		 }
	}

	auto camera = instance->gameWrapper->GetCamera();
	if (!camera.IsNull())
	{
		instance->cameraLoc = camera.GetLocation();
	}

	auto playerController = car.GetPlayerController();
	instance->matchSettingsSema.acquire();
	bool isPreGame = instance->matchSettings.isPreGame;
	instance->matchSettingsSema.release();
	if (!isPreGame && !playerController.IsNull())
	{
		auto playerInputs = playerController.GetVehicleInput();
		marioInstance->marioInputs.buttonA = playerInputs.Jump;
		marioInstance->marioInputs.buttonB = playerInputs.Handbrake;
		marioInstance->marioInputs.buttonZ = playerInputs.Throttle < 0;
		marioInstance->marioInputs.stickX = playerInputs.Steer;
		marioInstance->marioInputs.stickY = playerInputs.Pitch;
	}
	else
	{
		marioInstance->marioInputs.buttonA = NULL;
		marioInstance->marioInputs.buttonB = NULL;
		marioInstance->marioInputs.buttonZ = NULL;
		marioInstance->marioInputs.stickX = NULL;
		marioInstance->marioInputs.stickY = NULL;
	}

	marioInstance->marioInputs.camLookX = marioInstance->marioState.position[0] - instance->cameraLoc.X;
	marioInstance->marioInputs.camLookZ = marioInstance->marioState.position[2] - instance->cameraLoc.Y;

	auto controllerInput = car.GetPlayerController().GetVehicleInput();
	marioInstance->marioInputs.isBoosting = controllerInput.HoldingBoost && instance->currentBoostAount >= 0.01f;

	// Determine interaction between other marios
	marioInstance->marioInputs.attackInput.isAttacked = false;
	marioInstance->marioInputs.attackInput.attackedPosX = 0;
	marioInstance->marioInputs.attackInput.attackedPosY = 0;
	marioInstance->marioInputs.attackInput.attackedPosZ = 0;
	if (marioInstance->marioId >= 0)
	{
		Vector localMarioVector(marioInstance->marioState.position[0],
			marioInstance->marioState.position[1],
			marioInstance->marioState.position[2]);
		for (auto const& [playerId, remoteMarioInstance] : instance->remoteMarios)
		{
			if (marioInstance->marioInputs.attackInput.isAttacked ||
				!sm64_get_interpolation_should_update())
				break;

			if (remoteMarioInstance->marioId < 0)
				continue;

			remoteMarioInstance->sema.acquire();

			Vector remoteMarioVector(remoteMarioInstance->marioBodyState.marioState.position[0],
				remoteMarioInstance->marioBodyState.marioState.position[1],
				remoteMarioInstance->marioBodyState.marioState.position[2]);

			auto distance = Utils::Distance(localMarioVector, remoteMarioVector);
			if (distance < 100.0f && remoteMarioInstance->marioBodyState.action & ACT_FLAG_ATTACKING)
			{
				marioInstance->marioInputs.attackInput.isAttacked = true;
				marioInstance->marioInputs.attackInput.attackedPosX = remoteMarioVector.X;
				marioInstance->marioInputs.attackInput.attackedPosY = remoteMarioVector.Y;
				marioInstance->marioInputs.attackInput.attackedPosZ = remoteMarioVector.Z;
			}
			remoteMarioInstance->sema.release();
		}
	}

	marioInstance->marioInputs.bljInput =  instance->matchSettings.bljSetup;

	if (!self->gameWrapper->IsPaused())
	{
		marioInstance->marioInputs.isInput = true;
		marioInstance->marioInputs.giveWingcap = true;
		sm64_mario_tick(marioInstance->marioId,
			&marioInstance->marioInputs,
			&marioInstance->marioState,
			&marioInstance->marioGeometry,
			&marioInstance->marioBodyState);
	}

	auto marioVector = Vector(marioInstance->marioState.position[0], marioInstance->marioState.position[2], marioInstance->marioState.position[1]);
	auto marioVel = Vector(marioInstance->marioState.velocity[0], marioInstance->marioState.velocity[2], marioInstance->marioState.velocity[1]);
	auto quat = RotatorToQuat(camera.GetRotation());
	instance->cameraLoc = camera.GetLocation();
	Vector cameraAt = RotateVectorWithQuat(Vector(1, 0, 0), quat);

	if (marioInstance->marioBodyState.marioState.isUpdateFrame)
		MarioAudio::getInstance().UpdateSounds(marioInstance->marioState.soundMask,
			marioVector,
			marioVel,
			instance->cameraLoc,
			cameraAt,
			&marioInstance->slidingHandle,
			&marioInstance->yahooHandle,
			marioInstance->marioBodyState.action);

	marioInstance->playerId = car.GetPRI().GetPlayerID();
	if (marioInstance->marioBodyState.marioState.isUpdateFrame)
	{
		memcpy(self->netcodeOutBuf, &marioInstance->playerId, sizeof(int));
		memcpy(self->netcodeOutBuf + sizeof(int), &marioInstance->marioBodyState, sizeof(struct SM64MarioBodyState));
		Networking::SendBytes(self->netcodeOutBuf, sizeof(struct SM64MarioBodyState) + sizeof(int));
	}
	marioInstance->sema.release();
}

inline void renderMario(SM64MarioInstance* marioInstance, CameraWrapper camera)
{
	if (marioInstance == nullptr) return;

	if (self->menuStackCount > 0)
	{
		marioInstance->sema.acquire();
		if (marioInstance->model != nullptr)
		{
			marioInstance->model->RenderUpdateVertices(0, &camera);
		}
		marioInstance->sema.release();
		return;
	}

	marioInstance->sema.acquire();

	if (!marioInstance->isCar && marioInstance->model != nullptr)
	{
		std::vector<Vertex>* vertices = marioInstance->model->GetVertices();
		if (vertices != nullptr)
		{
			for (auto i = 0; i < marioInstance->marioGeometry.numTrianglesUsed * 3; i++)
			{
				auto position = &marioInstance->marioGeometry.position[i * 3];
				auto color = &marioInstance->marioGeometry.color[i * 3];
				auto uv = &marioInstance->marioGeometry.uv[i * 2];
				auto normal = &marioInstance->marioGeometry.normal[i * 3];

				auto currentVertex = &(*vertices)[i];
				// Unreal engine swaps x and y coords for 3d model
				currentVertex->pos.x = position[0];
				currentVertex->pos.y = position[2];
				currentVertex->pos.z = position[1];
				currentVertex->color.x = color[0];
				currentVertex->color.y = color[1];
				currentVertex->color.z = color[2];
				currentVertex->color.w = i >= (WINGCAP_VERTEX_INDEX * 3) ? 0.0f : 1.0f;
				currentVertex->texCoord.x = uv[0];
				currentVertex->texCoord.y = uv[1];
				currentVertex->normal.x = normal[0];
				currentVertex->normal.y = normal[2];
				currentVertex->normal.z = normal[1];
			}

			if (marioInstance->colorIndex >= 0)
			{
				int trueIndex = 6 * marioInstance->colorIndex;
				marioInstance->model->SetCapColor(self->teamColors[trueIndex],
					self->teamColors[trueIndex + 1],
					self->teamColors[trueIndex + 2]);
				marioInstance->model->SetShirtColor(self->teamColors[trueIndex + 3],
					self->teamColors[trueIndex + 4],
					self->teamColors[trueIndex + 5]);
			}

			marioInstance->model->RenderUpdateVertices(marioInstance->marioGeometry.numTrianglesUsed, &camera);
		}

	}
	marioInstance->sema.release();
}

static inline void renderCarGhost(CarWrapper car, CameraWrapper camera)
{
	Model* carModel = nullptr;
	switch (car.GetLoadoutBody())
	{
	case BREAKOUT_ID:
	case DOMINUS_ID:
		carModel = self->dominusModel;
		break;
	case FENNEC_ID:
		carModel = self->fennecModel;
		break;
	case OCTANE_ID:
	default:
		carModel = self->octaneModel;
		break;
	}

	auto carRotation = car.GetRotation();
	auto carLocation = car.GetLocation();
	auto quat = RotatorToQuat(carRotation);
	carModel->SetRotationQuat(quat.X, quat.Y, quat.Z, quat.W);
	carModel->SetTranslation(carLocation.X, carLocation.Y, carLocation.Z);
	carModel->Render(&camera);
}

void SM64::OnRender(CanvasWrapper canvas)
{
	if (!modelsInitialized)
	{
		std::string assetsFolder = Utils::GetBakkesmodFolderPath() + "data\\assets\\";
		ballModel = new Model(assetsFolder + "Rocketball.fbx", true);
		octaneModel = new Model(assetsFolder + "Octane.fbx");
		dominusModel = new Model(assetsFolder + "Dominus.fbx");
		fennecModel = new Model(assetsFolder + "Fennec.fbx");
		mapModel = new Model(10000000, nullptr, nullptr, 0, 0, 0, true);

		marioModelPoolSema.acquire();
		for (int i = 0; i < MARIO_MESH_POOL_SIZE; i++)
		{
			marioModelPool.push_back(new Model(SM64_GEO_MAX_TRIANGLES,
				texture,
				nullptr,
				4 * SM64_TEXTURE_WIDTH * SM64_TEXTURE_HEIGHT,
				SM64_TEXTURE_WIDTH,
				SM64_TEXTURE_HEIGHT,
				true,
				true));
		}
		marioModelPoolSema.release();

		modelsInitialized = true;
	}

	auto inGame = gameWrapper->IsInGame() || gameWrapper->IsInReplay() || gameWrapper->IsInOnlineGame();
	matchSettingsSema.acquire();
	bool inSm64Game = matchSettings.isInSm64Game;
	matchSettingsSema.release();
	if (!inGame && inSm64Game)
	{
		OnGameLeft(true);
	}
	if (!inGame || !inSm64Game)
	{
		return;
	}

	auto camera = gameWrapper->GetCamera();
	if (camera.IsNull()) return;

	auto server = gameWrapper->GetGameEventAsServer();
	if (server.IsNull())
	{
		server = gameWrapper->GetCurrentGameState();
	}

	if (server.IsNull()) return;

	auto engine = gameWrapper->GetEngine();
	if (!engine.IsNull())
	{
		auto statGraph = engine.GetStatGraphs().GetPerfStatGraph();
		auto fps = statGraph.GetTargetFPS();
		interpolationFactor = maxV(1, fps / 30);
		if (sm64_get_interpolation_should_update())
			sm64_set_interpolation_interval(interpolationFactor);
	}

	auto localCar = gameWrapper->GetLocalCar();
	std::string localPlayerName = "";
	if (!localCar.IsNull())
	{
		auto localPlayer = localCar.GetPRI();
		if (!localPlayer.IsNull())
		{
			localPlayerName = localPlayer.GetPlayerName().ToString();
		}
	}

	remoteMariosSema.acquire();
	for (auto const& [playerId, marioInstance] : remoteMarios)
	{
		marioInstance->MarioActive = false;
	}

	localMario.MarioActive = false;

	bool needsSettingSync = false;

	// Render local mario, and mark which marios no longer exist
	for (CarWrapper car : server.GetCars())
	{
		auto player = car.GetPRI();
		if (player.IsNull()) continue;
		auto playerName = player.GetPlayerName().ToString();

		int teamIndex = -1;
		auto team = player.GetTeam();
		if (!team.IsNull())
		{
			teamIndex = team.GetTeamIndex();
		}

		auto playerId = player.GetPlayerID();
		if (remoteMarios.count(playerId) > 0)
		{
			auto remoteMario = remoteMarios[playerId];
			remoteMario->teamIndex = teamIndex;
			if (remoteMario->isCar)
			{
				remoteMario->model->RenderUpdateVertices(0, nullptr);
				renderCarGhost(car, camera);
			}
			else
			{
				remoteMario->MarioActive = true;
			}
			if (isHost && remoteMario->colorIndex < 0)
			{
				remoteMario->colorIndex = getColorIndexFromPool(teamIndex);
				needsSettingSync = true;
			}
		}

		if (playerName != localPlayerName)
		{
			continue;
		}

		localMario.teamIndex = teamIndex;

		SM64MarioInstance* marioInstance = &localMario;


		carLocation = car.GetLocation();

		if (marioInstance->model == nullptr)
		{
			marioInstance->model = getModelFromPool();
		}

		if(!isHost)
		{
			tickMarioInstance(marioInstance, car, this);
		}
		if (isHost && localMario.colorIndex < 0)
		{
			localMario.colorIndex = getColorIndexFromPool(teamIndex);
			needsSettingSync = true;
		}

		if (localMario.isCar)
		{
			localMario.model->RenderUpdateVertices(0, nullptr);
			renderCarGhost(car, camera);
		}
		else
		{
			localMario.MarioActive = true;
		}

		renderMario(marioInstance, camera);
	}

	if (!localMario.MarioActive)
	{
		if (localMario.marioId >= 0)
		{
			sm64_mario_delete(localMario.marioId);
			localMario.marioId = -2;
		}

		if (localMario.model != nullptr)
		{
			localMario.model->RenderUpdateVertices(0, nullptr);
		}

		if (isHost && localMario.colorIndex >= 0)
		{
			addColorIndexToPool(localMario.colorIndex);
			localMario.colorIndex = -1;
			needsSettingSync = true;
		}
	}

	// Loop through remote marios and render
	for (auto const& [playerId, marioInstance] : remoteMarios)
	{
		marioInstance->sema.acquire();
		if (marioInstance->isCar)
		{
			marioInstance->sema.release();
			continue;
		}

		if (marioInstance->model == nullptr)
		{
			marioInstance->model = getModelFromPool();
			if (marioInstance->model == nullptr)
			{
				marioInstance->sema.release();
				continue;
			}
		}
		if (marioInstance->marioId < 0)
		{
			marioInstance->marioId = sm64_mario_create((int16_t)marioInstance->marioState.position[0],
				(int16_t)marioInstance->marioState.position[1],
				(int16_t)marioInstance->marioState.position[2]);
		}

		marioInstance->marioInputs.isInput = false;
		marioInstance->marioInputs.giveWingcap = false;
		sm64_mario_tick(marioInstance->marioId,
			&marioInstance->marioInputs,
			&marioInstance->marioBodyState.marioState,
			&marioInstance->marioGeometry,
			&marioInstance->marioBodyState);

		auto marioVector = Vector(marioInstance->marioBodyState.marioState.position[0],
			marioInstance->marioBodyState.marioState.position[2],
			marioInstance->marioBodyState.marioState.position[1]);
		auto marioVel = Vector(marioInstance->marioBodyState.marioState.velocity[0],
			marioInstance->marioBodyState.marioState.velocity[2],
			marioInstance->marioBodyState.marioState.velocity[1]);
		auto quat = RotatorToQuat(camera.GetRotation());
		cameraLoc = camera.GetLocation();
		Vector cameraAt = RotateVectorWithQuat(Vector(1, 0, 0), quat);

		MarioAudio::getInstance().UpdateSounds(marioInstance->marioBodyState.marioState.soundMask,
			marioVector,
			marioVel,
			cameraLoc,
			cameraAt,
			&marioInstance->slidingHandle,
			&marioInstance->yahooHandle,
			marioInstance->marioBodyState.action);

		marioInstance->marioBodyState.marioState.soundMask = 0;

		if (!marioInstance->MarioActive)
		{
			if (marioInstance->marioId >= 0)
			{
				sm64_mario_delete(marioInstance->marioId);
				marioInstance->marioId = -2;
			}

			if (marioInstance->model != nullptr)
			{
				marioInstance->model->RenderUpdateVertices(0, nullptr);
				addModelToPool(marioInstance->model);
				marioInstance->model = nullptr;
			}

			if (isHost && marioInstance->colorIndex >= 0)
			{
				addColorIndexToPool(marioInstance->colorIndex);
				marioInstance->colorIndex = -1;
				needsSettingSync = true;
			}
		}

		marioInstance->sema.release();

		renderMario(marioInstance, camera);
	}
	remoteMariosSema.release();

	if (needsSettingSync)
	{
		sendSettingsIfHost(server);
	}

	if (!server.IsNull())
	{
		auto ball = server.GetBall();
		if (!ball.IsNull())
		{
			auto ballLocation = ball.GetLocation();
			auto ballRotation = ball.GetRotation();
			auto quat = RotatorToQuat(ballRotation);

			ballModel->SetRotationQuat(quat.X, quat.Y, quat.Z, quat.W);
			ballModel->SetTranslation(ballLocation.X, ballLocation.Y, ballLocation.Z);
			ballModel->Render(&camera);
		}

		if (mapModel != nullptr)
		{
			auto modelVertices = mapModel->GetVertices();
			if (modelVertices != nullptr)
			{
				if (mapInitialized)
				{
					mapModel->Render(&camera);
				}
				else
				{
					for (int i = 0; i < mapVertices.size() / 3; i++)
					{
						int index = i * 3;
						(*modelVertices)[index + 2] = mapVertices[index];
						(*modelVertices)[index + 1] = mapVertices[index + 1];
						(*modelVertices)[index] = mapVertices[index + 2];
					}
					mapModel->RenderUpdateVertices(mapVertices.size() / 3, &camera);
					mapInitialized = true;
				}

			}
		}

	}

}

SM64MarioInstance::SM64MarioInstance()
{
	marioGeometry.position = (float*)malloc(sizeof(float) * 9 * SM64_GEO_MAX_TRIANGLES);
	marioGeometry.color = (float*)malloc(sizeof(float) * 9 * SM64_GEO_MAX_TRIANGLES);
	marioGeometry.normal = (float*)malloc(sizeof(float) * 9 * SM64_GEO_MAX_TRIANGLES);
	marioGeometry.uv = (float*)malloc(sizeof(float) * 6 * SM64_GEO_MAX_TRIANGLES);
}

SM64MarioInstance::~SM64MarioInstance()
{
	free(marioGeometry.position);
	free(marioGeometry.color);
	free(marioGeometry.normal);
	free(marioGeometry.uv);
}

Model* SM64::getModelFromPool()
{
	Model* model = nullptr;
	marioModelPoolSema.acquire();
	if (marioModelPool.size() > 0)
	{
		model = marioModelPool[0];
		marioModelPool.erase(marioModelPool.begin());
	}
	marioModelPoolSema.release();
	return model;
}

void SM64::addModelToPool(Model* model)
{
	if (model != nullptr)
	{
		marioModelPoolSema.acquire();
		marioModelPool.push_back(model);
		marioModelPoolSema.release();
	}
}

int SM64::getColorIndexFromPool(int teamIndex)
{
	std::vector<int>* colorPool = &blueTeamColorIndexPool;
	if (teamIndex == 1)
	{
		colorPool = &redTeamColorIndexPool;
	}
	int colorIndex = 0;
	if (colorPool->size() > 0)
	{
		colorIndex = (*colorPool)[0];
		colorPool->erase(colorPool->begin());
	}
	return colorIndex;
}

void SM64::addColorIndexToPool(int colorIndex)
{
	std::vector<int>* colorPool = &blueTeamColorIndexPool;
	if (colorIndex < TEAM_COLOR_POOL_SIZE)
	{
		colorPool = &redTeamColorIndexPool;
	}
	colorPool->insert(colorPool->begin(), colorIndex);
}




