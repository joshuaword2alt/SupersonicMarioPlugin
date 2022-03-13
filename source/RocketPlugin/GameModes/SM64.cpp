#include "SM64.h"

#include <thread>
#include <chrono>
#include <functional>
#include <WinUser.h>

// CONSTANTS
#define RL_YAW_RANGE 64692
#define CAR_OFFSET_Z 45.0f
#define BALL_MODEL_SCALE 5.35f
#define SM64_TEXTURE_SIZE (4 * SM64_TEXTURE_WIDTH * SM64_TEXTURE_HEIGHT)
#define WINGCAP_VERTEX_INDEX 750
#define ATTACK_BOOST_DAMAGE 0.12f

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

	// Register callback to receiving TCP data from server/clients
	Networking::RegisterCallback(MessageReceived);

	InitSM64();
	gameWrapper->RegisterDrawable(std::bind(&SM64::OnRender, this, _1));
	gameWrapper->HookEventPost("Function TAGame.EngineShare_TA.EventPostPhysicsStep", bind(&SM64::moveCarToMario, this, _1));
	gameWrapper->HookEventPost("Function TAGame.NetworkInputBuffer_TA.ClientAckFrame", bind(&SM64::moveCarToMario, this, _1));
	gameWrapper->HookEventPost("Function GameEvent_Soccar_TA.PostGoalScored.Tick", bind(&SM64::onGoalScored, this, _1));
	//gameWrapper->HookEventPost("Function TAGame.RBActor_TA.PreAsyncTick", bind(&SM64::moveCarToMario, this, _1));
	//gameWrapper->HookEventPost("Function ProjectX.ActorComponent_X.Tick", bind(&SM64::moveCarToMario, this, _1));
	//gameWrapper->HookEventPost("Function ProjectX.DynamicValueModifier_X.Tick", bind(&SM64::moveCarToMario, this, _1));
	//gameWrapper->HookEventPost("Function TAGame.CarDistanceTracker_TA.Tick", bind(&SM64::moveCarToMario, this, _1));
	//gameWrapper->HookEventPost("Function Engine.PrimitiveComponent.SetRBPosition", bind(&SM64::moveCarToMario, this, _1));
	//gameWrapper->HookEventPost("Function TAGame.GameObserver_TA.UpdateCarsData", bind(&SM64::moveCarToMario, this, _1));
	//gameWrapper->HookEventPost("Function TAGame.GameObserver_TA.Tick", bind(&SM64::moveCarToMario, this, _1));
	//gameWrapper->HookEventPost("Function ProjectX.Camera_X.UpdateCamera", bind(&SM64::moveCarToMario, this, _1));
	//gameWrapper->HookEventPost("Function ProjectX.Camera_X.UpdateCameraState", bind(&SM64::moveCarToMario, this, _1));
	//gameWrapper->HookEventPost("Function TAGame.NetworkInputBuffer_TA.ServerReceivePacket", bind(&SM64::moveCarToMario, this, _1));

	typeIdx = std::make_unique<std::type_index>(typeid(*this));

	HookEventWithCaller<CarWrapper>(
		"Function TAGame.Car_TA.SetVehicleInput",
		[this](const CarWrapper& caller, void* params, const std::string&) {
			onSetVehicleInput(caller, params);
		});



	self = this;

}

SM64::~SM64()
{
	gameWrapper->UnregisterDrawables();
	DestroySM64();
	UnhookEvent("Function TAGame.Car_TA.SetVehicleInput");
}

void SM64::OnGameLeft()
{
	// Cleanup after a game session has been left
	isHost = false;
	isInSm64GameSema.acquire();
	isInSm64Game = false;
	isInSm64GameSema.release();
	if (localMario.mesh != nullptr)
	{
		localMario.mesh->RenderUpdateVertices(0, nullptr);
	}
		
	if (localMario.marioId >= 0)
	{
		sm64_mario_delete(localMario.marioId);
		localMario.marioId = -2;
		if (localMario.mesh != nullptr)
		{
			localMario.mesh->RenderUpdateVertices(0, nullptr);
			addMeshToPool(localMario.mesh);
			localMario.mesh = nullptr;
		}
	}
	for (const auto& remoteMario : remoteMarios)
	{
		SM64MarioInstance* marioInstance = remoteMario.second;
		marioInstance->sema.acquire();
		if (marioInstance->mesh != nullptr)
		{
			marioInstance->mesh->RenderUpdateVertices(0, nullptr);
			addMeshToPool(marioInstance->mesh);
			marioInstance->mesh = nullptr;
		}
		if (marioInstance->marioId >= 0)
		{
			sm64_mario_delete(marioInstance->marioId);
			marioInstance->marioId = -2;
		}
		marioInstance->sema.release();
	}
	remoteMarios.clear();
}

