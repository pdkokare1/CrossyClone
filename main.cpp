#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <string>
#include <map>
#include <cmath>
#include <cstdlib>

// Configuration constants matching original engine parameters
#define COLS 20000
#define START_COL 10000
#define FIXED_PHYSICS_STEP (1.0f / 60.0f)

enum GameState { STATE_START, STATE_PLAYING, STATE_GAMEOVER };
enum LaneType { LANE_GRASS, LANE_ROAD, LANE_RIVER, LANE_RAILWAY };
enum BiomeType { BIOME_FOREST, BIOME_DESERT, BIOME_CYBER };

struct Obstacle {
    int gridX;
    int type; // 0: Tree/Cactus/Tower, 1: Rock, 2: Shrub/Flower
    Vector3 offset;
};

struct MovingEntity {
    Vector3 position;
    float speed;
    bool active;
};

struct Lane {
    int zIndex;
    LaneType type;
    Color baseColor;
    float speed;
    float spawnTimer;
    float spawnInterval;
    std::vector<Obstacle> obstacles;
    std::vector<MovingEntity> entities;
    bool activeWarning;
    float warningTimer;
    float trainCountdown;
};

struct FloatingText {
    std::string text;
    Vector2 pos;
    Color color;
    float life;
};

struct Particle {
    Vector3 pos;
    Vector3 velocity;
    Color color;
    float scale;
    float life;
    float decay;
};

// Application Scope Variables
static GameState currentGameState = STATE_START;
static int playerGridX = START_COL;
static int playerGridZ = 0;
static Vector3 playerPos = { 0.0f, 0.0f, 0.0f };
static Vector3 targetPlayerPos = { 0.0f, 0.0f, 0.0f };
static Vector3 startPlayerPos = { 0.0f, 0.0f, 0.0f };
static bool isJumping = false;
static float jumpProgress = 0.0f;
static Vector3 playerScale = { 1.0f, 1.0f, 1.0f };

static int maxRowReached = 0;
static int globalHighScore = 0;
static int cumulativeSteps = 0;
static int currentComboMultiplier = 1;
static float lastForwardHopTime = 0.0f;
static int forwardComboCount = 0;

static std::map<int, Lane> activeLanes;
static std::vector<Particle> activeParticles;
static std::vector<FloatingText> indicators;
static float cameraShake = 0.0f;
static float gameTimeTotal = 0.0f;

// Native Audio Synth Engine Parameters
static AudioStream ambientStream;
static float audioPhase = 0.0f;
static float audioFreq1 = 110.0f;
static float audioFreq2 = 165.0f;
static bool audioEnabled = true;

// Helper to determine the current biome based on lane position
BiomeType GetBiome(int z) {
    if (z < 50) return BIOME_FOREST;
    if (z < 100) return BIOME_DESERT;
    return BIOME_CYBER;
}

// Generates procedural sound frequencies natively
void GameAudioCallback(void *buffer, unsigned int frames) {
    short *d = (short *)buffer;
    float incr1 = audioFreq1 / 44100.0f;
    float incr2 = audioFreq2 / 44100.0f;
    
    for (unsigned int i = 0; i < frames; i++) {
        if (!audioEnabled) {
            d[i] = 0;
            continue;
        }
        audioPhase += 1.0f;
        float sample1 = std::sin(audioPhase * incr1 * 2.0f * PI) * 0.3f;
        float sample2 = std::sin(audioPhase * incr2 * 2.0f * PI) * 0.2f;
        d[i] = (short)((sample1 + sample2) * 200.0f);
    }
}

void TriggerProgrammaticSound(std::string type) {
    if (!audioEnabled) return;
    if (type == "jump") {
        audioFreq1 = 150.0f + (currentComboMultiplier * 20.0f);
        audioFreq2 = 300.0f;
    } else if (type == "score") {
        audioFreq1 = 587.3f;
        audioFreq2 = 880.0f;
    } else if (type == "crash") {
        audioFreq1 = 70.0f;
        audioFreq2 = 30.0f;
    }
}

