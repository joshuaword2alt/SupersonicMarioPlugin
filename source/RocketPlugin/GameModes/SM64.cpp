#include "SM64.h"

#include <thread>
#include <chrono>
#include <functional>

// CONSTANTS
#define RL_YAW_RANGE 64692

std::shared_ptr<GameWrapper> gw;
std::shared_ptr<CVarManagerWrapper> cm;

void SM64Plugin::onLoad()
{
	gw = gameWrapper;
	cm = cvarManager;
	
	gw->HookEvent("Function TAGame.Mutator_Freeplay_TA.Init", bind(&SM64Plugin::OnFreeplayLoad, this, std::placeholders::_1));
	gw->HookEvent("Function TAGame.GameEvent_Soccar_TA.Destroyed", bind(&SM64Plugin::OnFreeplayDestroy, this, std::placeholders::_1));
	gw->HookEvent("Function TAGame.GameEvent_TrainingEditor_TA.StartPlayTest", bind(&SM64Plugin::OnFreeplayLoad, this, std::placeholders::_1));
	gw->HookEvent("Function TAGame.GameEvent_TrainingEditor_TA.Destroyed", bind(&SM64Plugin::OnFreeplayDestroy, this, std::placeholders::_1));
	gw->HookEvent("Function TAGame.GameInfo_Replay_TA.InitGame", bind(&SM64Plugin::OnFreeplayLoad, this, std::placeholders::_1));
	gw->HookEvent("Function TAGame.Replay_TA.EventPostTimeSkip", bind(&SM64Plugin::OnFreeplayLoad, this, std::placeholders::_1));
	gw->HookEvent("Function TAGame.GameInfo_Replay_TA.Destroyed", bind(&SM64Plugin::OnFreeplayDestroy, this, std::placeholders::_1));
}

void SM64Plugin::onUnload()
{
	
}

void SM64Plugin::OnFreeplayLoad(std::string eventName)
{
	size_t romSize;
	std::string path = GetCurrentDirectory() + "\\baserom.us.z64";
	statusText = path;
	uint8_t* rom = utilsReadFileAlloc(path, &romSize);
	if (rom == NULL)
	{
		return;
	}
	
	size_t textureSize = 4 * SM64_TEXTURE_WIDTH * SM64_TEXTURE_HEIGHT;
	texture = (uint8_t*)malloc(textureSize);
	marioGeometry.position = (float*)malloc(sizeof(float) * 9 * SM64_GEO_MAX_TRIANGLES);
	marioGeometry.color = (float*)malloc(sizeof(float) * 9 * SM64_GEO_MAX_TRIANGLES);
	marioGeometry.normal = (float*)malloc(sizeof(float) * 9 * SM64_GEO_MAX_TRIANGLES);
	marioGeometry.uv = (float*)malloc(sizeof(float) * 6 * SM64_GEO_MAX_TRIANGLES);
	projectedVertices = (Renderer::MeshVertex*)malloc(sizeof(Renderer::MeshVertex) * 3 * SM64_GEO_MAX_TRIANGLES);
	ZeroMemory(projectedVertices, sizeof(Renderer::MeshVertex) * 3 * SM64_GEO_MAX_TRIANGLES);
	
	sm64_global_terminate();
	sm64_global_init(rom, texture, NULL);
	sm64_static_surfaces_load(surfaces, surfaces_count);
	
	marioId = -1;
	cameraPos[0] = 0.0f;
	cameraPos[1] = 0.0f;
	cameraPos[2] = 0.0f;
	cameraRot = 0.0f;

	locationInit = false;
	drawOutline = false;
	lineThickness = 10.0f;

	renderer = new Renderer(SM64_GEO_MAX_TRIANGLES,
		texture,
		textureSize,
		SM64_TEXTURE_WIDTH,
		SM64_TEXTURE_HEIGHT);
	
	gw->RegisterDrawable(std::bind(&SM64Plugin::RenderMario, this, std::placeholders::_1));
}

void SM64Plugin::OnFreeplayDestroy(std::string eventName)
{
	marioId = -2;
	sm64_global_terminate();
	free(texture);
	free(marioGeometry.position);
	free(marioGeometry.color);
	free(marioGeometry.normal);
	free(marioGeometry.uv);
	free(projectedVertices);
	delete renderer;
}

void SM64Plugin::DebugRenderLevel(CanvasWrapper canvas, RT::Frustum frust)
{
	canvas.SetColor((char)255, 0, 0, 1);
	for (unsigned int i = 0; i < surfaces_count; i++)
	{
		auto surface = surfaces[i];
		auto vertices = surface.vertices;
		
		auto v1 = Vector((float)vertices[0][0], (float)vertices[0][2], (float)vertices[0][1]);
		auto v2 = Vector((float)vertices[1][0], (float)vertices[1][2], (float)vertices[1][1]);
		auto v3 = Vector((float)vertices[2][0], (float)vertices[2][2], (float)vertices[2][1]);

		RT::Triangle(v1, v2, v3).DrawOutline(canvas, frust, lineThickness, false);
	}
}

float SM64Plugin::Distance(Vector v1, Vector v2)
{
	return (float)sqrt(pow(v2.X - v1.X, 2.0) + pow(v2.Y - v1.Y, 2.0) + pow(v2.Z - v1.Z, 2.0));
}