void SM64::moveCarToMario(std::string eventName)
{
	auto inGame = gameWrapper->IsInGame() || gameWrapper->IsInReplay() || gameWrapper->IsInOnlineGame();
	if (!inGame || isHost)
	{
		return;
	}
	isInSm64GameSema.acquire();
	bool inSm64Game = isInSm64Game;
	isInSm64GameSema.release();
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

	if (marioInstance->marioId >= 0)
	{
		auto marioState = &marioInstance->marioBodyState.marioState;
		auto marioYaw = (int)(-marioState->faceAngle * (RL_YAW_RANGE / 6)) + (RL_YAW_RANGE / 4);
		auto carPosition = Vector(marioState->posX, marioState->posZ, marioState->posY + CAR_OFFSET_Z);
		car.SetLocation(carPosition);
		car.SetVelocity(Vector(marioState->velX, marioState->velZ, marioState->velY));
		auto carRot = car.GetRotation();
		carRot.Yaw = marioYaw;
		carRot.Roll = carRotation.Roll;
		carRot.Pitch = carRotation.Pitch;
		car.SetRotation(carRot);
	}

	marioInstance->sema.release();
}

void SM64::onGoalScored(std::string eventName)
{
	isInSm64GameSema.acquire();
	bool inSm64Game = isInSm64Game;
	isInSm64GameSema.release();
	if (isInSm64Game)
	{
		OnGameLeft();
	}
}

void MessageReceived(char* buf, int len)
{
	auto targetLen = sizeof(struct SM64MarioBodyState) + sizeof(int);
	uint8_t* targetData = (uint8_t*)buf;
	if (len < targetLen)
	{
		return;
	}
	else if (len > targetLen)
	{
		targetData = (uint8_t*)(buf + len - targetLen);
	}

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
	memcpy(&marioInstance->marioBodyState, targetData + sizeof(int), targetLen - sizeof(int));
	marioInstance->sema.release();

	self->isInSm64GameSema.acquire();
	self->isInSm64Game = true;
	self->isInSm64GameSema.release();
}

