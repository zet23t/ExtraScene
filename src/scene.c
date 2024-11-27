#include <raylib.h>
#include <raymath.h>
#include <scene.h>
#include <string.h>
#include <rlgl.h>

static void *ListAlloc(void **list, unsigned long *count, unsigned long *capacity, unsigned long size)
{
    if (*count >= *capacity)
    {
        *capacity = (*capacity == 0) ? 8 : *capacity * 2;
        *list = *list ? MemRealloc(*list, *capacity * size) : MemAlloc(*capacity * size);
    }
    char *byteList = (char *)*list;
    char *ptr = &byteList[*count * size];
    *count += 1;
    for (unsigned long i = 0; i < size; i++)
    {
        ptr[i] = 0;
    }
    return (void *)ptr;
}

typedef struct SceneModel
{
    long generation;
    Model model;
    const char *name;
    char isManaged;
    BoundingBox *meshBounds;
    Vector4 *meshBoundingSpheres;
} SceneModel;

typedef struct SceneNode
{
    long generation;
    SceneNodeId parent;
    SceneNodeId nextSiblingId;
    SceneNodeId firstChildId;

    // the generation of the transform data; is increased when TRS is modified
    unsigned long modTRSGeneration;
    // if modTRSMarker != modTRSGeneration, the TRS matrix is dirty
    unsigned long modTRSMarker;

    Vector3 position;
    Vector3 rotation;
    Vector3 scale;
    char *name;
    Matrix localToWorld;
    Matrix worldToLocal;

    // SceneNode metadata
    int userIdentifier;
    SceneModelId model;

    
} SceneNode;

typedef struct SceneComponentData
{
    unsigned char *componentData;
} SceneComponentData;

typedef struct Scene
{
    long generation;

    SceneComponentData sceneComponentData[256];

    SceneNodeId firstRoot, firstFree;
    SceneNode *nodes;
    unsigned long nodesCount;
    unsigned long nodesCapacity;

    SceneModel *models;
    unsigned long modelsCount;
    unsigned long modelsCapacity;

} Scene;

static Scene *scenes = 0;
static unsigned long scenesCount = 0;

static SceneNodeComponentDefinition sceneNodeComponentDefinitions[256] = {0};

void RegisterSceneNodeComponent(SceneNodeComponentDefinition definition)
{
    int index = definition.definitionId;
    if (sceneNodeComponentDefinitions[index].name)
    {
        TraceLog(LOG_WARNING, "RegisterSceneNodeComponent: definition with id %d already exists: %s vs %s", 
            index, definition.name, sceneNodeComponentDefinitions[index].name);
        return;
    }
    sceneNodeComponentDefinitions[index] = definition;
}

static SceneNode *GetSceneNode(SceneNodeId sceneNodeId, Scene **sceneOut);

// # Scene Management Functions
SceneId LoadScene()
{
    int useIndex = -1;
    for (unsigned long i = 0; i < scenesCount; i++)
    {
        if (scenes[i].generation < 0)
        {
            useIndex = i;
            break;
        }
    }

    if (useIndex == -1)
    {
        scenes = scenes ? MemRealloc(scenes, sizeof(Scene) * (scenesCount + 1)) : MemAlloc(sizeof(Scene));
        useIndex = scenesCount;
        scenesCount++;
    }

    SceneId sceneId = {useIndex, scenes[useIndex].generation};
    sceneId.generation = -sceneId.generation + 1;

    scenes[useIndex] = (Scene){
        .generation = sceneId.generation};

    return sceneId;
}

void UnloadScene(SceneId sceneId)
{
    if (sceneId.id >= scenesCount || scenes[sceneId.id].generation != sceneId.generation)
    {
        return;
    }

    scenes[sceneId.id].generation = -scenes[sceneId.id].generation;

    Scene *scene = &scenes[sceneId.id];
    // Unload all scene resources
    for (int i = 0; i < scene->modelsCount; i++)
    {
        SceneModel *sceneModel = &scene->models[i];
        MemFree(sceneModel->meshBounds);
        MemFree(sceneModel->meshBoundingSpheres);
        sceneModel->meshBounds = 0;
        sceneModel->meshBoundingSpheres = 0;

        if (sceneModel->generation < 0 || !sceneModel->isManaged)
        {
            continue;
        }

        UnloadModel(sceneModel->model);
    }

    if (scene->models)
    {
        MemFree(scene->models);
        scene->models = 0;
    }

    for (int i = 0; i < scene->nodesCount; i++)
    {
        SceneNode *node = &scene->nodes[i];
        if (node->generation < 0)
        {
            continue;
        }

        if (node->name)
        {
            MemFree(node->name);
            node->name = 0;
        }
    }

    // clean up all when last scene is unloaded
    for (unsigned long i = 0; i < scenesCount; i++)
    {
        if (scenes[i].generation > 0)
        {
            // still have a loaded scene, don't free anything
            return;
        }
    }

    MemFree(scenes);
    scenes = 0;
    scenesCount = 0;
}

