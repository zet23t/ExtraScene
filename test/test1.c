#include "raylib.h"
#include "raymath.h"
#include "scene.h"
#include <math.h>
#include <rlgl.h>

#define TREE_PATCH_COUNT 8

SceneNodeId CreateTreePatch(SceneId sceneId, SceneModelId modelId, int count)
{
    SceneNodeId root = AcquireSceneNode(sceneId);
    
    for (int i = 0; i < count; i++)
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

static Vector3 Vector4Transform3(Vector4 v, Matrix m)
{
    Vector4 result = { 0 };
    result.x = v.x*m.m0 + v.y*m.m4 + v.z*m.m8 + v.w*m.m12;
    result.y = v.x*m.m1 + v.y*m.m5 + v.z*m.m9 + v.w*m.m13;
    result.z = v.x*m.m2 + v.y*m.m6 + v.z*m.m10 + v.w*m.m14;
    result.w = v.x*m.m3 + v.y*m.m7 + v.z*m.m11 + v.w*m.m15;
    return (Vector3){result.x / result.w, result.y / result.w, result.z / result.w};
}

static void CalcFrustumCorners(Camera3D camera, Vector3* corners)
{
    Matrix view = GetCameraMatrix(camera);
    Matrix proj = MatrixIdentity();
    proj = MatrixPerspective(camera.fovy * DEG2RAD, (double)GetScreenWidth() / (double)GetScreenHeight(), 1.0f, 30.0f);
    Matrix viewProj = MatrixMultiply(view, proj);
    viewProj = MatrixInvert(viewProj);
    corners[0] = Vector4Transform3((Vector4){-1, -1, -1, 1}, viewProj);
    corners[1] = Vector4Transform3((Vector4){1, -1, -1, 1}, viewProj);
    corners[2] = Vector4Transform3((Vector4){1, 1, -1, 1}, viewProj);
    corners[3] = Vector4Transform3((Vector4){-1, 1, -1, 1}, viewProj);
    corners[4] = Vector4Transform3((Vector4){-1, -1, 1, 1}, viewProj);
    corners[5] = Vector4Transform3((Vector4){1, -1, 1, 1}, viewProj);
    corners[6] = Vector4Transform3((Vector4){1, 1, 1, 1}, viewProj);
    corners[7] = Vector4Transform3((Vector4){-1, 1, 1, 1}, viewProj);
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

    SceneNodeId treePatchNodeId[TREE_PATCH_COUNT]; 
    for (int i = 0; i < 8; i++)
    {
        treePatchNodeId[i] = CreateTreePatch(sceneId, firTreeId, 50);
        SetSceneNodePosition(treePatchNodeId[i], 0, 0, i * 8.0f);
    }

    SetTargetFPS(60);               // Set our game to run at 60 frames-per-second
    //--------------------------------------------------------------------------------------

    float time = 0.0f;
    int isPaused = 0;
    int useExternalCamera = 0;
    int drawBoundingBoxes = 0;
    // rlSetClipPlanes(1.0f,50.0f);
    // Main game loop
    Camera3D externalCamera;
    while (!WindowShouldClose())    // Detect window close button or ESC key
    {
        time += isPaused ? 0.0f : GetFrameTime();
        if (IsKeyPressed(KEY_P))
        {
            isPaused = !isPaused;
        }
        if (IsKeyPressed(KEY_B))
        {
            drawBoundingBoxes = !drawBoundingBoxes;
        }
        if (IsKeyPressed(KEY_C))
        {
            useExternalCamera = !useExternalCamera;
            
            if (useExternalCamera)
            {
                externalCamera.position = (Vector3){ 15.0f, 25.0f, 10.0f };
                externalCamera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
                externalCamera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
                externalCamera.fovy = 55.0f;
                externalCamera.projection = CAMERA_PERSPECTIVE;
            }
        }
        
        // Draw
        //----------------------------------------------------------------------------------
        BeginDrawing();

        ClearBackground(useExternalCamera ? GRAY : RAYWHITE);
        Camera3D camera = { 0 };
        camera.position = (Vector3){ 3.0f, 10.0f, 10.0f };
        camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
        camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
        camera.fovy = 35.0f;
        camera.projection = CAMERA_PERSPECTIVE;

        if (!useExternalCamera)
        {
            externalCamera = camera;
        }
        else if (IsMouseButtonDown(MOUSE_LEFT_BUTTON))
        {
            UpdateCamera(&externalCamera, CAMERA_THIRD_PERSON);
        }
        float pitch = sinf(time * 0.55f) * 10.0f;
        float yaw = cosf(time * 0.7f) * 20.0f;
        float roll = sinf(time * 0.7f) * 20.0f;
        float height = cosf(time * 0.55f) * 0.5f + 2.5f;
        float x = sinf(time * 0.7f) * 1.0f;
        SetSceneNodePosition(biplaneNodeId, x, height, -height + 2.5f + pitch * 0.1f);
        SetSceneNodeRotation(biplaneNodeId, pitch, yaw, roll);
        SetSceneNodeRotation(biplanePropellerNodeId, 0, 0, time * 5000.0f);

        float zOffset = -time * 2.5f - 16.0f;
        float z = 0;
        for (int i = 0; i < TREE_PATCH_COUNT; i++)
        {
            z = i * 8.0f + zOffset;
            z = fmodf(z, TREE_PATCH_COUNT * 8.0f) + 16.0f;
            SetSceneNodePosition(treePatchNodeId[i], 0, 0, z);
        }

        BeginMode3D(externalCamera);

        SceneDrawStats drawStats = DrawScene(sceneId, (SceneDrawConfig) { .camera = camera, 
            .transform = MatrixIdentity(), .layerMask = 0, 
            .sortMode = SCENE_DRAW_SORT_NONE, .drawBoundingBoxes = drawBoundingBoxes });

        if (useExternalCamera)
        {
            Vector3 corners[8];
            CalcFrustumCorners(camera, corners);
            
            DrawLine3D(corners[0], corners[1], BLUE);
            DrawLine3D(corners[1], corners[2], BLUE);
            DrawLine3D(corners[2], corners[3], BLUE);
            DrawLine3D(corners[3], corners[0], BLUE);
            DrawLine3D(corners[4], corners[5], BLUE);
            DrawLine3D(corners[5], corners[6], BLUE);
            DrawLine3D(corners[6], corners[7], BLUE);
            DrawLine3D(corners[7], corners[4], BLUE);
            DrawLine3D(corners[0], corners[4], BLUE);
            DrawLine3D(corners[1], corners[5], BLUE);
            DrawLine3D(corners[2], corners[6], BLUE);
            DrawLine3D(corners[3], corners[7], BLUE);
        }
        EndMode3D();

        const char *info = TextFormat("Scenegraph test\n"
            "FPS: %d\n"
            "C: switch camera\n"
            "B: toggle bounding boxes\n"
            "P: pause\n"
            "Draw stats:\n"
            "  Culled meshes: %d\n"
            "  Meshes drawn: %d\n"
            "  Triangles drawn: %d\n",
            GetFPS(), drawStats.culledMeshCount, drawStats.meshDrawCount, drawStats.trianglesDrawCount);
        DrawText(info, 10, 10, 20, BLACK);

        EndDrawing();
        //----------------------------------------------------------------------------------
    }

    // De-Initialization
    //--------------------------------------------------------------------------------------
    CloseWindow();        // Close window and OpenGL context
    //--------------------------------------------------------------------------------------

    return 0;
}