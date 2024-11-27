#ifndef SCENE_H
#define SCENE_H

#include <raylib.h>
#include <raymath.h>
/*
This is a draft for a scene graph system for raylib.

Primary goals:
- Loading model data from files (GLTF) retaining hierarchy, naming and transform data
- Pointer safety: worry free API for adding, removing and modifying nodes; 
                  no double frees, no use after free, no use after return
- Ease of use: simple API, header only implementation
- raylib API style

Secondary goals:
- Hierarchy manipulation: parent, child, sibling, ancestor, descendant queries
- Transform handling (local to world, world to local)
- Frustum culling
- Scene graph traversal
- Draw order sorting and filtering
*/

#define SCENE_DRAW_SORT_NONE 0
#define SCENE_DRAW_SORT_FRONT_TO_BACK 1
#define SCENE_DRAW_SORT_BACK_TO_FRONT 2
#define SCENE_DRAW_SORT_HIERARCHY 3

typedef struct SceneId {
    unsigned long id;
    long generation;
} SceneId;

typedef struct SceneNodeId {
    SceneId sceneId;
    unsigned long id;
    long generation;
} SceneNodeId;

typedef struct SceneModelId {
    // models can be shared between scenes
    SceneId ownerSceneId;
    unsigned long id;
    long generation;
} SceneModelId;

typedef struct SceneDrawConfig {
    Camera3D camera;
    Matrix transform;
    unsigned long layerMask;
    unsigned char sortMode;
    unsigned char drawBoundingBoxes: 1;
    unsigned char drawCameraFrustum: 1;
} SceneDrawConfig;

typedef struct SceneDrawStats {
    unsigned long culledMeshCount;
    unsigned long meshDrawCount;
    unsigned long trianglesDrawCount;
} SceneDrawStats;

// creates a new empty scene
SceneId LoadScene();
void UnloadScene(SceneId sceneId);
int IsSceneValid(SceneId sceneId);
SceneDrawStats DrawScene(SceneId sceneId, SceneDrawConfig config);
SceneModelId AddModelToScene(SceneId sceneId, Model model, const char* name, int manageModel);
void TraverseSceneNodes(SceneId sceneId, void (*callback)(SceneNodeId, void*), void* data);

SceneNodeId AcquireSceneNode(SceneId sceneId);
void ReleaseSceneNode(SceneNodeId sceneNodeId);
int IsSceneNodeValid(SceneNodeId sceneNodeId);

void SetSceneNodeParent(SceneNodeId sceneNodeId, SceneNodeId parentSceneNodeId);
SceneNodeId GetSceneNodeFirstRoot(SceneNodeId sceneNodeId);
SceneNodeId GetSceneNodeParent(SceneNodeId sceneNodeId);
SceneNodeId GetSceneNodeFirstChild(SceneNodeId sceneNodeId);
SceneNodeId GetSceneNodeNextSibling(SceneNodeId sceneNodeId);

void SetSceneNodePosition(SceneNodeId sceneNodeId, float x, float y, float z);
void SetSceneNodeRotation(SceneNodeId sceneNodeId, float eulerXDeg, float eulerYDeg, float eulerZDeg);
void SetSceneNodeScale(SceneNodeId sceneNodeId, float x, float y, float z);
void SetSceneNodePositionV(SceneNodeId sceneNodeId, Vector3 position);
void SetSceneNodeRotationV(SceneNodeId sceneNodeId, Vector3 rotation);
void SetSceneNodeScaleV(SceneNodeId sceneNodeId, Vector3 scale);

Matrix GetSceneNodeLocalTransform(SceneNodeId sceneNodeId);

Vector3 GetSceneNodeLocalPosition(SceneNodeId sceneNodeId);
Vector3 GetSceneNodeLocalRotation(SceneNodeId sceneNodeId);
Vector3 GetSceneNodeLocalScale(SceneNodeId sceneNodeId);
Vector3 GetSceneNodeWorldPosition(SceneNodeId sceneNodeId);
Vector3 GetSceneNodeWorldForward(SceneNodeId sceneNodeId);
Vector3 GetSceneNodeWorldUp(SceneNodeId sceneNodeId);
Vector3 GetSceneNodeWorldRight(SceneNodeId sceneNodeId);

int SetSceneNodeName(SceneNodeId sceneNodeId, const char* name);
const char* GetSceneNodeName(SceneNodeId sceneNodeId);

int GetSceneNodeIdentifier(SceneNodeId sceneNodeId);
int SetSceneNodeIdentifier(SceneNodeId sceneNodeId, int identifier);

void SetSceneNodeModel(SceneNodeId sceneNodeId, SceneModelId model);

void AddGLTFScene(SceneId sceneId, const char* filename, Matrix transform);

#endif