int IsSceneValid(SceneId sceneId)
{
    return sceneId.id < scenesCount && scenes[sceneId.id].generation == sceneId.generation;
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

static void GetCameraFrustumPlanes(Camera3D camera, Vector4 *planes)
{
    // this algorithm is absolutely nowhere near optimal, but I am able
    // to understand it and it works; it can be optimized later
    Vector3 corners[8];
    CalcFrustumCorners(camera, corners);
    Vector3 nearCenter = Vector3Scale(Vector3Add(corners[0], corners[2]), 0.5f);
    Vector3 farCenter = Vector3Scale(Vector3Add(corners[4], corners[6]), 0.5f);
    Vector3 rightCenter = Vector3Scale(Vector3Add(corners[1], corners[6]), 0.5f);
    Vector3 leftCenter = Vector3Scale(Vector3Add(corners[0], corners[7]), 0.5f);
    Vector3 topCenter = Vector3Scale(Vector3Add(corners[2], corners[7]), 0.5f);
    Vector3 bottomCenter = Vector3Scale(Vector3Add(corners[0], corners[5]), 0.5f);

    Vector3 nearCenterNormal = Vector3Normalize(Vector3Subtract(nearCenter, camera.position));
    Vector3 farCenterNormal = Vector3Scale(nearCenterNormal, -1.0f);
    Vector3 rightCenterNormal = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(corners[2], corners[1]), Vector3Subtract(corners[6], corners[2])));
    Vector3 leftCenterNormal = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(corners[3], corners[0]), Vector3Subtract(corners[0], corners[4])));
    Vector3 topCenterNormal = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(corners[3], corners[2]), Vector3Subtract(corners[7], corners[3])));
    Vector3 bottomCenterNormal = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(corners[1], corners[0]), Vector3Subtract(corners[5], corners[1])));

    planes[0] = (Vector4){nearCenterNormal.x, nearCenterNormal.y, nearCenterNormal.z, Vector3DotProduct(nearCenter, nearCenterNormal)};
    planes[1] = (Vector4){farCenterNormal.x, farCenterNormal.y, farCenterNormal.z, Vector3DotProduct(farCenter, farCenterNormal)};
    planes[2] = (Vector4){rightCenterNormal.x, rightCenterNormal.y, rightCenterNormal.z, Vector3DotProduct(rightCenter, rightCenterNormal)};
    planes[3] = (Vector4){leftCenterNormal.x, leftCenterNormal.y, leftCenterNormal.z, Vector3DotProduct(leftCenter, leftCenterNormal)};
    planes[4] = (Vector4){topCenterNormal.x, topCenterNormal.y, topCenterNormal.z, Vector3DotProduct(topCenter, topCenterNormal)};
    planes[5] = (Vector4){bottomCenterNormal.x, bottomCenterNormal.y, bottomCenterNormal.z, Vector3DotProduct(bottomCenter, bottomCenterNormal)};
}

static void DrawPlaneEq(Vector4 planeEq, Color color)
{
    Vector3 normal = (Vector3){planeEq.x, planeEq.y, planeEq.z};
    Vector3 point = Vector3Scale(normal, planeEq.w);
    Vector3 right = Vector3Normalize(Vector3CrossProduct(normal, (Vector3){0, 1, 0}));
    Vector3 up = Vector3Normalize(Vector3CrossProduct(right, normal));
    DrawLine3D(point, Vector3Add(point, right), RED);
    DrawLine3D(point, Vector3Add(point, up), GREEN);
    DrawLine3D(point, Vector3Subtract(point, right), RED);
    DrawLine3D(point, Vector3Subtract(point, up), GREEN);
}