#define MIN_LIGHT_COORD -6000.0f
#define MAX_LIGHT_COORD 6000.0f
static int currentLightIndex = 0;
/// <summary>Renders the available options for the game mode.</summary>
void SM64::RenderOptions()
{
	if (renderer != nullptr)
	{
		ImGui::Text("Ambient Light");
		ImGui::SliderFloat("R", &renderer->Lighting.AmbientLightColorR, 0.0f, 1.0f);
		ImGui::SliderFloat("G", &renderer->Lighting.AmbientLightColorG, 0.0f, 1.0f);
		ImGui::SliderFloat("B", &renderer->Lighting.AmbientLightColorB, 0.0f, 1.0f);
		ImGui::SliderFloat("Ambient Strength", &renderer->Lighting.AmbientLightStrength, 0.0f, 1.0f);

		ImGui::NewLine();

		ImGui::Text("Dynamic Light");
		std::string currentLightLabel = "Light " + std::to_string(currentLightIndex + 1);
		if (ImGui::BeginCombo("Light Select", currentLightLabel.c_str()))
		{
			for (int i = 0; i < MAX_LIGHTS; i++)
			{
				std::string lightLabel = "Light " + std::to_string(i + 1);
				bool isSelected = i == currentLightIndex;
				if (ImGui::Selectable(lightLabel.c_str(), isSelected))
					currentLightIndex = i;
				if (isSelected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		ImGui::SliderFloat("X", &renderer->Lighting.Lights[currentLightIndex].posX, MIN_LIGHT_COORD, MAX_LIGHT_COORD);
		ImGui::SliderFloat("Y", &renderer->Lighting.Lights[currentLightIndex].posY, MIN_LIGHT_COORD, MAX_LIGHT_COORD);
		ImGui::SliderFloat("Z", &renderer->Lighting.Lights[currentLightIndex].posZ, MIN_LIGHT_COORD, MAX_LIGHT_COORD);
		ImGui::SliderFloat("Rd", &renderer->Lighting.Lights[currentLightIndex].r, 0.0f, 1.0f);
		ImGui::SliderFloat("Gd", &renderer->Lighting.Lights[currentLightIndex].g, 0.0f, 1.0f);
		ImGui::SliderFloat("Bd", &renderer->Lighting.Lights[currentLightIndex].b, 0.0f, 1.0f);
		ImGui::SliderFloat("Dynamic Strength", &renderer->Lighting.Lights[currentLightIndex].strength, 0.0f, 1.0f);
		ImGui::Checkbox("Show Bulb", &renderer->Lighting.Lights[currentLightIndex].showBulb);
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
		isHost = false;
		HookEventWithCaller<ServerWrapper>(
			"Function GameEvent_Soccar_TA.Active.Tick",
			[this](const ServerWrapper& caller, void* params, const std::string&) {
				onTick(caller);
			});
	}
	else if (!active && isActive) {
		isHost = false;
		UnhookEvent("Function GameEvent_Soccar_TA.Active.Tick");
	}

	isActive = active;
}

void SM64::InitSM64()
{
	size_t romSize;
	std::string bakkesmodFolderPath = utils.GetBakkesmodFolderPath();
	std::string romPath = bakkesmodFolderPath + "data\\assets\\baserom.us.z64";
	uint8_t* rom = utilsReadFileAlloc(romPath, &romSize);
	if (rom == NULL)
	{
		return;
	}
	
	texture = (uint8_t*)malloc(SM64_TEXTURE_SIZE);
	
	//sm64_global_terminate();
	if (!sm64Initialized)
	{
		sm64_global_init(rom, texture, NULL);
		sm64_static_surfaces_load(surfaces, surfaces_count);
		sm64Initialized = true;
	}
	
	cameraPos[0] = 0.0f;
	cameraPos[1] = 0.0f;
	cameraPos[2] = 0.0f;
	cameraRot = 0.0f;

	locationInit = false;

	renderer = new Renderer();
	marioAudio = new MarioAudio();
}

void SM64::DestroySM64()
{
	if (localMario.marioId >= 0)
	{
		sm64_mario_delete(localMario.marioId);
	}
	localMario.marioId = -2;
	//sm64_global_terminate();
	free(texture);
	delete renderer;
	delete marioAudio;
}

void SM64::onSetVehicleInput(CarWrapper car, void* params)
{
	PriWrapper player = car.GetPRI();
	if (player.IsNull()) return;
	auto isLocalPlayer = player.IsLocalPlayerPRI();

	auto inGame = gameWrapper->IsInGame() || gameWrapper->IsInReplay() || gameWrapper->IsInOnlineGame();
	isInSm64GameSema.acquire();
	bool inSm64Game = isInSm64Game;
	isInSm64GameSema.release();
	if (!inGame || !isInSm64Game)
	{
		return;
	}

	auto playerId = player.GetPlayerID();

	if (isHost || isLocalPlayer)
	{
		SM64MarioInstance* marioInstance = nullptr;

		if (isLocalPlayer)
		{
			marioInstance = &localMario;
			auto boostComponent = car.GetBoostComponent();
			if (!boostComponent.IsNull())
			{
				currentBoostAount = car.GetBoostComponent().GetCurrentBoostAmount();
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

		if (marioInstance->marioId >= 0)
		{
			car.SetHidden2(TRUE);
			car.SetbHiddenSelf(TRUE);
			auto marioState = &marioInstance->marioBodyState.marioState;
			auto marioYaw = (int)(-marioState->faceAngle * (RL_YAW_RANGE / 6)) + (RL_YAW_RANGE / 4);
			auto carPosition = Vector(marioState->posX, marioState->posZ, marioState->posY + CAR_OFFSET_Z);
			car.SetLocation(carPosition);
			car.SetVelocity(Vector(marioState->velX, marioState->velZ, marioState->velY));
			auto carRot = car.GetRotation();
			carRot.Yaw = marioYaw;
			carRot.Roll = carRotation.Roll;
			carRot.Pitch = carRotation.Pitch;
			car.SetRotation(carRot);
			ControllerInput* newInput = (ControllerInput*)params;
			newInput->Jump = 0;
			newInput->Handbrake = 0;
			newInput->Throttle = 0;
			newInput->Steer = 0;
			newInput->Pitch = 0;

			auto boostComponent = car.GetBoostComponent();
			if (marioInstance->marioBodyState.marioState.attacked && !boostComponent.IsNull())
			{
				marioInstance->marioBodyState.marioState.attacked = false;
				float curBoostAmt = boostComponent.GetCurrentBoostAmount();
				if (curBoostAmt >= 0.01f)
				{
					curBoostAmt -= ATTACK_BOOST_DAMAGE;
					curBoostAmt = curBoostAmt < 0 ? 0 : curBoostAmt;
				}
				boostComponent.SetCurrentBoostAmount(curBoostAmt);
				if (curBoostAmt < 0.01f)
				{
					car.Demolish();
				}
			}

		}

		marioInstance->sema.release();

	}

}

void SM64::onTick(ServerWrapper server)
{
	isHost = true;
	isInSm64GameSema.acquire();
	isInSm64Game = true;
	isInSm64GameSema.release();
	for (CarWrapper car : server.GetCars())
	{
		PriWrapper player = car.GetPRI();
		if (player.IsNull()) continue;
		if (player.IsLocalPlayerPRI())
		{
			remoteMariosSema.acquire();
			tickMarioInstance(&localMario, car, this);
			remoteMariosSema.release();
		}
		
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
	if (marioInstance->marioId < 0)
	{
		// Unreal swaps coords
		instance->carRotation = car.GetRotation();
		marioInstance->marioId = sm64_mario_create(x, z, y);
		if (marioInstance->marioId < 0) return;
	}

	auto camera = instance->gameWrapper->GetCamera();
	if (!camera.IsNull())
	{
		instance->cameraLoc = camera.GetLocation();
	}

	auto playerController = car.GetPlayerController();
	if (!playerController.IsNull())
	{
		auto playerInputs = playerController.GetVehicleInput();
		marioInstance->marioInputs.buttonA = playerInputs.Jump;
		marioInstance->marioInputs.buttonB = playerInputs.Handbrake;
		marioInstance->marioInputs.buttonZ = playerInputs.Throttle < 0;
		marioInstance->marioInputs.stickX = playerInputs.Steer;
		marioInstance->marioInputs.stickY = playerInputs.Pitch;
	}

	marioInstance->marioInputs.camLookX = marioInstance->marioState.posX - instance->cameraLoc.X;
	marioInstance->marioInputs.camLookZ = marioInstance->marioState.posZ - instance->cameraLoc.Y;

	auto controllerInput = car.GetPlayerController().GetVehicleInput();
	auto isboosting = controllerInput.HoldingBoost && instance->currentBoostAount >= 0.01f;

	// Determine interaction between other marios
	bool isAttacked = false;
	Vector attackedFromFector(0, 0, 0);
	if (marioInstance->marioId >= 0)
	{
		Vector localMarioVector(marioInstance->marioState.posX,
			marioInstance->marioState.posY,
			marioInstance->marioState.posZ);
		for (auto const& [playerId, remoteMarioInstance] : instance->remoteMarios)
		{
			if (isAttacked)
				break;

			if (remoteMarioInstance->marioId < 0)
				continue;

			remoteMarioInstance->sema.acquire();

			Vector remoteMarioVector(remoteMarioInstance->marioBodyState.marioState.posX,
				remoteMarioInstance->marioBodyState.marioState.posY,
				remoteMarioInstance->marioBodyState.marioState.posZ);

			auto distance = instance->utils.Distance(localMarioVector, remoteMarioVector);
			if (distance < 100.0f && remoteMarioInstance->marioBodyState.action & ACT_FLAG_ATTACKING) // TODO if in our direction
			{
				isAttacked = true;
				attackedFromFector = remoteMarioVector;
			}
			remoteMarioInstance->sema.release();
		}
	}

	sm64_mario_tick(marioInstance->marioId,
		&marioInstance->marioInputs,
		&marioInstance->marioState,
		&marioInstance->marioGeometry,
		&marioInstance->marioBodyState,
		true,
		true,
		isboosting,
		isAttacked,
		attackedFromFector.X,
		attackedFromFector.Y,
		attackedFromFector.Z);


	auto marioVector = Vector(marioInstance->marioState.posX, marioInstance->marioState.posZ, marioInstance->marioState.posY);
	auto marioVel = Vector(marioInstance->marioState.velX, marioInstance->marioState.velZ, marioInstance->marioState.velY);
	auto quat = RotatorToQuat(camera.GetRotation());
	instance->cameraLoc = camera.GetLocation();
	Vector cameraAt = RotateVectorWithQuat(Vector(1, 0, 0), quat);

	marioInstance->slidingHandle = instance->marioAudio->UpdateSounds(marioInstance->marioState.soundMask,
		marioVector,
		marioVel,
		instance->cameraLoc,
		cameraAt,
		marioInstance->slidingHandle,
		marioInstance->marioBodyState.action);

	int playerId = car.GetPRI().GetPlayerID();
	memcpy(self->netcodeOutBuf, &playerId, sizeof(int));
	memcpy(self->netcodeOutBuf + sizeof(int), &marioInstance->marioBodyState, sizeof(struct SM64MarioBodyState));
	Networking::SendBytes(self->netcodeOutBuf, sizeof(struct SM64MarioBodyState) + sizeof(int));
	marioInstance->sema.release();
}

void loadBallMesh()
{
	self->utils.ParseObjFile(self->utils.GetBakkesmodFolderPath() + "data\\assets\\ROCKETBALL.obj", &self->ballVertices);
	self->loadMeshesThreadFinished = true;
}

inline void renderMario(SM64MarioInstance* marioInstance, CameraWrapper camera)
{
	if (marioInstance == nullptr) return;

	marioInstance->sema.acquire();
	if (marioInstance->mesh != nullptr)
	{
		for (auto i = 0; i < marioInstance->marioGeometry.numTrianglesUsed * 3; i++)
		{
			auto position = &marioInstance->marioGeometry.position[i * 3];
			auto color = &marioInstance->marioGeometry.color[i * 3];
			auto uv = &marioInstance->marioGeometry.uv[i * 2];
			auto normal = &marioInstance->marioGeometry.normal[i * 3];

			auto currentVertex = &marioInstance->mesh->Vertices[i];
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
		marioInstance->mesh->RenderUpdateVertices(marioInstance->marioGeometry.numTrianglesUsed, &camera);
	}
	marioInstance->sema.release();
}

void SM64::OnRender(CanvasWrapper canvas)
{
	if (renderer == nullptr) return;
	if (!loadMeshesThreadStarted)
	{
		if (!renderer->Initialized) return;
		std::thread loadMeshesThread(loadBallMesh);
		loadMeshesThread.detach();
		loadMeshesThreadStarted = true;
		return;
	}

	if (!loadMeshesThreadFinished) return;

	if (!meshesInitialized)
	{
		ballMesh = renderer->CreateMesh(&ballVertices);

		marioMeshPoolSema.acquire();
		for (int i = 0; i < MARIO_MESH_POOL_SIZE; i++)
		{
			marioMeshPool.push_back(renderer->CreateMesh(SM64_GEO_MAX_TRIANGLES,
				texture,
				4 * SM64_TEXTURE_WIDTH * SM64_TEXTURE_HEIGHT,
				SM64_TEXTURE_WIDTH,
				SM64_TEXTURE_HEIGHT));
		}
		marioMeshPoolSema.release();

		meshesInitialized = true;
	}

	auto inGame = gameWrapper->IsInGame() || gameWrapper->IsInReplay() || gameWrapper->IsInOnlineGame();
	isInSm64GameSema.acquire();
	bool inSm64Game = isInSm64Game;
	isInSm64GameSema.release();
	if (!inGame || !isInSm64Game)
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
		marioInstance->CarActive = false;
	}

	localMario.CarActive = false;

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
			remoteMario->CarActive = true;
			if (remoteMario->mesh != nullptr)
			{
				remoteMario->mesh->SetColorIndex(teamIndex);
			}
		}

		if (playerName != localPlayerName)
		{
			continue;
		}

		localMario.CarActive = true;

		SM64MarioInstance* marioInstance = &localMario;


		carLocation = car.GetLocation();

		if (marioInstance->mesh == nullptr)
		{
			marioInstance->mesh = getMeshFromPool();
		}
		else
		{
			marioInstance->mesh->SetColorIndex(teamIndex);
		}

		if(!isHost)
		{
			tickMarioInstance(marioInstance, car, this);
		}

		renderMario(marioInstance, camera);
	}

	if (!localMario.CarActive)
	{
		if (localMario.marioId >= 0)
		{
			sm64_mario_delete(localMario.marioId);
			localMario.marioId = -2;
		}

		if (localMario.mesh != nullptr)
		{
			localMario.mesh->RenderUpdateVertices(0, nullptr);
		}
	}

	// Loop through remote marios and render
	for (auto const& [playerId, marioInstance] : remoteMarios)
	{
		marioInstance->sema.acquire();
		if (marioInstance->mesh == nullptr)
		{
			marioInstance->mesh = getMeshFromPool();
			if (marioInstance->mesh == nullptr)
			{
				marioInstance->sema.release();
				continue;
			}
		}
		if (marioInstance->marioId < 0)
		{
			marioInstance->marioId = sm64_mario_create((int16_t)marioInstance->marioState.posX,
				(int16_t)marioInstance->marioState.posY,
				(int16_t)marioInstance->marioState.posZ);
		}

		sm64_mario_tick(marioInstance->marioId,
			&marioInstance->marioInputs,
			&marioInstance->marioBodyState.marioState,
			&marioInstance->marioGeometry,
			&marioInstance->marioBodyState,
			false,
			false,
			false,
			false,
			0,
			0,
			0);

		auto marioVector = Vector(marioInstance->marioBodyState.marioState.posX,
			marioInstance->marioBodyState.marioState.posZ,
			marioInstance->marioBodyState.marioState.posY);
		auto marioVel = Vector(marioInstance->marioBodyState.marioState.velX,
			marioInstance->marioBodyState.marioState.velZ,
			marioInstance->marioBodyState.marioState.velY);
		auto quat = RotatorToQuat(camera.GetRotation());
		cameraLoc = camera.GetLocation();
		Vector cameraAt = RotateVectorWithQuat(Vector(1, 0, 0), quat);
		
		marioInstance->slidingHandle = marioAudio->UpdateSounds(marioInstance->marioBodyState.marioState.soundMask,
			marioVector,
			marioVel,
			cameraLoc,
			cameraAt,
			marioInstance->slidingHandle,
			marioInstance->marioBodyState.action);

		marioInstance->marioBodyState.marioState.soundMask = 0;

		if (!marioInstance->CarActive)
		{
			if (marioInstance->marioId >= 0)
			{
				sm64_mario_delete(marioInstance->marioId);
				marioInstance->marioId = -2;
			}
			
			if (marioInstance->mesh != nullptr)
			{
				marioInstance->mesh->RenderUpdateVertices(0, nullptr);
				addMeshToPool(marioInstance->mesh);
				marioInstance->mesh = nullptr;
			}
		}

		marioInstance->sema.release();

		renderMario(marioInstance, camera);
	}
	remoteMariosSema.release();


	if (ballMesh != nullptr)
	{
		if (!server.IsNull())
		{
			auto ball = server.GetBall();
			if (!ball.IsNull())
			{
				auto ballLocation = ball.GetLocation();
				auto ballRotation = ball.GetRotation();
				auto quat = RotatorToQuat(ballRotation);

				ballMesh->SetRotationQuat(quat.X, quat.Y, quat.Z, quat.W);
				ballMesh->SetTranslation(ballLocation.X, ballLocation.Y, ballLocation.Z);
				ballMesh->SetScale(BALL_MODEL_SCALE, BALL_MODEL_SCALE, BALL_MODEL_SCALE);
				ballMesh->Render(&camera);

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

std::string SM64::bytesToHex(unsigned char* data, unsigned int len)
{
	std::stringstream ss;
	ss << std::hex << std::setfill('0');
	for (unsigned int i = 0; i < len; ++i)
		ss << std::setw(2) << static_cast<unsigned>(data[i]);

	return ss.str();
}

Mesh* SM64::getMeshFromPool()
{
	Mesh* mesh = nullptr;
	marioMeshPoolSema.acquire();
	if (marioMeshPool.size() > 0)
	{
		mesh = marioMeshPool[0];
		marioMeshPool.erase(marioMeshPool.begin());
	}
	marioMeshPoolSema.release();
	return mesh;
}

void SM64::addMeshToPool(Mesh* mesh)
{
	if (mesh != nullptr)
	{
		marioMeshPoolSema.acquire();
		marioMeshPool.push_back(mesh);
		marioMeshPoolSema.release();
	}
}

std::vector<char> SM64::hexToBytes(const std::string& hex) {
	std::vector<char> bytes;

	for (unsigned int i = 0; i < hex.length(); i += 2) {
		std::string byteString = hex.substr(i, 2);
		char byte = (char)strtol(byteString.c_str(), NULL, 16);
		bytes.push_back(byte);
	}

	return bytes;
}

uint8_t* SM64::utilsReadFileAlloc(std::string path, size_t* fileLength)
{
	FILE* f;
	fopen_s(&f, path.c_str(), "rb");

	if (!f) return NULL;

	fseek(f, 0, SEEK_END);
	size_t length = (size_t)ftell(f);
	rewind(f);
	uint8_t* buffer = (uint8_t*)malloc(length + 1);
	if (buffer != NULL)
	{
		fread(buffer, 1, length, f);
		buffer[length] = 0;
	}

	fclose(f);

	if (fileLength) *fileLength = length;

	return (uint8_t*)buffer;
}