void UpdateAmbientFilters() {
    BiomeType currentBiome = GetBiome(playerGridZ);
    if (currentBiome == BIOME_CYBER) {
        audioFreq1 = 90.0f;
        audioFreq2 = 180.0f;
    } else if (currentBiome == BIOME_DESERT) {
        audioFreq1 = 100.0f;
        audioFreq2 = 150.0f;
    } else {
        audioFreq1 = 110.0f;
        audioFreq2 = 165.0f;
    }
}

void SpawnCubeParticles(Vector3 pos, Color c, int count) {
    for (int i = 0; i < count; i++) {
        Particle p;
        p.pos = pos;
        p.velocity = { 
            ((float)rand() / (float)RAND_MAX - 0.5f) * 4.0f, 
            ((float)rand() / (float)RAND_MAX) * 5.0f + 2.0f, 
            ((float)rand() / (float)RAND_MAX - 0.5f) * 4.0f 
        };
        p.color = c;
        p.scale = 0.06f + ((float)rand() / (float)RAND_MAX) * 0.1f;
        p.life = 1.0f;
        p.decay = 1.5f + ((float)rand() / (float)RAND_MAX) * 1.0f;
        activeParticles.push_back(p);
    }
}

void GenerateLaneStructure(int z) {
    if (activeLanes.find(z) != activeLanes.end()) return;

    Lane lane;
    lane.zIndex = z;
    BiomeType biome = GetBiome(z);
    
    float speedScalar = 1.0f + std::min(maxRowReached * 0.005f, 0.45f);
    float spaceScalar = 1.0f + std::min(maxRowReached * 0.007f, 0.50f);

    if (z <= 2) {
        lane.type = LANE_GRASS;
        lane.baseColor = (biome == BIOME_DESERT) ? DARKBROWN : ((biome == BIOME_CYBER) ? BLACK : DARKGREEN);
        activeLanes[z] = lane;
        return;
    }

    float randVal = (float)rand() / (float)RAND_MAX;
    if (randVal < 0.42f) {
        lane.type = LANE_ROAD;
        lane.baseColor = (biome == BIOME_CYBER) ? Color{ 30, 32, 38, 255 } : GRAY;
        lane.speed = (1.5f + ((float)rand() / (float)RAND_MAX) * 1.5f) * (((float)rand() / (float)RAND_MAX > 0.5f) ? 1.0f : -1.0f) * speedScalar;
        lane.spawnTimer = 0.0f;
        lane.spawnInterval = (2.0f + ((float)rand() / (float)RAND_MAX) * 1.5f) / spaceScalar;
    } else if (randVal < 0.68f) {
        lane.type = LANE_RIVER;
        lane.baseColor = (biome == BIOME_CYBER) ? DARKBLUE : BLUE;
        lane.speed = (1.0f + ((float)rand() / (float)RAND_MAX) * 1.0f) * (((float)rand() / (float)RAND_MAX > 0.5f) ? 1.0f : -1.0f) * speedScalar;
        lane.spawnTimer = 0.0f;
        lane.spawnInterval = (2.5f + ((float)rand() / (float)RAND_MAX) * 1.5f) / spaceScalar;
    } else if (randVal < 0.82f) {
        lane.type = LANE_RAILWAY;
        lane.baseColor = (biome == BIOME_CYBER) ? BLACK : LIME;
        lane.speed = (((float)rand() / (float)RAND_MAX) > 0.5f ? 8.0f : -8.0f) * speedScalar;
        lane.activeWarning = false;
        lane.warningTimer = 0.0f;
        lane.trainCountdown = (4.0f + ((float)rand() / (float)RAND_MAX) * 3.0f) / speedScalar;
    } else {
        lane.type = LANE_GRASS;
        lane.baseColor = (biome == BIOME_DESERT) ? ORANGE : ((biome == BIOME_CYBER) ? MAGENTA : GREEN);
        
        for (int x = START_COL - 10; x <= START_COL + 10; x++) {
            if (x == START_COL && z <= 4) continue;
            if (((float)rand() / (float)RAND_MAX) < 0.20f) {
                Obstacle obs;
                obs.gridX = x;
                obs.type = rand() % 3;
                obs.offset = { ((float)rand() / (float)RAND_MAX - 0.5f) * 0.2f, 0.0f, ((float)rand() / (float)RAND_MAX - 0.5f) * 0.2f };
                lane.obstacles.push_back(obs);
            }
        }
    }

    activeLanes[z] = lane;
}