int CheckCollisionBoxFrustum(BoundingBox box, Vector4 *planes, Matrix transform)
{
    Vector3 corners[8] = {
        (Vector3){box.min.x, box.min.y, box.min.z},
        (Vector3){box.max.x, box.min.y, box.min.z},
        (Vector3){box.max.x, box.max.y, box.min.z},
        (Vector3){box.min.x, box.max.y, box.min.z},
        (Vector3){box.min.x, box.min.y, box.max.z},
        (Vector3){box.max.x, box.min.y, box.max.z},
        (Vector3){box.max.x, box.max.y, box.max.z},
        (Vector3){box.min.x, box.max.y, box.max.z}};
    for (int i = 0; i < 8; i++)
    {
        corners[i] = Vector3Transform(corners[i], transform);
    }

    for (int i = 0; i < 6; i++)
    {
        int out = 0;
        Vector3 planeCenter = Vector3Scale((Vector3){planes[i].x, planes[i].y, planes[i].z}, planes[i].w);
        Vector3 planeNormal = (Vector3){planes[i].x, planes[i].y, planes[i].z};
        for (int j = 0; j < 8; j++)
        {
            Vector3 relative = Vector3Subtract(corners[j], planeCenter);
            if (Vector3DotProduct(planeNormal, relative) < 0)
            {
                // DrawLine3D(corners[j], planeCenter, BLUE);
                // Vector3 planeNormal = Vector3Normalize((Vector3){planes[i].x, planes[i].y, planes[i].z});
                // DrawLine3D(planeCenter, Vector3Add(planeCenter, planeNormal), RED);
                out++;
            }
            else
            {
                break;
            }
        }

        if (out == 8)
        {
            return 0;
        }
    }

    return 1;
}

SceneDrawStats DrawScene(SceneId sceneId, SceneDrawConfig config)
{
    SceneDrawStats stats = {0};
    if (!IsSceneValid(sceneId))
    {
        return stats;
    }

    Camera3D camera = config.camera; 
    Matrix transform = config.transform;
    unsigned long layerMask = config.layerMask;
    int sortMode = config.sortMode;
    char drawBoundingBoxes = config.drawBoundingBoxes;

    Vector4 frustumPlanes[6];
    GetCameraFrustumPlanes(camera, frustumPlanes);

    if (config.drawCameraFrustum)
    {
        DrawPlaneEq(frustumPlanes[0], GREEN);
        DrawPlaneEq(frustumPlanes[1], GREEN);
        DrawPlaneEq(frustumPlanes[2], GREEN);
        DrawPlaneEq(frustumPlanes[3], GREEN);
        DrawPlaneEq(frustumPlanes[4], GREEN);
        DrawPlaneEq(frustumPlanes[5], GREEN);
    }


    Scene *scene = &scenes[sceneId.id];
    for (int i = 0; i < scene->nodesCount; i++)
    {
        SceneNode *node = &scene->nodes[i];
        if (node->generation < 0)
        {
            continue;
        }

        SceneModel *sceneModel = &scene->models[node->model.id];
        if (sceneModel->generation != node->model.generation)
        {
            continue;
        }

        Matrix matrix = GetSceneNodeLocalTransform((SceneNodeId){sceneId, i, node->generation});
        Model model = sceneModel->model;
        for (int i = 0; i < model.meshCount; i++)
        {
            BoundingBox box = sceneModel->meshBounds[i];
            if (!CheckCollisionBoxFrustum(box, frustumPlanes, matrix))
            {
                stats.culledMeshCount++;
                continue;
            }

            Color color = model.materials[model.meshMaterial[i]].maps[MATERIAL_MAP_DIFFUSE].color;

            Color colorTint = WHITE;
            model.materials[model.meshMaterial[i]].maps[MATERIAL_MAP_DIFFUSE].color = colorTint;
            DrawMesh(model.meshes[i], model.materials[model.meshMaterial[i]], matrix);
            model.materials[model.meshMaterial[i]].maps[MATERIAL_MAP_DIFFUSE].color = color;

            stats.meshDrawCount++;
            stats.trianglesDrawCount += model.meshes[i].vertexCount / 3;
        }
    }
    if (drawBoundingBoxes)
    {
        for (int i = 0; i < scene->nodesCount; i++)
        {
            SceneNode *node = &scene->nodes[i];
            if (node->generation < 0)
            {
                continue;
            }

            SceneModel *sceneModel = &scene->models[node->model.id];
            if (sceneModel->generation != node->model.generation)
            {
                continue;
            }

            Matrix matrix = GetSceneNodeLocalTransform((SceneNodeId){sceneId, i, node->generation});
            Model model = sceneModel->model;
            rlPushMatrix();
            rlMultMatrixf(MatrixToFloat(matrix));
            for (int i = 0; i < model.meshCount; i++)
            {
                BoundingBox box = sceneModel->meshBounds[i];
                DrawBoundingBox(box, RED);
            }
            rlPopMatrix();
        }
    }

    return stats;
}

