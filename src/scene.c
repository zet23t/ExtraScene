#include <raylib.h>
#include <raymath.h>
#include <scene.h>
#include <string.h>

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
} SceneModel;

typedef struct SceneNode
{
    long generation;
    SceneNodeId nextSiblingId;
    SceneNodeId firstChildId;
    // the generation of the transform data; is increased when TRS is modified
    unsigned long modTRSGeneration;
    unsigned long modTRSMarker;

    Vector3 position;
    Vector3 rotation;
    Vector3 scale;
    char *name;
    Matrix localToWorld;
    Matrix worldToLocal;

    // SceneNode metadata
    int userIdentifier;
    SceneNodeId parent;
    SceneModelId model;
} SceneNode;

typedef struct Scene
{
    long generation;

    SceneNodeId firstRoot, firstFree;
    SceneNode *nodes;
    unsigned long nodesCount;
    unsigned long nodesCapacity;

    SceneModel *models;
    unsigned long modelsCount;
    unsigned long modelsCapacity;

} Scene;

Scene *scenes = 0;
unsigned long scenesCount = 0;

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

void DrawScene(SceneId sceneId, Camera3D camera, Matrix transform, unsigned long layerMask, int sortMode)
{
    if (!IsSceneValid(sceneId))
    {
        return;
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
            Color color = model.materials[model.meshMaterial[i]].maps[MATERIAL_MAP_DIFFUSE].color;

            Color colorTint = WHITE;
            model.materials[model.meshMaterial[i]].maps[MATERIAL_MAP_DIFFUSE].color = colorTint;
            DrawMesh(model.meshes[i], model.materials[model.meshMaterial[i]], matrix);
            model.materials[model.meshMaterial[i]].maps[MATERIAL_MAP_DIFFUSE].color = color;
        }
    }
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