void SynchronizeViewportLanes(int centerZ) {
    int minZ = std::max(0, centerZ - 8);
    int maxZ = centerZ + 18;

    for (int z = minZ; z <= maxZ; z++) {
        GenerateLaneStructure(z);
    }
    
    for (auto it = activeLanes.begin(); it != activeLanes.end();) {
        if (it->first < minZ || it->first > maxZ) {
            it = activeLanes.erase(it);
        } else {
            ++it;
        }
    }
}

void ExecuteMovementQueue(int dx, int dz) {
    if (currentGameState != STATE_PLAYING) return;

    int nextX = playerGridX + dx;
    int nextZ = playerGridZ + dz;

    if (nextX < START_COL - 10 || nextX > START_COL + 10 || nextZ < 0) return;

    if (activeLanes.find(nextZ) != activeLanes.end()) {
        for (auto &obs : activeLanes[nextZ].obstacles) {
            if (obs.gridX == nextX) return;
        }
    }

    playerGridX = nextX;
    playerGridZ = nextZ;

    startPlayerPos = playerPos;
    targetPlayerPos = { (float)(playerGridX - START_COL), 0.0f, (float)playerGridZ };
    
    isJumping = true;
    jumpProgress = 0.0f;

    if (dz > 0) {
        float now = gameTimeTotal;
        float diff = now - lastForwardHopTime;
        lastForwardHopTime = now;

        if (diff < 0.38f) {
            forwardComboCount++;
            currentComboMultiplier = std::min((forwardComboCount / 2) + 1, 5);
        } else {
            forwardComboCount = 1;
            currentComboMultiplier = 1;
        }
    } else {
        forwardComboCount = 0;
        currentComboMultiplier = 1;
    }

    cumulativeSteps++;
    TriggerProgrammaticSound("jump");
}

void TriggerDeathSequence(Color particleColor) {
    currentGameState = STATE_GAMEOVER;
    cameraShake = 0.5f;
    TriggerProgrammaticSound("crash");
    SpawnCubeParticles(playerPos, particleColor, 25);
    SpawnCubeParticles(playerPos, WHITE, 10);
    
    if (maxRowReached > globalHighScore) {
        globalHighScore = maxRowReached;
    }
}

void ResetGameplaySession() {
    playerGridX = START_COL;
    playerGridZ = 0;
    playerPos = { 0.0f, 0.0f, 0.0f };
    targetPlayerPos = playerPos;
    isJumping = false;
    maxRowReached = 0;
    currentComboMultiplier = 1;
    forwardComboCount = 0;
    activeLanes.clear();
    activeParticles.clear();
    SynchronizeViewportLanes(0);
    currentGameState = STATE_PLAYING;
}