SceneModelId AddModelToScene(SceneId sceneId, Model model, const char *name, int manageModel)
{
    if (!IsSceneValid(sceneId))
    {
        return (SceneModelId){0};
    }

    Scene *scene = &scenes[sceneId.id];
    SceneModel *sceneModel = ListAlloc((void **)&scene->models, &scene->modelsCount, &scene->modelsCapacity, sizeof(SceneModel));
    int index = scene->modelsCount - 1;
    *sceneModel = (SceneModel){
        .generation = sceneModel->generation + 1,
        .model = model,
        .name = name,
        .isManaged = manageModel};
    
    sceneModel->meshBounds = MemAlloc(sizeof(BoundingBox) * model.meshCount);
    sceneModel->meshBoundingSpheres = MemAlloc(sizeof(Vector4) * model.meshCount);
    for (int i = 0; i < model.meshCount; i++)
    {
        BoundingBox box = GetMeshBoundingBox(model.meshes[i]);
        sceneModel->meshBounds[i] = box;
        Vector3 center = Vector3Scale(Vector3Add(box.min, box.max), 0.5f);
        float radius = Vector3Distance(center, box.max);
        sceneModel->meshBoundingSpheres[i] = (Vector4){center.x, center.y, center.z, radius};
    }

    return (SceneModelId){sceneId, index, sceneModel->generation};
}

static Scene *GetScene(SceneId sceneId)
{
    if (!IsSceneValid(sceneId))
    {
        return 0;
    }

    return &scenes[sceneId.id];
}

void AddGLTFScene(SceneId sceneId, const char *filename, Matrix transform)
{
    if (!IsSceneValid(sceneId))
    {
        return;
    }
}

SceneNodeId AcquireSceneNode(SceneId sceneId)
{
    Scene *scene = GetScene(sceneId);
    if (!scene)
    {
        return (SceneNodeId){0};
    }

    SceneNode *node = GetSceneNode(scene->firstFree, 0);
    int index;
    if (!node)
    {
        node = ListAlloc((void **)&scene->nodes, &scene->nodesCount, &scene->nodesCapacity, sizeof(SceneNode));
        index = scene->nodesCount - 1;
    }
    else
    {
        index = scene->firstFree.id;
        scene->firstFree = node->nextSiblingId;
    }

    *node = (SceneNode){
        .generation = node->generation + 1,
        .position = (Vector3){0, 0, 0},
        .rotation = (Vector3){0, 0, 0},
        .scale = (Vector3){1, 1, 1},
        .name = 0,
        .modTRSMarker = 0,
        .modTRSGeneration = 1,
        .localToWorld = MatrixIdentity(),
        .worldToLocal = MatrixIdentity(),
        .userIdentifier = 0,
        .parent = (SceneNodeId){0},
        .model = (SceneModelId){0}};

    return (SceneNodeId){sceneId, index, node->generation};
}

static unsigned long GetSceneNodeGenerationSum(SceneNode *node)
{
    unsigned long generationSum = node->modTRSGeneration;
    SceneNode *parentNode = GetSceneNode(node->parent, 0);
    while (parentNode)
    {
        // TraceLog(LOG_INFO, "parentNode: %p", parentNode);
        generationSum += parentNode->modTRSGeneration;
        parentNode = GetSceneNode(parentNode->parent, 0);
    }

    return generationSum;
}

// returns the node if it is dirty, otherwise 0; saves pointer resolving for callee
static SceneNode *IsSceneNodeTRSDirty(SceneNodeId sceneNodeId)
{
    Scene *scene;
    SceneNode *node = GetSceneNode(sceneNodeId, &scene);
    if (!node)
    {
        return 0;
    }

    unsigned long generationSum = GetSceneNodeGenerationSum(node);
    return node->modTRSMarker != generationSum ? node : 0;
}

static SceneNode *GetSceneNode(SceneNodeId sceneNodeId, Scene **sceneOut)
{
    Scene *scene = GetScene(sceneNodeId.sceneId);
    if (!scene)
    {
        return 0;
    }

    if (sceneNodeId.id >= scene->nodesCount || scene->nodes[sceneNodeId.id].generation != sceneNodeId.generation)
    {
        return 0;
    }

    if (sceneOut)
        *sceneOut = scene;

    return &scene->nodes[sceneNodeId.id];
}