void SM64Plugin::RenderMario(CanvasWrapper canvas)
{
	int inGame = (gw->IsInGame()) ? 1 : (gw->IsInReplay()) ? 2 : 0;
	if (!inGame || (gw->IsInOnlineGame() && inGame != 2))
	{
		return;
	}
	
	auto game = (inGame == 1) ? gw->GetGameEventAsServer() : gw->GetGameEventAsReplay();
	if (game.IsNull())
	{
		return;
	}
	
	auto cars = game.GetCars();
	auto ball = game.GetBall();
	auto camera = gw->GetCamera();
	if (camera.IsNull()) return;

	RT::Frustum frust{ canvas, camera };

	auto playerController = gw->GetPlayerController();
	if (!playerController.IsNull())
	{
		playerInputs = playerController.GetVehicleInput();
	}
	
	int carIndex = 0;
	for (auto car : cars)
	{
		if (car.IsNull())
		{
			continue;
		}
		car.SetHidden2(TRUE);
		car.SetbHiddenSelf(TRUE);
	
		if (marioId < 0)
		{
			v = car.GetLocation();
			auto x = (int16_t)(v.X);
			auto y = (int16_t)(v.Y);
			auto z = (int16_t)(v.Z);

			carRotation = car.GetRotation();

			// Unreal swaps coords
			marioId = sm64_mario_create(x, z, y);
		}

		if (marioId >= 0)
		{
			auto marioPos = marioState.position;
			auto marioVel = marioState.velocity;
			auto marioYaw = (int)(-marioState.faceAngle * (RL_YAW_RANGE / 6)) + (RL_YAW_RANGE / 4);
			// again, unreal swaps coords
			car.SetLocation(Vector(marioPos[0], marioPos[2], marioPos[1] + carOffsetZ));
			car.SetVelocity(Vector(marioVel[0], marioVel[2], marioVel[1] + carOffsetZ));

			auto carRot = car.GetRotation();
			carRot.Yaw = marioYaw;
			carRot.Roll = carRotation.Roll;
			carRot.Pitch = carRotation.Pitch;
			car.SetRotation(carRot);

			marioInputs.buttonA = playerInputs.Jump;
			marioInputs.buttonB = playerInputs.Handbrake;
			marioInputs.buttonZ = playerInputs.Throttle < 0;
			marioInputs.stickX = playerInputs.Steer;
			marioInputs.stickY = playerInputs.Pitch;
			marioInputs.camLookX = marioState.position[0] - cameraLoc.X;
			marioInputs.camLookZ = marioState.position[2] - cameraLoc.Y;

			sm64_mario_tick(marioId, &marioInputs, &marioState, &marioGeometry, 0);
		}

		cameraLoc = camera.GetLocation();

		int triangleCount = 0;
		for (auto i = 0; i < marioGeometry.numTrianglesUsed; i++)
		{
			auto position = &marioGeometry.position[i * 9];
			auto color = &marioGeometry.color[i * 9];
			auto uv = &marioGeometry.uv[i * 6];

			// Unreal engine swaps x and y coords for 3d model
			auto tv1 = Vector(position[0], position[2], position[1] + marioOffsetZ);
			auto tv2 = Vector(position[3], position[5], position[4] + marioOffsetZ);
			auto tv3 = Vector(position[6], position[8], position[7] + marioOffsetZ);

			if (frust.IsInFrustum(tv1) || frust.IsInFrustum(tv2) || frust.IsInFrustum(tv3))
			{
				auto projV1 = canvas.ProjectF(tv1);
				auto projV2 = canvas.ProjectF(tv2);
				auto projV3 = canvas.ProjectF(tv3);

				auto distance1 = Distance(cameraLoc, tv1) / depthFactor;
				auto distance2 = Distance(cameraLoc, tv2) / depthFactor;
				auto distance3 = Distance(cameraLoc, tv3) / depthFactor;

				auto baseVertexIndex = triangleCount * 3;
				projectedVertices[baseVertexIndex].position = Vector(projV1.X, projV1.Y, distance1);
				projectedVertices[baseVertexIndex].r = color[0];
				projectedVertices[baseVertexIndex].g = color[1];
				projectedVertices[baseVertexIndex].b = color[2];
				projectedVertices[baseVertexIndex].a = 1.0f;
				projectedVertices[baseVertexIndex].u = uv[0];
				projectedVertices[baseVertexIndex].v = uv[1];

				projectedVertices[baseVertexIndex + 1].position = Vector(projV2.X, projV2.Y, distance2);
				projectedVertices[baseVertexIndex + 1].r = color[3];
				projectedVertices[baseVertexIndex + 1].g = color[4];
				projectedVertices[baseVertexIndex + 1].b = color[5];
				projectedVertices[baseVertexIndex + 1].a = 1.0f;
				projectedVertices[baseVertexIndex + 1].u = uv[2];
				projectedVertices[baseVertexIndex + 1].v = uv[3];

				projectedVertices[baseVertexIndex + 2].position = Vector(projV3.X, projV3.Y, distance3);
				projectedVertices[baseVertexIndex + 2].r = color[6];
				projectedVertices[baseVertexIndex + 2].g = color[7];
				projectedVertices[baseVertexIndex + 2].b = color[8];
				projectedVertices[baseVertexIndex + 2].a = 1.0f;
				projectedVertices[baseVertexIndex + 2].u = uv[4];
				projectedVertices[baseVertexIndex + 2].v = uv[5];

				triangleCount++;
			}



			//auto triangle = RT::Triangle(tv1, tv2, tv3);
			//triangle.Draw(canvas);
		}

		renderer->RenderMesh(
			projectedVertices,
			triangleCount
		);
	
		carIndex++;
	}
}