void ProcessFixedPhysicsUpdate(float dt) {
    gameTimeTotal += dt;

    if (isJumping) {
        jumpProgress += 7.0f * dt;
        if (jumpProgress >= 1.0f) {
            isJumping = false;
            playerPos = targetPlayerPos;
            playerScale = { 1.2f, 0.7f, 1.2f };
        } else {
            playerPos.x = startPlayerPos.x + (targetPlayerPos.x - startPlayerPos.x) * jumpProgress;
            playerPos.z = startPlayerPos.z + (targetPlayerPos.z - startPlayerPos.z) * jumpProgress;
            playerPos.y = std::sin(jumpProgress * PI) * 0.55f;
        }
    } else {
        playerScale.x += (1.0f - playerScale.x) * 12.0f * dt;
        playerScale.y += (1.0f - playerScale.y) * 12.0f * dt;
        playerScale.z += (1.0f - playerScale.z) * 12.0f * dt;
    }

    if (playerGridZ > maxRowReached) {
        maxRowReached = playerGridZ;
        TriggerProgrammaticSound("score");
        UpdateAmbientFilters();
    }

    for (auto &pair : activeLanes) {
        Lane &lane = pair.second;
        
        if (lane.type == LANE_ROAD) {
            lane.spawnTimer += dt;
            if (lane.spawnTimer >= lane.spawnInterval) {
                lane.spawnTimer = 0.0f;
                MovingEntity car;
                car.position = { lane.speed > 0 ? -12.0f : 12.0f, 0.0f, (float)lane.zIndex };
                car.speed = lane.speed;
                car.active = true;
                lane.entities.push_back(car);
            }
        } else if (lane.type == LANE_RIVER) {
            lane.spawnTimer += dt;
            if (lane.spawnTimer >= lane.spawnInterval) {
                lane.spawnTimer = 0.0f;
                MovingEntity log;
                log.position = { lane.speed > 0 ? -13.0f : 13.0f, -0.1f, (float)lane.zIndex };
                log.speed = lane.speed;
                log.active = true;
                lane.entities.push_back(log);
            }
        } else if (lane.type == LANE_RAILWAY) {
            if (!lane.activeWarning) {
                lane.trainCountdown -= dt;
                if (lane.trainCountdown <= 0.0f) {
                    lane.activeWarning = true;
                    lane.warningTimer = 1.2f;
                }
            } else {
                lane.warningTimer -= dt;
                if (lane.warningTimer <= 0.0f) {
                    lane.activeWarning = false;
                    lane.trainCountdown = 3.0f + ((float)rand() / (float)RAND_MAX) * 4.0f;
                    MovingEntity train;
                    train.position = { lane.speed > 0 ? -25.0f : 25.0f, 0.1f, (float)lane.zIndex };
                    train.speed = lane.speed * 2.0f;
                    train.active = true;
                    lane.entities.push_back(train);
                }
            }
        }

        bool playerOnLog = false;
        for (auto &ent : lane.entities) {
            if (!ent.active) continue;
            ent.position.x += ent.speed * dt;

            if (currentGameState == STATE_PLAYING && playerGridZ == lane.zIndex) {
                if (lane.type == LANE_ROAD && std::abs(ent.position.x - playerPos.x) < 0.85f) {
                    TriggerDeathSequence(RED);
                    return;
                }
                if (lane.type == LANE_RAILWAY && std::abs(ent.position.x - playerPos.x) < 4.0f) {
                    TriggerDeathSequence(MAROON);
                    return;
                }
                if (lane.type == LANE_RIVER && !isJumping && std::abs(ent.position.x - playerPos.x) < 1.3f) {
                    playerOnLog = true;
                    playerPos.x += ent.speed * dt;
                    playerGridX = START_COL + (int)std::round(playerPos.x);
                    targetPlayerPos.x = playerPos.x;
                }
            }
        }

        if (currentGameState == STATE_PLAYING && lane.zIndex == playerGridZ && lane.type == LANE_RIVER && !isJumping && !playerOnLog) {
            TriggerDeathSequence(BLUE);
            return;
        }
    }

    for (auto it = activeParticles.begin(); it != activeParticles.end();) {
        it->pos = Vector3Add(it->pos, Vector3Scale(it->velocity, dt));
        it->velocity.y -= 9.8f * dt;
        it->life -= it->decay * dt;
        if (it->life <= 0.0f) {
            it = activeParticles.erase(it);
        } else {
            ++it;
        }
    }

    if (cameraShake > 0.0f) cameraShake -= dt * 2.0f;
}