int IsSceneNodeValid(SceneNodeId sceneNodeId)
{
    Scene *scene;
    return GetSceneNode(sceneNodeId, &scene) != 0;
}

void SetSceneNodeParent(SceneNodeId sceneNodeId, SceneNodeId parentSceneNodeId)
{
    if (parentSceneNodeId.sceneId.id != sceneNodeId.sceneId.id)
    {
        TraceLog(LOG_WARNING, "SetSceneNodeParent: scene node and parent must be in the same scene");
        return;
    }

    Scene *scene;
    SceneNode *node = GetSceneNode(sceneNodeId, &scene);
    if (!node)
    {
        return;
    }

    SceneNode *parentNode = GetSceneNode(parentSceneNodeId, 0);
    if (!parentNode)
    {
        return;
    }

    node->parent = parentSceneNodeId;
    node->nextSiblingId = parentNode->firstChildId;
    parentNode->firstChildId = sceneNodeId;
    node->modTRSGeneration = 0;
}

// releases a scene node (destroy) and all its children
void ReleaseSceneNode(SceneNodeId sceneNodeId)
{
    Scene *scene;
    SceneNode *node = GetSceneNode(sceneNodeId, &scene);
    if (!node)
    {
        return;
    }

    SceneNode *parentNode = GetSceneNode(node->parent, 0);
    if (parentNode)
    {
        // remove from parent child list
        SceneNodeId siblingId = parentNode->firstChildId;
        if (siblingId.id == sceneNodeId.id)
        {
            parentNode->firstChildId = node->nextSiblingId;
        }
        else
        {
            SceneNode *sibling = GetSceneNode(siblingId, 0);
            while (sibling && sibling->nextSiblingId.id != sceneNodeId.id)
            {
                siblingId = sibling->nextSiblingId;
                sibling = GetSceneNode(siblingId, 0);
            }
            sibling->nextSiblingId = node->nextSiblingId;
        }
    }

    node->generation++;
    if (node->name)
    {
        MemFree(node->name);
        node->name = 0;
    }

    // release children
    SceneNodeId childId = node->firstChildId;
    SceneNode* child = GetSceneNode(childId, 0);
    while (child)
    {
        SceneNodeId nextChildId = child->nextSiblingId;
        ReleaseSceneNode(childId);
        child = GetSceneNode(nextChildId, 0);
        childId = nextChildId;
    }

    // add to free list
    node->parent = (SceneNodeId){0};
    node->nextSiblingId = scene->firstFree;
    scene->firstFree = sceneNodeId;
}

void SetSceneNodePosition(SceneNodeId sceneNodeId, float x, float y, float z)
{
    SceneNode *node = GetSceneNode(sceneNodeId, 0);
    if (!node)
    {
        return;
    }

    node->position = (Vector3){x, y, z};
    node->modTRSGeneration += 1;
}

void SetSceneNodeRotation(SceneNodeId sceneNodeId, float eulerXDeg, float eulerYDeg, float eulerZDeg)
{
    SceneNode *node = GetSceneNode(sceneNodeId, 0);
    if (!node)
    {
        return;
    }

    node->rotation = (Vector3){eulerXDeg, eulerYDeg, eulerZDeg};
    node->modTRSGeneration += 1;
}

void SetSceneNodeScale(SceneNodeId sceneNodeId, float x, float y, float z)
{
    SceneNode *node = GetSceneNode(sceneNodeId, 0);
    if (!node)
    {
        return;
    }

    node->scale = (Vector3){x, y, z};
    node->modTRSGeneration += 1;
}

void SetSceneNodePositionV(SceneNodeId sceneNodeId, Vector3 position)
{
    SetSceneNodePosition(sceneNodeId, position.x, position.y, position.z);
}

void SetSceneNodeRotationV(SceneNodeId sceneNodeId, Vector3 rotation)
{
    SetSceneNodeRotation(sceneNodeId, rotation.x, rotation.y, rotation.z);
}

void SetSceneNodeScaleV(SceneNodeId sceneNodeId, Vector3 scale)
{
    SetSceneNodeScale(sceneNodeId, scale.x, scale.y, scale.z);
}

Vector3 GetSceneNodeLocalPosition(SceneNodeId sceneNodeId)
{
    SceneNode *node = GetSceneNode(sceneNodeId, 0);
    if (!node)
    {
        return (Vector3){0, 0, 0};
    }

    return node->position;
}

Vector3 GetSceneNodeLocalRotation(SceneNodeId sceneNodeId)
{
    SceneNode *node = GetSceneNode(sceneNodeId, 0);
    if (!node)
    {
        return (Vector3){0, 0, 0};
    }

    return node->rotation;
}

