#include "raylib.h"
#include "raymath.h"
#include "scene.h"
#include <math.h>

SceneNodeId CreateTreePatch(SceneId sceneId, SceneModelId modelId, int count)
{
    SceneNodeId root = AcquireSceneNode(sceneId);
    
    for (int i = 0; i < 100; i++)
    {
        SceneNodeId firTreeNodeId = AcquireSceneNode(sceneId);
        SetSceneNodeModel(firTreeNodeId, modelId);
        float rx = GetRandomValue(-400, 400) * 0.01f;
        float rz = GetRandomValue(-400, 400) * 0.01f;
        SetSceneNodePosition(firTreeNodeId, rx, 0, rz);
        float scale = GetRandomValue(90, 110) * 0.01f;
        float scaleHeight = GetRandomValue(20, -20) * 0.01f + scale;
        SetSceneNodeScale(firTreeNodeId, scale, scaleHeight, scale);
        SetSceneNodeRotation(firTreeNodeId, 0, GetRandomValue(0, 360), 0);
        SetSceneNodeParent(firTreeNodeId, root);
    }

    return root;
}

int main(void)
{
    // Initialization
    //--------------------------------------------------------------------------------------
    const int screenWidth = 800;
    const int screenHeight = 450;

    InitWindow(screenWidth, screenHeight, "Scene graph test");

    Model firTree = LoadModel("resources/firtree-1.glb");
    Model biplane = LoadModel("resources/biplane-1.glb");
    Model biplanePropeller = LoadModel("resources/biplane-1-propeller.glb");
    SetTextureFilter(firTree.materials[1].maps[MATERIAL_MAP_ALBEDO].texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(biplane.materials[1].maps[MATERIAL_MAP_ALBEDO].texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(biplanePropeller.materials[1].maps[MATERIAL_MAP_ALBEDO].texture, TEXTURE_FILTER_BILINEAR);
    
    SceneId sceneId = LoadScene();
    SceneModelId firTreeId = AddModelToScene(sceneId, firTree, "fir tree", 1);
    SceneModelId biplaneId = AddModelToScene(sceneId, biplane, "biplane", 1);
    SceneModelId biplanePropellerId = AddModelToScene(sceneId, biplanePropeller, "biplane propeller", 1);

    SceneNodeId biplaneNodeId = AcquireSceneNode(sceneId);
    SetSceneNodeModel(biplaneNodeId, biplaneId);
    SetSceneNodePosition(biplaneNodeId, 0, 2.0f, 0);
    SceneNodeId biplanePropellerNodeId = AcquireSceneNode(sceneId);
    SetSceneNodeModel(biplanePropellerNodeId, biplanePropellerId);
    SetSceneNodePosition(biplanePropellerNodeId, 0, 0, 0.0f);
    SetSceneNodeParent(biplanePropellerNodeId, biplaneNodeId);

    const int treePatchCount = 8;
    SceneNodeId treePatchNodeId[treePatchCount]; 
    for (int i = 0; i < 8; i++)
    {
        treePatchNodeId[i] = CreateTreePatch(sceneId, firTreeId, 50);
        SetSceneNodePosition(treePatchNodeId[i], 0, 0, i * 8.0f);
    }

    SetTargetFPS(60);               // Set our game to run at 60 frames-per-second
    //--------------------------------------------------------------------------------------

    // Main game loop
    while (!WindowShouldClose())    // Detect window close button or ESC key
    {
        // Update
        //----------------------------------------------------------------------------------
        // TODO: Update your variables here
        //----------------------------------------------------------------------------------

        // Draw
        //----------------------------------------------------------------------------------
        BeginDrawing();

        ClearBackground(RAYWHITE);
        Camera3D camera = { 0 };
        camera.position = (Vector3){ 3.0f, 10.0f, 10.0f };
        camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
        camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
        camera.fovy = 35.0f;
        camera.projection = CAMERA_PERSPECTIVE;
        
        float pitch = sinf(GetTime() * 0.55f) * 10.0f;
        float yaw = cosf(GetTime() * 0.7f) * 20.0f;
        float roll = sinf(GetTime() * 0.7f) * 20.0f;
        float height = cosf(GetTime() * 0.55f) * 0.5f + 2.5f;
        float x = sinf(GetTime() * 0.7f) * 1.0f;
        SetSceneNodePosition(biplaneNodeId, x, height, 0);
        SetSceneNodeRotation(biplaneNodeId, pitch, yaw, roll);
        SetSceneNodeRotation(biplanePropellerNodeId, 0, 0, GetTime() * 5000.0f);

        float zOffset = -GetTime() * 2.5f - 16.0f;
        float z = 0;
        for (int i = 0; i < treePatchCount; i++)
        {
            z = i * 8.0f + zOffset;
            z = fmodf(z, treePatchCount * 8.0f) + 16.0f;
            SetSceneNodePosition(treePatchNodeId[i], 0, 0, z);
        }

        BeginMode3D(camera);
        DrawScene(sceneId, camera, MatrixIdentity(), 0, SCENE_DRAW_SORT_NONE);
        EndMode3D();

        DrawText("Scenegraph test", 10, 10, 20, BLACK);

        EndDrawing();
        //----------------------------------------------------------------------------------
    }

    // De-Initialization
    //--------------------------------------------------------------------------------------
    CloseWindow();        // Close window and OpenGL context
    //--------------------------------------------------------------------------------------

    return 0;
}