// THE MAIN GAME RUNNER: Contains pure window rendering frames
void ExecuteGameLoopIteration(Camera3D &camera, float &accumulator) {
    float frameTime = GetFrameTime();
    if (frameTime > 0.1f) frameTime = 0.1f;
    
    accumulator += frameTime;
    while (accumulator >= FIXED_PHYSICS_STEP) {
        ProcessFixedPhysicsUpdate(FIXED_PHYSICS_STEP);
        accumulator -= FIXED_PHYSICS_STEP;
    }

    if (GetTouchPointCount() > 0 && !isJumping && currentGameState == STATE_PLAYING) {
        Vector2 touchPos = GetTouchPosition(0);
        float screenW = (float)GetScreenWidth();
        float screenH = (float)GetScreenHeight();

        if (touchPos.y < screenH * 0.33f) ExecuteMovementQueue(0, 1);
        else if (touchPos.y > screenH * 0.66f) ExecuteMovementQueue(0, -1);
        else {
            if (touchPos.x < screenW * 0.5f) ExecuteMovementQueue(1, 0);
            else ExecuteMovementQueue(-1, 0);
        }
    }

    if (IsGestureDetected(GESTURE_TAP)) {
        if (currentGameState == STATE_START) ResetGameplaySession();
        else if (currentGameState == STATE_GAMEOVER) currentGameState = STATE_START;
    }

    if (currentGameState == STATE_PLAYING || currentGameState == STATE_GAMEOVER) {
        camera.target.x += ((playerPos.x) - camera.target.x) * 0.1f;
        camera.target.z += ((playerPos.z + 3.0f) - camera.target.z) * 0.1f;
        camera.position.x = camera.target.x + 7.0f;
        camera.position.z = camera.target.z + 7.0f;
    }

    if (cameraShake > 0.0f) {
        camera.position.x += ((float)rand() / (float)RAND_MAX - 0.5f) * cameraShake;
        camera.position.y += ((float)rand() / (float)RAND_MAX - 0.5f) * cameraShake;
    }

    BeginDrawing();
    ClearBackground(GetBiome(playerGridZ) == BIOME_CYBER ? BLACK : SKYBLUE);

    BeginMode3D(camera);
        for (auto &pair : activeLanes) {
            Lane &lane = pair.second;
            DrawCube({ 0.0f, -0.5f, (float)lane.zIndex }, 60.0f, 1.0f, 1.0f, lane.baseColor);

            if (lane.type == LANE_ROAD) {
                for (float mx = -30.0f; mx < 30.0f; mx += 4.0f) {
                    DrawCube({ mx, -0.01f, (float)lane.zIndex }, 0.5f, 0.02f, 0.1f, RAYWHITE);
                }
            }

            for (auto &obs : lane.obstacles) {
                Vector3 obsPos = { (float)(obs.gridX - START_COL) + obs.offset.x, 0.4f, (float)lane.zIndex + obs.offset.z };
                DrawCube(obsPos, 0.6f, 0.8f, 0.6f, BROWN);
                DrawCube({obsPos.x, obsPos.y + 0.4f, obsPos.z}, 0.9f, 0.5f, 0.9f, DARKGREEN);
            }

            for (auto &ent : lane.entities) {
                if (!ent.active) continue;
                if (lane.type == LANE_ROAD) {
                    DrawCube(ent.position, 1.4f, 0.6f, 0.8f, RED);
                } else if (lane.type == LANE_RIVER) {
                    DrawCube(ent.position, 2.5f, 0.2f, 0.7f, DARKGRAY);
                } else if (lane.type == LANE_RAILWAY) {
                    DrawCube(ent.position, 6.0f, 1.2f, 0.8f, YELLOW);
                }
            }
        }

        if (currentGameState == STATE_PLAYING) {
            DrawCube(playerPos, 0.6f * playerScale.x, 0.7f * playerScale.y, 0.6f * playerScale.z, WHITE);
            DrawCube({ playerPos.x, playerPos.y + (0.3f * playerScale.y), playerPos.z + (0.3f * playerScale.z) }, 0.2f, 0.15f, 0.2f, ORANGE);
        }

        for (auto &p : activeParticles) {
            DrawCube(p.pos, p.scale, p.scale, p.scale, p.color);
        }
    EndMode3D();

    if (currentGameState == STATE_START) {
        DrawText("VOXEL HOPPER", GetScreenWidth()/2 - MeasureText("VOXEL HOPPER", 60)/2, (int)(GetScreenHeight()*0.25f), 60, WHITE);
        DrawText("TAP SCREEN TO INITIALIZE RUN", GetScreenWidth()/2 - MeasureText("TAP SCREEN TO INITIALIZE RUN", 24)/2, (int)(GetScreenHeight()*0.45f), 24, LIGHTGRAY);
    } else if (currentGameState == STATE_PLAYING) {
        std::string scoreStr = std::to_string(maxRowReached);
        DrawText(scoreStr.c_str(), 40, 50, 70, WHITE);
        if (currentComboMultiplier > 1) {
            std::string comboStr = "COMBO X" + std::to_string(currentComboMultiplier);
            DrawText(comboStr.c_str(), 40, 130, 35, GOLD);
        }
    } else if (currentGameState == STATE_GAMEOVER) {
        DrawText("GRID COLLISION", GetScreenWidth()/2 - MeasureText("GRID COLLISION", 55)/2, (int)(GetScreenHeight()*0.3f), 55, RED);
        std::string finalStats = "Lanes Crossed: " + std::to_string(maxRowReached) + " | Best: " + std::to_string(globalHighScore);
        DrawText(finalStats.c_str(), GetScreenWidth()/2 - MeasureText(finalStats.c_str(), 28)/2, (int)(GetScreenHeight()*0.45f), 28, LIGHTGRAY);
        DrawText("TAP TO RESET CYCLE", GetScreenWidth()/2 - MeasureText("TAP TO RESET CYCLE", 22)/2, (int)(GetScreenHeight()*0.6f), 22, GRAY);
    }

    EndDrawing();
    if (currentGameState == STATE_PLAYING) {
        SynchronizeViewportLanes(playerGridZ);
    }
}