Vector3 GetSceneNodeLocalScale(SceneNodeId sceneNodeId)
{
    SceneNode *node = GetSceneNode(sceneNodeId, 0);
    if (!node)
    {
        return (Vector3){1, 1, 1};
    }

    return node->scale;
}

static SceneNode *UpdateSceneNodeTRS(SceneNodeId sceneNodeId)
{
    SceneNode *node = IsSceneNodeTRSDirty(sceneNodeId);
    if (!node)
    {
        return GetSceneNode(sceneNodeId, 0);
    }

    SceneNode *parentNode = GetSceneNode(node->parent, 0);
    node->localToWorld = MatrixIdentity();
    node->localToWorld = MatrixMultiply(node->localToWorld, MatrixScale(node->scale.x, node->scale.y, node->scale.z));
    node->localToWorld = MatrixMultiply(node->localToWorld, MatrixRotateXYZ((Vector3){DEG2RAD * node->rotation.x, DEG2RAD * node->rotation.y, DEG2RAD * node->rotation.z}));
    node->localToWorld = MatrixMultiply(node->localToWorld, MatrixTranslate(node->position.x, node->position.y, node->position.z));
    if (parentNode)
    {
        UpdateSceneNodeTRS(node->parent);
        node->localToWorld = MatrixMultiply(node->localToWorld, parentNode->localToWorld);
    }

    unsigned long generationSum = GetSceneNodeGenerationSum(node);
    node->modTRSMarker = generationSum;

    return node;
}

Matrix GetSceneNodeLocalTransform(SceneNodeId sceneNodeId)
{
    SceneNode *node = UpdateSceneNodeTRS(sceneNodeId);
    if (!node)
    {
        return MatrixIdentity();
    }

    return node->localToWorld;
}

Vector3 GetSceneNodeWorldPosition(SceneNodeId sceneNodeId)
{
    Matrix localToWorld = GetSceneNodeLocalTransform(sceneNodeId);
    return (Vector3){localToWorld.m12, localToWorld.m13, localToWorld.m14};
}

Vector3 GetSceneNodeWorldForward(SceneNodeId sceneNodeId)
{
    Matrix localToWorld = GetSceneNodeLocalTransform(sceneNodeId);
    return (Vector3){localToWorld.m8, localToWorld.m9, localToWorld.m10};
}

Vector3 GetSceneNodeWorldUp(SceneNodeId sceneNodeId)
{
    Matrix localToWorld = GetSceneNodeLocalTransform(sceneNodeId);
    return (Vector3){localToWorld.m4, localToWorld.m5, localToWorld.m6};
}

Vector3 GetSceneNodeWorldRight(SceneNodeId sceneNodeId)
{
    Matrix localToWorld = GetSceneNodeLocalTransform(sceneNodeId);
    return (Vector3){localToWorld.m0, localToWorld.m1, localToWorld.m2};
}

static char *StringDup(const char *str)
{
    if (!str)
    {
        return 0;
    }

    unsigned long len = strlen(str);
    char *dup = MemAlloc(len + 1);
    strcpy(dup, str);
    return dup;
}

int SetSceneNodeName(SceneNodeId sceneNodeId, const char *name)
{
    SceneNode *node = GetSceneNode(sceneNodeId, 0);
    if (!node)
    {
        return 0;
    }

    if (node->name)
    {
        MemFree(node->name);
    }
    node->name = StringDup(name);

    return 1;
}

const char *GetSceneNodeName(SceneNodeId sceneNodeId)
{
    SceneNode *node = GetSceneNode(sceneNodeId, 0);
    if (!node)
    {
        return 0;
    }

    return node->name;
}

int GetSceneNodeIdentifier(SceneNodeId sceneNodeId)
{
    SceneNode *node = GetSceneNode(sceneNodeId, 0);
    if (!node)
    {
        return 0;
    }

    return node->userIdentifier;
}

int SetSceneNodeIdentifier(SceneNodeId sceneNodeId, int identifier)
{
    SceneNode *node = GetSceneNode(sceneNodeId, 0);
    if (!node)
    {
        return 0;
    }

    node->userIdentifier = identifier;
    return 1;
}

void SetSceneNodeModel(SceneNodeId sceneNodeId, SceneModelId model)
{
    SceneNode *node = GetSceneNode(sceneNodeId, 0);
    if (!node)
    {
        return;
    }

    node->model = model;
}