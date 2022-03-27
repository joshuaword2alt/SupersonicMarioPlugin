#pragma once

#pragma comment(lib, "sm64.lib")
#pragma comment(lib,"shlwapi.lib")

#include <fstream>
#include <semaphore>
#include <iostream>
#include <sstream>
#include <tchar.h>
#include <shlwapi.h>
#include <stdlib.h>
#include <math.h>

#include "shlobj.h"

#include "../Graphics/Renderer.h"
#include "../Graphics/Mesh.h"
#include "../Graphics/GraphicsTypes.h"
#include "../Modules/Utils.h"
#include "../Modules/MarioAudio.h"
#include "imgui/imgui.h"
#include "imgui/imgui_additions.h"
#include "GameModes/RocketGameMode.h"
#include "../../External/BakkesModSDK/include/bakkesmod/wrappers/PluginManagerWrapper.h"
#include "Networking/Networking.h"

extern "C" {
    #include "libsm64.h"
}

#include "../Graphics/level.h"

#define SM64_NETCODE_BUF_LEN 1024
#define MARIO_MESH_POOL_SIZE 10

class SM64MarioInstance
{
public:
    SM64MarioInstance();
    ~SM64MarioInstance();

public:
    int32_t marioId = -2;
    struct SM64MarioInputs marioInputs { 0 };
    struct SM64MarioState marioState { 0 };
    struct SM64MarioGeometryBuffers marioGeometry { 0 };
    struct SM64MarioBodyState marioBodyState { 0 };
    bool CarActive = true;
    Mesh* mesh = nullptr;
    std::counting_semaphore<1> sema{ 1 };
    int slidingHandle = -1;
    unsigned long tickCount = 0;
    unsigned long lastBallInteraction = 0;
};

class SM64 final : public RocketGameMode
{
public:
    SM64(std::shared_ptr<GameWrapper> gw,
        std::shared_ptr<CVarManagerWrapper> cm,
        BakkesMod::Plugin::PluginInfo exports);
    ~SM64();

    void RenderOptions() override;
    bool IsActive() override;
    void Activate(bool active) override;
    std::string GetGameModeName() override;

    void InitSM64();
    void DestroySM64();
    void OnRender(CanvasWrapper canvas);

    void OnGameLeft();

    std::string bytesToHex(unsigned char* data, unsigned int len);

private:
    void onCharacterSpawn(ServerWrapper server);
    void onCountdownEnd(ServerWrapper server);
    void onOvertimeStart(ServerWrapper server);
    void onTick(ServerWrapper server);
    void onSetVehicleInput(CarWrapper car, void* params);
    void moveCarToMario(std::string eventName);
    void onGoalScored(std::string eventName);
    std::vector<char> hexToBytes(const std::string& hex);
    uint8_t* utilsReadFileAlloc(std::string path, size_t* fileLength);
    Mesh* getMeshFromPool();
    void addMeshToPool(Mesh*);

public:
    MarioAudio* marioAudio = nullptr;
    std::shared_ptr<GameWrapper> gameWrapper;
    Vector cameraLoc = Vector(0, 0, 0);
    ControllerInput playerInputs;
    bool inputManagerInitialized = false;
    Renderer* renderer = nullptr;
    std::vector<Vertex> ballVertices;
    bool backgroundLoadThreadStarted = false;
    bool backgroundLoadThreadFinished = false;
    Utils utils;
    Rotator carRotation;
    char netcodeOutBuf[SM64_NETCODE_BUF_LEN];
    std::vector<Mesh*> marioMeshPool;
    std::counting_semaphore<1> marioMeshPoolSema{ 1 };
    float currentBoostAount = 0.33f;

public:
    /* SM64 Members */
    uint8_t* texture;
    std::map<int, SM64MarioInstance*> remoteMarios;
    std::counting_semaphore<1> remoteMariosSema{ 1 };
    SM64MarioInstance localMario;
    vec3 cameraPos;
    float cameraRot;
    Vector carLocation;
    bool locationInit;
    Mesh* marioMesh = nullptr;
    Mesh* ballMesh = nullptr;
    bool sm64Initialized = false;
    bool meshesInitialized = false;
    struct SM64MarioBodyState marioBodyStateIn;
    std::shared_ptr<CVarManagerWrapper> cvarManager;
    std::counting_semaphore<1> isInSm64GameSema{ 1 };
    bool isPreGame = false;
    bool isInSm64Game = false;
    bool isHost = false;
    float groundPoundPinchVel;
    float attackBallRadius;
    float kickBallVelHoriz;
    float kickBallVelVert;
    float punchBallVelHoriz;
    float punchBallVelVert;
    float attackBoostDamage;
    float diveBallVelHoriz;
    float diveBallVelVert;
    struct SM64MarioBljInput bljSetup;
    float testCapColorR = 1.0f;
    float testCapColorG = 0.0f;
    float testCapColorB = 0.0f;
    float testShirtColorR = 0.0f;
    float testShirtColorG = 0.0f;
    float testShirtColorB = 1.0f;

protected:
    const std::string vehicleInputCheck = "Function TAGame.Car_TA.SetVehicleInput";
    const std::string initialCharacterSpawnCheck = "Function GameEvent_Soccar_TA.WaitingForPlayers.BeginState";
    const std::string CharacterSpawnCheck = "Function GameEvent_TA.Countdown.BeginState"; //.OnPlayerRestarted might also work
    const std::string preGameTickCheck = "Function TAGame.GameEvent_Soccar_TA.ShowSeasonIntroScene";
    const std::string endPreGameTickCheck = "Function GameEvent_TA.Countdown.EndState";
    const std::string clientEndPreGameTickCheck = "Function GameEvent_Soccar_TA.Countdown.EndState";
    const std::string gameTickCheck = "Function GameEvent_Soccar_TA.Active.Tick";
    const std::string overtimeGameCheck = "Function TAGame.GameEvent_Soccar_TA.StartOvertime";
};