// ANDROID MAIN EXECUTION SUB-SYSTEM ENTRY HOOK
#if defined(PLATFORM_ANDROID)
void android_main(struct android_app *state) {
    // 1. Core device channel initializations
    InitWindow(1080, 2400, "Voxel Hopper - Native Performance");
    InitAudioDevice();
    SetTargetFPS(60);

    ambientStream = LoadAudioStream(44100, 16, 1);
    SetAudioStreamCallback(ambientStream, GameAudioCallback);
    PlayAudioStream(ambientStream);

    Camera3D camera = { 0 };
    camera.position = { 7.0f, 9.0f, 7.0f };
    camera.target = { 0.0f, 0.0f, 0.0f };
    camera.up = { 0.0f, 1.0f, 0.0f };
    camera.fovy = 14.0f;
    camera.projection = CAMERA_ORTHOGRAPHIC;

    float accumulator = 0.0f;
    SynchronizeViewportLanes(0);

    // 2. Drive the core engine loops natively inside Android's system thread context frame
    while (!WindowShouldClose()) {
        ExecuteGameLoopIteration(camera, accumulator);
    }

    // 3. Unload channels safely on close
    UnloadAudioStream(ambientStream);
    CloseAudioDevice();
    CloseWindow();
}
#else
int main(void) {
    InitWindow(1080, 2400, "Voxel Hopper - Desktop Fallback");
    InitAudioDevice();
    SetTargetFPS(60);

    ambientStream = LoadAudioStream(44100, 16, 1);
    SetAudioStreamCallback(ambientStream, GameAudioCallback);
    PlayAudioStream(ambientStream);

    Camera3D camera = { 0 };
    camera.position = { 7.0f, 9.0f, 7.0f };
    camera.target = { 0.0f, 0.0f, 0.0f };
    camera.up = { 0.0f, 1.0f, 0.0f };
    camera.fovy = 14.0f;
    camera.projection = CAMERA_ORTHOGRAPHIC;

    float accumulator = 0.0f;
    SynchronizeViewportLanes(0);

    while (!WindowShouldClose()) {
        ExecuteGameLoopIteration(camera, accumulator);
    }

    UnloadAudioStream(ambientStream);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
#endif
