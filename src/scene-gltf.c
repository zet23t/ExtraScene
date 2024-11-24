// # GLTF loading

#include "raylib.h"
#include "raymath.h"

#if !defined(SUPPORT_FILEFORMAT_GLTF)
    #define CGLTF_MALLOC RL_MALLOC
    #define CGLTF_FREE RL_FREE

    #define CGLTF_IMPLEMENTATION
    #include "external/cgltf.h"         // glTF file format loading
#else
    #include "external/cgltf.h"         // glTF file format loading
#endif

// Load file data callback for cgltf
static cgltf_result LoadFileGLTFCallback(const struct cgltf_memory_options *memoryOptions, const struct cgltf_file_options *fileOptions, const char *path, cgltf_size *size, void **data)
{
    int filesize;
    unsigned char *filedata = LoadFileData(path, &filesize);

    if (filedata == NULL) return cgltf_result_io_error;

    *size = filesize;
    *data = filedata;

    return cgltf_result_success;
}

// Release file data callback for cgltf
static void ReleaseFileGLTFCallback(const struct cgltf_memory_options *memoryOptions, const struct cgltf_file_options *fileOptions, void *data)
{
    UnloadFileData(data);
}


#define TRACELOG TraceLog

// Load image from different glTF provided methods (uri, path, buffer_view)
static Image LoadImageFromCgltfImage(cgltf_image *cgltfImage, const char *texPath)
{
    Image image = { 0 };

    if (cgltfImage->uri != NULL)     // Check if image data is provided as an uri (base64 or path)
    {
        if ((strlen(cgltfImage->uri) > 5) &&
            (cgltfImage->uri[0] == 'd') &&
            (cgltfImage->uri[1] == 'a') &&
            (cgltfImage->uri[2] == 't') &&
            (cgltfImage->uri[3] == 'a') &&
            (cgltfImage->uri[4] == ':'))     // Check if image is provided as base64 text data
        {
            // Data URI Format: data:<mediatype>;base64,<data>

            // Find the comma
            int i = 0;
            while ((cgltfImage->uri[i] != ',') && (cgltfImage->uri[i] != 0)) i++;

            if (cgltfImage->uri[i] == 0) TRACELOG(LOG_WARNING, "IMAGE: glTF data URI is not a valid image");
            else
            {
                int base64Size = (int)strlen(cgltfImage->uri + i + 1);
                while (cgltfImage->uri[i + base64Size] == '=') base64Size--;    // Ignore optional paddings
                int numberOfEncodedBits = base64Size*6 - (base64Size*6) % 8 ;   // Encoded bits minus extra bits, so it becomes a multiple of 8 bits
                int outSize = numberOfEncodedBits/8 ;                           // Actual encoded bytes
                void *data = NULL;

                cgltf_options options = { 0 };
                options.file.read = LoadFileGLTFCallback;
                options.file.release = ReleaseFileGLTFCallback;
                cgltf_result result = cgltf_load_buffer_base64(&options, outSize, cgltfImage->uri + i + 1, &data);

                if (result == cgltf_result_success)
                {
                    image = LoadImageFromMemory(".png", (unsigned char *)data, outSize);
                    RL_FREE(data);
                }
            }
        }
        else     // Check if image is provided as image path
        {
            image = LoadImage(TextFormat("%s/%s", texPath, cgltfImage->uri));
        }
    }
    else if (cgltfImage->buffer_view->buffer->data != NULL)    // Check if image is provided as data buffer
    {
        unsigned char *data = RL_MALLOC(cgltfImage->buffer_view->size);
        int offset = (int)cgltfImage->buffer_view->offset;
        int stride = (int)cgltfImage->buffer_view->stride? (int)cgltfImage->buffer_view->stride : 1;

        // Copy buffer data to memory for loading
        for (unsigned int i = 0; i < cgltfImage->buffer_view->size; i++)
        {
            data[i] = ((unsigned char *)cgltfImage->buffer_view->buffer->data)[offset];
            offset += stride;
        }

        // Check mime_type for image: (cgltfImage->mime_type == "image/png")
        // NOTE: Detected that some models define mime_type as "image\\/png"
        if ((strcmp(cgltfImage->mime_type, "image\\/png") == 0) ||
            (strcmp(cgltfImage->mime_type, "image/png") == 0)) image = LoadImageFromMemory(".png", data, (int)cgltfImage->buffer_view->size);
        else if ((strcmp(cgltfImage->mime_type, "image\\/jpeg") == 0) ||
                 (strcmp(cgltfImage->mime_type, "image/jpeg") == 0)) image = LoadImageFromMemory(".jpg", data, (int)cgltfImage->buffer_view->size);
        else TRACELOG(LOG_WARNING, "MODEL: glTF image data MIME type not recognized", TextFormat("%s/%s", texPath, cgltfImage->uri));

        RL_FREE(data);
    }

    return image;
}

// Load bone info from GLTF skin data
static BoneInfo *LoadBoneInfoGLTF(cgltf_skin skin, int *boneCount)
{
    *boneCount = (int)skin.joints_count;
    BoneInfo *bones = RL_MALLOC(skin.joints_count*sizeof(BoneInfo));

    for (unsigned int i = 0; i < skin.joints_count; i++)
    {
        cgltf_node node = *skin.joints[i];
        if (node.name != NULL)
        {
            strncpy(bones[i].name, node.name, sizeof(bones[i].name));
            bones[i].name[sizeof(bones[i].name) - 1] = '\0';
        }

        // Find parent bone index
        int parentIndex = -1;

        for (unsigned int j = 0; j < skin.joints_count; j++)
        {
            if (skin.joints[j] == node.parent)
            {
                parentIndex = (int)j;
                break;
            }
        }

        bones[i].parent = parentIndex;
    }

    return bones;
}

// Load glTF file into model struct, .gltf and .glb supported
static Model LoadGLTF(const char *fileName)
{
    /*********************************************************************************************

        Function implemented by Wilhem Barbier(@wbrbr), with modifications by Tyler Bezera(@gamerfiend)
        Transform handling implemented by Paul Melis (@paulmelis).
        Reviewed by Ramon Santamaria (@raysan5)

        FEATURES:
          - Supports .gltf and .glb files
          - Supports embedded (base64) or external textures
          - Supports PBR metallic/roughness flow, loads material textures, values and colors
                     PBR specular/glossiness flow and extended texture flows not supported
          - Supports multiple meshes per model (every primitives is loaded as a separate mesh)
          - Supports basic animations
          - Transforms, including parent-child relations, are applied on the mesh data, but the
            hierarchy is not kept (as it can't be represented).
          - Mesh instances in the glTF file (i.e. same mesh linked from multiple nodes)
            are turned into separate raylib Meshes.

        RESTRICTIONS:
          - Only triangle meshes supported
          - Vertex attribute types and formats supported:
              > Vertices (position): vec3: float
              > Normals: vec3: float
              > Texcoords: vec2: float
              > Colors: vec4: u8, u16, f32 (normalized)
              > Indices: u16, u32 (truncated to u16)
          - Scenes defined in the glTF file are ignored. All nodes in the file
            are used.

    ***********************************************************************************************/

    // Macro to simplify attributes loading code
    #define LOAD_ATTRIBUTE(accesor, numComp, srcType, dstPtr) LOAD_ATTRIBUTE_CAST(accesor, numComp, srcType, dstPtr, srcType)

    #define LOAD_ATTRIBUTE_CAST(accesor, numComp, srcType, dstPtr, dstType) \
    { \
        int n = 0; \
        srcType *buffer = (srcType *)accesor->buffer_view->buffer->data + accesor->buffer_view->offset/sizeof(srcType) + accesor->offset/sizeof(srcType); \
        for (unsigned int k = 0; k < accesor->count; k++) \
        {\
            for (int l = 0; l < numComp; l++) \
            {\
                dstPtr[numComp*k + l] = (dstType)buffer[n + l];\
            }\
            n += (int)(accesor->stride/sizeof(srcType));\
        }\
    }

    Model model = { 0 };

    // glTF file loading
    int dataSize = 0;
    unsigned char *fileData = LoadFileData(fileName, &dataSize);

    if (fileData == NULL) return model;

    // glTF data loading
    cgltf_options options = { 0 };
    options.file.read = LoadFileGLTFCallback;
    options.file.release = ReleaseFileGLTFCallback;
    cgltf_data *data = NULL;
    cgltf_result result = cgltf_parse(&options, fileData, dataSize, &data);

    if (result == cgltf_result_success)
    {
        if (data->file_type == cgltf_file_type_glb) TRACELOG(LOG_INFO, "MODEL: [%s] Model basic data (glb) loaded successfully", fileName);
        else if (data->file_type == cgltf_file_type_gltf) TRACELOG(LOG_INFO, "MODEL: [%s] Model basic data (glTF) loaded successfully", fileName);
        else TRACELOG(LOG_WARNING, "MODEL: [%s] Model format not recognized", fileName);

        TRACELOG(LOG_INFO, "    > Meshes count: %i", data->meshes_count);
        TRACELOG(LOG_INFO, "    > Materials count: %i (+1 default)", data->materials_count);
        TRACELOG(LOG_DEBUG, "    > Buffers count: %i", data->buffers_count);
        TRACELOG(LOG_DEBUG, "    > Images count: %i", data->images_count);
        TRACELOG(LOG_DEBUG, "    > Textures count: %i", data->textures_count);

        // Force reading data buffers (fills buffer_view->buffer->data)
        // NOTE: If an uri is defined to base64 data or external path, it's automatically loaded
        result = cgltf_load_buffers(&options, data, fileName);
        if (result != cgltf_result_success) TRACELOG(LOG_INFO, "MODEL: [%s] Failed to load mesh/material buffers", fileName);

        int primitivesCount = 0;
        // NOTE: We will load every primitive in the glTF as a separate raylib Mesh.
        // Determine total number of meshes needed from the node hierarchy.
        for (unsigned int i = 0; i < data->nodes_count; i++)
        {
            cgltf_node *node = &(data->nodes[i]);
            cgltf_mesh *mesh = node->mesh;
            if (!mesh)
                continue;

            for (unsigned int p = 0; p < mesh->primitives_count; p++)
            {
                if (mesh->primitives[p].type == cgltf_primitive_type_triangles)
                    primitivesCount++;
            }
        }
        TRACELOG(LOG_DEBUG, "    > Primitives (triangles only) count based on hierarchy : %i", primitivesCount);

        // Load our model data: meshes and materials
        model.meshCount = primitivesCount;
        model.meshes = RL_CALLOC(model.meshCount, sizeof(Mesh));

        // NOTE: We keep an extra slot for default material, in case some mesh requires it
        model.materialCount = (int)data->materials_count + 1;
        model.materials = RL_CALLOC(model.materialCount, sizeof(Material));
        model.materials[0] = LoadMaterialDefault();     // Load default material (index: 0)

        // Load mesh-material indices, by default all meshes are mapped to material index: 0
        model.meshMaterial = RL_CALLOC(model.meshCount, sizeof(int));

        // Load materials data
        //----------------------------------------------------------------------------------------------------
        for (unsigned int i = 0, j = 1; i < data->materials_count; i++, j++)
        {
            model.materials[j] = LoadMaterialDefault();
            const char *texPath = GetDirectoryPath(fileName);

            // Check glTF material flow: PBR metallic/roughness flow
            // NOTE: Alternatively, materials can follow PBR specular/glossiness flow
            if (data->materials[i].has_pbr_metallic_roughness)
            {
                // Load base color texture (albedo)
                if (data->materials[i].pbr_metallic_roughness.base_color_texture.texture)
                {
                    Image imAlbedo = LoadImageFromCgltfImage(data->materials[i].pbr_metallic_roughness.base_color_texture.texture->image, texPath);
                    if (imAlbedo.data != NULL)
                    {
                        model.materials[j].maps[MATERIAL_MAP_ALBEDO].texture = LoadTextureFromImage(imAlbedo);
                        UnloadImage(imAlbedo);
                    }
                }
                // Load base color factor (tint)
                model.materials[j].maps[MATERIAL_MAP_ALBEDO].color.r = (unsigned char)(data->materials[i].pbr_metallic_roughness.base_color_factor[0]*255);
                model.materials[j].maps[MATERIAL_MAP_ALBEDO].color.g = (unsigned char)(data->materials[i].pbr_metallic_roughness.base_color_factor[1]*255);
                model.materials[j].maps[MATERIAL_MAP_ALBEDO].color.b = (unsigned char)(data->materials[i].pbr_metallic_roughness.base_color_factor[2]*255);
                model.materials[j].maps[MATERIAL_MAP_ALBEDO].color.a = (unsigned char)(data->materials[i].pbr_metallic_roughness.base_color_factor[3]*255);

                // Load metallic/roughness texture
                if (data->materials[i].pbr_metallic_roughness.metallic_roughness_texture.texture)
                {
                    Image imMetallicRoughness = LoadImageFromCgltfImage(data->materials[i].pbr_metallic_roughness.metallic_roughness_texture.texture->image, texPath);
                    if (imMetallicRoughness.data != NULL)
                    {
                        model.materials[j].maps[MATERIAL_MAP_ROUGHNESS].texture = LoadTextureFromImage(imMetallicRoughness);
                        UnloadImage(imMetallicRoughness);
                    }

                    // Load metallic/roughness material properties
                    float roughness = data->materials[i].pbr_metallic_roughness.roughness_factor;
                    model.materials[j].maps[MATERIAL_MAP_ROUGHNESS].value = roughness;

                    float metallic = data->materials[i].pbr_metallic_roughness.metallic_factor;
                    model.materials[j].maps[MATERIAL_MAP_METALNESS].value = metallic;
                }

                // Load normal texture
                if (data->materials[i].normal_texture.texture)
                {
                    Image imNormal = LoadImageFromCgltfImage(data->materials[i].normal_texture.texture->image, texPath);
                    if (imNormal.data != NULL)
                    {
                        model.materials[j].maps[MATERIAL_MAP_NORMAL].texture = LoadTextureFromImage(imNormal);
                        UnloadImage(imNormal);
                    }
                }

                // Load ambient occlusion texture
                if (data->materials[i].occlusion_texture.texture)
                {
                    Image imOcclusion = LoadImageFromCgltfImage(data->materials[i].occlusion_texture.texture->image, texPath);
                    if (imOcclusion.data != NULL)
                    {
                        model.materials[j].maps[MATERIAL_MAP_OCCLUSION].texture = LoadTextureFromImage(imOcclusion);
                        UnloadImage(imOcclusion);
                    }
                }

                // Load emissive texture
                if (data->materials[i].emissive_texture.texture)
                {
                    Image imEmissive = LoadImageFromCgltfImage(data->materials[i].emissive_texture.texture->image, texPath);
                    if (imEmissive.data != NULL)
                    {
                        model.materials[j].maps[MATERIAL_MAP_EMISSION].texture = LoadTextureFromImage(imEmissive);
                        UnloadImage(imEmissive);
                    }

                    // Load emissive color factor
                    model.materials[j].maps[MATERIAL_MAP_EMISSION].color.r = (unsigned char)(data->materials[i].emissive_factor[0]*255);
                    model.materials[j].maps[MATERIAL_MAP_EMISSION].color.g = (unsigned char)(data->materials[i].emissive_factor[1]*255);
                    model.materials[j].maps[MATERIAL_MAP_EMISSION].color.b = (unsigned char)(data->materials[i].emissive_factor[2]*255);
                    model.materials[j].maps[MATERIAL_MAP_EMISSION].color.a = 255;
                }
            }

            // Other possible materials not supported by raylib pipeline:
            // has_clearcoat, has_transmission, has_volume, has_ior, has specular, has_sheen
        }

        // Visit each node in the hierarchy and process any mesh linked from it.
        // Each primitive within a glTF node becomes a Raylib Mesh.
        // The local-to-world transform of each node is used to transform the
        // points/normals/tangents of the created Mesh(es).
        // Any glTF mesh linked from more than one Node (i.e. instancing)
        // is turned into multiple Mesh's, as each Node will have its own
        // transform applied.
        // Note: the code below disregards the scenes defined in the file, all nodes are used.
        //----------------------------------------------------------------------------------------------------
        int meshIndex = 0;
        for (unsigned int i = 0; i < data->nodes_count; i++)
        {
            cgltf_node *node = &(data->nodes[i]);

            cgltf_mesh *mesh = node->mesh;
            if (!mesh)
                continue;

            cgltf_float worldTransform[16];
            cgltf_node_transform_world(node, worldTransform);

            Matrix worldMatrix = {
                worldTransform[0], worldTransform[4], worldTransform[8], worldTransform[12],
                worldTransform[1], worldTransform[5], worldTransform[9], worldTransform[13],
                worldTransform[2], worldTransform[6], worldTransform[10], worldTransform[14],
                worldTransform[3], worldTransform[7], worldTransform[11], worldTransform[15]
            };

            Matrix worldMatrixNormals = MatrixTranspose(MatrixInvert(worldMatrix));

            for (unsigned int p = 0; p < mesh->primitives_count; p++)
            {
                // NOTE: We only support primitives defined by triangles
                // Other alternatives: points, lines, line_strip, triangle_strip
                if (mesh->primitives[p].type != cgltf_primitive_type_triangles) continue;

                // NOTE: Attributes data could be provided in several data formats (8, 8u, 16u, 32...),
                // Only some formats for each attribute type are supported, read info at the top of this function!

                for (unsigned int j = 0; j < mesh->primitives[p].attributes_count; j++)
                {
                    // Check the different attributes for every primitive
                    if (mesh->primitives[p].attributes[j].type == cgltf_attribute_type_position)      // POSITION, vec3, float
                    {
                        cgltf_accessor *attribute = mesh->primitives[p].attributes[j].data;

                        // WARNING: SPECS: POSITION accessor MUST have its min and max properties defined

                        if ((attribute->type == cgltf_type_vec3) && (attribute->component_type == cgltf_component_type_r_32f))
                        {
                            // Init raylib mesh vertices to copy glTF attribute data
                            model.meshes[meshIndex].vertexCount = (int)attribute->count;
                            model.meshes[meshIndex].vertices = RL_MALLOC(attribute->count*3*sizeof(float));

                            // Load 3 components of float data type into mesh.vertices
                            LOAD_ATTRIBUTE(attribute, 3, float, model.meshes[meshIndex].vertices)

                            // Transform the vertices
                            float *vertices = model.meshes[meshIndex].vertices;
                            for (unsigned int k = 0; k < attribute->count; k++)
                            {
                                Vector3 vt = Vector3Transform((Vector3){ vertices[3*k], vertices[3*k+1], vertices[3*k+2] }, worldMatrix);
                                vertices[3*k] = vt.x;
                                vertices[3*k+1] = vt.y;
                                vertices[3*k+2] = vt.z;
                            }
                        }
                        else TRACELOG(LOG_WARNING, "MODEL: [%s] Vertices attribute data format not supported, use vec3 float", fileName);
                    }
                    else if (mesh->primitives[p].attributes[j].type == cgltf_attribute_type_normal)   // NORMAL, vec3, float
                    {
                        cgltf_accessor *attribute = mesh->primitives[p].attributes[j].data;

                        if ((attribute->type == cgltf_type_vec3) && (attribute->component_type == cgltf_component_type_r_32f))
                        {
                            // Init raylib mesh normals to copy glTF attribute data
                            model.meshes[meshIndex].normals = RL_MALLOC(attribute->count*3*sizeof(float));

                            // Load 3 components of float data type into mesh.normals
                            LOAD_ATTRIBUTE(attribute, 3, float, model.meshes[meshIndex].normals)

                            // Transform the normals
                            float *normals = model.meshes[meshIndex].normals;
                            for (unsigned int k = 0; k < attribute->count; k++)
                            {
                                Vector3 nt = Vector3Transform((Vector3){ normals[3*k], normals[3*k+1], normals[3*k+2] }, worldMatrixNormals);
                                normals[3*k] = nt.x;
                                normals[3*k+1] = nt.y;
                                normals[3*k+2] = nt.z;
                            }
                        }
                        else TRACELOG(LOG_WARNING, "MODEL: [%s] Normal attribute data format not supported, use vec3 float", fileName);
                    }
                    else if (mesh->primitives[p].attributes[j].type == cgltf_attribute_type_tangent)   // TANGENT, vec3, float
                    {
                        cgltf_accessor *attribute = mesh->primitives[p].attributes[j].data;

                        if ((attribute->type == cgltf_type_vec4) && (attribute->component_type == cgltf_component_type_r_32f))
                        {
                            // Init raylib mesh tangent to copy glTF attribute data
                            model.meshes[meshIndex].tangents = RL_MALLOC(attribute->count*4*sizeof(float));

                            // Load 4 components of float data type into mesh.tangents
                            LOAD_ATTRIBUTE(attribute, 4, float, model.meshes[meshIndex].tangents)

                            // Transform the tangents
                            float *tangents = model.meshes[meshIndex].tangents;
                            for (unsigned int k = 0; k < attribute->count; k++)
                            {
                                Vector3 tt = Vector3Transform((Vector3){ tangents[3*k], tangents[3*k+1], tangents[3*k+2] }, worldMatrix);
                                tangents[3*k] = tt.x;
                                tangents[3*k+1] = tt.y;
                                tangents[3*k+2] = tt.z;
                            }
                        }
                        else TRACELOG(LOG_WARNING, "MODEL: [%s] Tangent attribute data format not supported, use vec4 float", fileName);
                    }
                    else if (mesh->primitives[p].attributes[j].type == cgltf_attribute_type_texcoord) // TEXCOORD_n, vec2, float/u8n/u16n
                    {
                        // Support up to 2 texture coordinates attributes
                        float *texcoordPtr = NULL;

                        cgltf_accessor *attribute = mesh->primitives[p].attributes[j].data;

                        if (attribute->type == cgltf_type_vec2)
                        {
                            if (attribute->component_type == cgltf_component_type_r_32f)  // vec2, float
                            {
                                // Init raylib mesh texcoords to copy glTF attribute data
                                texcoordPtr = (float *)RL_MALLOC(attribute->count*2*sizeof(float));

                                // Load 3 components of float data type into mesh.texcoords
                                LOAD_ATTRIBUTE(attribute, 2, float, texcoordPtr)
                            }
                            else if (attribute->component_type == cgltf_component_type_r_8u) // vec2, u8n
                            {
                                // Init raylib mesh texcoords to copy glTF attribute data
                                texcoordPtr = (float *)RL_MALLOC(attribute->count*2*sizeof(float));

                                // Load data into a temp buffer to be converted to raylib data type
                                unsigned char *temp = (unsigned char *)RL_MALLOC(attribute->count*2*sizeof(unsigned char));
                                LOAD_ATTRIBUTE(attribute, 2, unsigned char, temp);

                                // Convert data to raylib texcoord data type (float)
                                for (unsigned int t = 0; t < attribute->count*2; t++) texcoordPtr[t] = (float)temp[t]/255.0f;

                                RL_FREE(temp);
                            }
                            else if (attribute->component_type == cgltf_component_type_r_16u) // vec2, u16n
                            {
                                // Init raylib mesh texcoords to copy glTF attribute data
                                texcoordPtr = (float *)RL_MALLOC(attribute->count*2*sizeof(float));

                                // Load data into a temp buffer to be converted to raylib data type
                                unsigned short *temp = (unsigned short *)RL_MALLOC(attribute->count*2*sizeof(unsigned short));
                                LOAD_ATTRIBUTE(attribute, 2, unsigned short, temp);

                                // Convert data to raylib texcoord data type (float)
                                for (unsigned int t = 0; t < attribute->count*2; t++) texcoordPtr[t] = (float)temp[t]/65535.0f;

                                RL_FREE(temp);
                            }
                            else TRACELOG(LOG_WARNING, "MODEL: [%s] Texcoords attribute data format not supported", fileName);
                        }
                        else TRACELOG(LOG_WARNING, "MODEL: [%s] Texcoords attribute data format not supported, use vec2 float", fileName);

                        int index = mesh->primitives[p].attributes[j].index;
                        if (index == 0) model.meshes[meshIndex].texcoords = texcoordPtr;
                        else if (index == 1) model.meshes[meshIndex].texcoords2 = texcoordPtr;
                        else
                        {
                            TRACELOG(LOG_WARNING, "MODEL: [%s] No more than 2 texture coordinates attributes supported", fileName);
                            if (texcoordPtr != NULL) RL_FREE(texcoordPtr);
                        }
                    }
                    else if (mesh->primitives[p].attributes[j].type == cgltf_attribute_type_color)    // COLOR_n, vec3/vec4, float/u8n/u16n
                    {
                        cgltf_accessor *attribute = mesh->primitives[p].attributes[j].data;

                        // WARNING: SPECS: All components of each COLOR_n accessor element MUST be clamped to [0.0, 1.0] range

                        if (attribute->type == cgltf_type_vec3)  // RGB
                        {
                            if (attribute->component_type == cgltf_component_type_r_8u)
                            {
                                // Init raylib mesh color to copy glTF attribute data
                                model.meshes[meshIndex].colors = RL_MALLOC(attribute->count*4*sizeof(unsigned char));

                                // Load data into a temp buffer to be converted to raylib data type
                                unsigned char *temp = RL_MALLOC(attribute->count*3*sizeof(unsigned char));
                                LOAD_ATTRIBUTE(attribute, 3, unsigned char, temp);

                                // Convert data to raylib color data type (4 bytes)
                                for (unsigned int c = 0, k = 0; c < (attribute->count*4 - 3); c += 4, k += 3)
                                {
                                    model.meshes[meshIndex].colors[c] = temp[k];
                                    model.meshes[meshIndex].colors[c + 1] = temp[k + 1];
                                    model.meshes[meshIndex].colors[c + 2] = temp[k + 2];
                                    model.meshes[meshIndex].colors[c + 3] = 255;
                                }

                                RL_FREE(temp);
                            }
                            else if (attribute->component_type == cgltf_component_type_r_16u)
                            {
                                // Init raylib mesh color to copy glTF attribute data
                                model.meshes[meshIndex].colors = RL_MALLOC(attribute->count*4*sizeof(unsigned char));

                                // Load data into a temp buffer to be converted to raylib data type
                                unsigned short *temp = RL_MALLOC(attribute->count*3*sizeof(unsigned short));
                                LOAD_ATTRIBUTE(attribute, 3, unsigned short, temp);

                                // Convert data to raylib color data type (4 bytes)
                                for (unsigned int c = 0, k = 0; c < (attribute->count*4 - 3); c += 4, k += 3)
                                {
                                    model.meshes[meshIndex].colors[c] = (unsigned char)(((float)temp[k]/65535.0f)*255.0f);
                                    model.meshes[meshIndex].colors[c + 1] = (unsigned char)(((float)temp[k + 1]/65535.0f)*255.0f);
                                    model.meshes[meshIndex].colors[c + 2] = (unsigned char)(((float)temp[k + 2]/65535.0f)*255.0f);
                                    model.meshes[meshIndex].colors[c + 3] = 255;
                                }

                                RL_FREE(temp);
                            }
                            else if (attribute->component_type == cgltf_component_type_r_32f)
                            {
                                // Init raylib mesh color to copy glTF attribute data
                                model.meshes[meshIndex].colors = RL_MALLOC(attribute->count*4*sizeof(unsigned char));

                                // Load data into a temp buffer to be converted to raylib data type
                                float *temp = RL_MALLOC(attribute->count*3*sizeof(float));
                                LOAD_ATTRIBUTE(attribute, 3, float, temp);

                                // Convert data to raylib color data type (4 bytes)
                                for (unsigned int c = 0, k = 0; c < (attribute->count*4 - 3); c += 4, k += 3)
                                {
                                    model.meshes[meshIndex].colors[c] = (unsigned char)(temp[k]*255.0f);
                                    model.meshes[meshIndex].colors[c + 1] = (unsigned char)(temp[k + 1]*255.0f);
                                    model.meshes[meshIndex].colors[c + 2] = (unsigned char)(temp[k + 2]*255.0f);
                                    model.meshes[meshIndex].colors[c + 3] = 255;
                                }

                                RL_FREE(temp);
                            }
                            else TRACELOG(LOG_WARNING, "MODEL: [%s] Color attribute data format not supported", fileName);
                        }
                        else if (attribute->type == cgltf_type_vec4) // RGBA
                        {
                            if (attribute->component_type == cgltf_component_type_r_8u)
                            {
                                // Init raylib mesh color to copy glTF attribute data
                                model.meshes[meshIndex].colors = RL_MALLOC(attribute->count*4*sizeof(unsigned char));

                                // Load 4 components of unsigned char data type into mesh.colors
                                LOAD_ATTRIBUTE(attribute, 4, unsigned char, model.meshes[meshIndex].colors)
                            }
                            else if (attribute->component_type == cgltf_component_type_r_16u)
                            {
                                // Init raylib mesh color to copy glTF attribute data
                                model.meshes[meshIndex].colors = RL_MALLOC(attribute->count*4*sizeof(unsigned char));

                                // Load data into a temp buffer to be converted to raylib data type
                                unsigned short *temp = RL_MALLOC(attribute->count*4*sizeof(unsigned short));
                                LOAD_ATTRIBUTE(attribute, 4, unsigned short, temp);

                                // Convert data to raylib color data type (4 bytes)
                                for (unsigned int c = 0; c < attribute->count*4; c++) model.meshes[meshIndex].colors[c] = (unsigned char)(((float)temp[c]/65535.0f)*255.0f);

                                RL_FREE(temp);
                            }
                            else if (attribute->component_type == cgltf_component_type_r_32f)
                            {
                                // Init raylib mesh color to copy glTF attribute data
                                model.meshes[meshIndex].colors = RL_MALLOC(attribute->count*4*sizeof(unsigned char));

                                // Load data into a temp buffer to be converted to raylib data type
                                float *temp = RL_MALLOC(attribute->count*4*sizeof(float));
                                LOAD_ATTRIBUTE(attribute, 4, float, temp);

                                // Convert data to raylib color data type (4 bytes), we expect the color data normalized
                                for (unsigned int c = 0; c < attribute->count*4; c++) model.meshes[meshIndex].colors[c] = (unsigned char)(temp[c]*255.0f);

                                RL_FREE(temp);
                            }
                            else TRACELOG(LOG_WARNING, "MODEL: [%s] Color attribute data format not supported", fileName);
                        }
                        else TRACELOG(LOG_WARNING, "MODEL: [%s] Color attribute data format not supported", fileName);
                    }

                    // NOTE: Attributes related to animations are processed separately
                }

                // Load primitive indices data (if provided)
                if (mesh->primitives[p].indices != NULL)
                {
                    cgltf_accessor *attribute = mesh->primitives[p].indices;

                    model.meshes[meshIndex].triangleCount = (int)attribute->count/3;

                    if (attribute->component_type == cgltf_component_type_r_16u)
                    {
                        // Init raylib mesh indices to copy glTF attribute data
                        model.meshes[meshIndex].indices = RL_MALLOC(attribute->count*sizeof(unsigned short));

                        // Load unsigned short data type into mesh.indices
                        LOAD_ATTRIBUTE(attribute, 1, unsigned short, model.meshes[meshIndex].indices)
                    }
                    else if (attribute->component_type == cgltf_component_type_r_8u)
                    {
                        // Init raylib mesh indices to copy glTF attribute data
                        model.meshes[meshIndex].indices = RL_MALLOC(attribute->count * sizeof(unsigned short));
                        LOAD_ATTRIBUTE_CAST(attribute, 1, unsigned char, model.meshes[meshIndex].indices, unsigned short)

                    }
                    else if (attribute->component_type == cgltf_component_type_r_32u)
                    {
                        // Init raylib mesh indices to copy glTF attribute data
                        model.meshes[meshIndex].indices = RL_MALLOC(attribute->count*sizeof(unsigned short));
                        LOAD_ATTRIBUTE_CAST(attribute, 1, unsigned int, model.meshes[meshIndex].indices, unsigned short);

                        TRACELOG(LOG_WARNING, "MODEL: [%s] Indices data converted from u32 to u16, possible loss of data", fileName);
                    }
                    else
                    {
                        TRACELOG(LOG_WARNING, "MODEL: [%s] Indices data format not supported, use u16", fileName);
                    }
                }
                else model.meshes[meshIndex].triangleCount = model.meshes[meshIndex].vertexCount/3;    // Unindexed mesh

                // Assign to the primitive mesh the corresponding material index
                // NOTE: If no material defined, mesh uses the already assigned default material (index: 0)
                for (unsigned int m = 0; m < data->materials_count; m++)
                {
                    // The primitive actually keeps the pointer to the corresponding material,
                    // raylib instead assigns to the mesh the by its index, as loaded in model.materials array
                    // To get the index, we check if material pointers match, and we assign the corresponding index,
                    // skipping index 0, the default material
                    if (&data->materials[m] == mesh->primitives[p].material)
                    {
                        model.meshMaterial[meshIndex] = m + 1;
                        break;
                    }
                }

                meshIndex++;       // Move to next mesh
            }
        }

        // Load glTF meshes animation data
        // REF: https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html#skins
        // REF: https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html#skinned-mesh-attributes
        //
        // LIMITATIONS:
        //  - Only supports 1 armature per file, and skips loading it if there are multiple armatures
        //  - Only supports linear interpolation (default method in Blender when checked "Always Sample Animations" when exporting a GLTF file)
        //  - Only supports translation/rotation/scale animation channel.path, weights not considered (i.e. morph targets)
        //----------------------------------------------------------------------------------------------------
        if (data->skins_count > 0)
        {
            cgltf_skin skin = data->skins[0];
            model.bones = LoadBoneInfoGLTF(skin, &model.boneCount);
            model.bindPose = RL_MALLOC(model.boneCount*sizeof(Transform));

            for (int i = 0; i < model.boneCount; i++)
            {
                cgltf_node* node = skin.joints[i];
                cgltf_float worldTransform[16];
                cgltf_node_transform_world(node, worldTransform);
                Matrix worldMatrix = {
                    worldTransform[0], worldTransform[4], worldTransform[8], worldTransform[12],
                    worldTransform[1], worldTransform[5], worldTransform[9], worldTransform[13],
                    worldTransform[2], worldTransform[6], worldTransform[10], worldTransform[14],
                    worldTransform[3], worldTransform[7], worldTransform[11], worldTransform[15]
                };
                MatrixDecompose(worldMatrix, &(model.bindPose[i].translation), &(model.bindPose[i].rotation), &(model.bindPose[i].scale));
            }
        }
        if (data->skins_count > 1)
        {
            TRACELOG(LOG_WARNING, "MODEL: [%s] can only load one skin (armature) per model, but gltf skins_count == %i", fileName, data->skins_count);
        }

        meshIndex = 0;
        for (unsigned int i = 0; i < data->nodes_count; i++)
        {
            cgltf_node *node = &(data->nodes[i]);

            cgltf_mesh *mesh = node->mesh;
            if (!mesh)
                continue;

            for (unsigned int p = 0; p < mesh->primitives_count; p++)
            {
                // NOTE: We only support primitives defined by triangles
                if (mesh->primitives[p].type != cgltf_primitive_type_triangles) continue;

                for (unsigned int j = 0; j < mesh->primitives[p].attributes_count; j++)
                {
                    // NOTE: JOINTS_1 + WEIGHT_1 will be used for +4 joints influencing a vertex -> Not supported by raylib

                    if (mesh->primitives[p].attributes[j].type == cgltf_attribute_type_joints) // JOINTS_n (vec4: 4 bones max per vertex / u8, u16)
                    {
                        cgltf_accessor *attribute = mesh->primitives[p].attributes[j].data;

                        // NOTE: JOINTS_n can only be vec4 and u8/u16
                        // SPECS: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#meshes-overview

                        // WARNING: raylib only supports model.meshes[].boneIds as u8 (unsigned char),
                        // if data is provided in any other format, it is converted to supported format but
                        // it could imply data loss (a warning message is issued in that case)

                        if (attribute->type == cgltf_type_vec4)
                        {
                            if (attribute->component_type == cgltf_component_type_r_8u)
                            {
                                // Init raylib mesh boneIds to copy glTF attribute data
                                model.meshes[meshIndex].boneIds = RL_CALLOC(model.meshes[meshIndex].vertexCount*4, sizeof(unsigned char));

                                // Load attribute: vec4, u8 (unsigned char)
                                LOAD_ATTRIBUTE(attribute, 4, unsigned char, model.meshes[meshIndex].boneIds)
                            }
                            else if (attribute->component_type == cgltf_component_type_r_16u)
                            {
                                // Init raylib mesh boneIds to copy glTF attribute data
                                model.meshes[meshIndex].boneIds = RL_CALLOC(model.meshes[meshIndex].vertexCount*4, sizeof(unsigned char));

                                // Load data into a temp buffer to be converted to raylib data type
                                unsigned short *temp = RL_CALLOC(model.meshes[meshIndex].vertexCount*4, sizeof(unsigned short));
                                LOAD_ATTRIBUTE(attribute, 4, unsigned short, temp);

                                // Convert data to raylib color data type (4 bytes)
                                bool boneIdOverflowWarning = false;
                                for (int b = 0; b < model.meshes[meshIndex].vertexCount*4; b++)
                                {
                                    if ((temp[b] > 255) && !boneIdOverflowWarning)
                                    {
                                        TRACELOG(LOG_WARNING, "MODEL: [%s] Joint attribute data format (u16) overflow", fileName);
                                        boneIdOverflowWarning = true;
                                    }

                                    // Despite the possible overflow, we convert data to unsigned char
                                    model.meshes[meshIndex].boneIds[b] = (unsigned char)temp[b];
                                }

                                RL_FREE(temp);
                            }
                            else TRACELOG(LOG_WARNING, "MODEL: [%s] Joint attribute data format not supported", fileName);
                        }
                        else TRACELOG(LOG_WARNING, "MODEL: [%s] Joint attribute data format not supported", fileName);
                    }
                    else if (mesh->primitives[p].attributes[j].type == cgltf_attribute_type_weights)  // WEIGHTS_n (vec4, u8n/u16n/f32)
                    {
                        cgltf_accessor *attribute = mesh->primitives[p].attributes[j].data;

                        if (attribute->type == cgltf_type_vec4)
                        {
                            // TODO: Support component types: u8, u16?
                            if (attribute->component_type == cgltf_component_type_r_8u)
                            {
                                // Init raylib mesh bone weight to copy glTF attribute data
                                model.meshes[meshIndex].boneWeights = RL_CALLOC(model.meshes[meshIndex].vertexCount*4, sizeof(float));

                                // Load data into a temp buffer to be converted to raylib data type
                                unsigned char *temp = RL_MALLOC(attribute->count*4*sizeof(unsigned char));
                                LOAD_ATTRIBUTE(attribute, 4, unsigned char, temp);

                                // Convert data to raylib bone weight data type (4 bytes)
                                for (unsigned int b = 0; b < attribute->count*4; b++) model.meshes[meshIndex].boneWeights[b] = (float)temp[b]/255.0f;

                                RL_FREE(temp);
                            }
                            else if (attribute->component_type == cgltf_component_type_r_16u)
                            {
                                // Init raylib mesh bone weight to copy glTF attribute data
                                model.meshes[meshIndex].boneWeights = RL_CALLOC(model.meshes[meshIndex].vertexCount*4, sizeof(float));

                                // Load data into a temp buffer to be converted to raylib data type
                                unsigned short *temp = RL_MALLOC(attribute->count*4*sizeof(unsigned short));
                                LOAD_ATTRIBUTE(attribute, 4, unsigned short, temp);

                                // Convert data to raylib bone weight data type
                                for (unsigned int b = 0; b < attribute->count*4; b++) model.meshes[meshIndex].boneWeights[b] = (float)temp[b]/65535.0f;

                                RL_FREE(temp);
                            }
                            else if (attribute->component_type == cgltf_component_type_r_32f)
                            {
                                // Init raylib mesh bone weight to copy glTF attribute data
                                model.meshes[meshIndex].boneWeights = RL_CALLOC(model.meshes[meshIndex].vertexCount*4, sizeof(float));

                                // Load 4 components of float data type into mesh.boneWeights
                                // for cgltf_attribute_type_weights we have:
                                //   - data.meshes[0] (256 vertices)
                                //   - 256 values, provided as cgltf_type_vec4 of float (4 byte per joint, stride 16)
                                LOAD_ATTRIBUTE(attribute, 4, float, model.meshes[meshIndex].boneWeights)
                            }
                            else TRACELOG(LOG_WARNING, "MODEL: [%s] Joint weight attribute data format not supported, use vec4 float", fileName);
                        }
                        else TRACELOG(LOG_WARNING, "MODEL: [%s] Joint weight attribute data format not supported, use vec4 float", fileName);
                    }
                }

                // Animated vertex data
                model.meshes[meshIndex].animVertices = RL_CALLOC(model.meshes[meshIndex].vertexCount*3, sizeof(float));
                memcpy(model.meshes[meshIndex].animVertices, model.meshes[meshIndex].vertices, model.meshes[meshIndex].vertexCount*3*sizeof(float));
                model.meshes[meshIndex].animNormals = RL_CALLOC(model.meshes[meshIndex].vertexCount*3, sizeof(float));
                if (model.meshes[meshIndex].normals != NULL)
                {
                    memcpy(model.meshes[meshIndex].animNormals, model.meshes[meshIndex].normals, model.meshes[meshIndex].vertexCount*3*sizeof(float));
                }

                // Bone Transform Matrices
                model.meshes[meshIndex].boneCount = model.boneCount;
                model.meshes[meshIndex].boneMatrices = RL_CALLOC(model.meshes[meshIndex].boneCount, sizeof(Matrix));

                for (int j = 0; j < model.meshes[meshIndex].boneCount; j++)
                {
                    model.meshes[meshIndex].boneMatrices[j] = MatrixIdentity();
                }

                meshIndex++;       // Move to next mesh
            }

        }

        // Free all cgltf loaded data
        cgltf_free(data);
    }
    else TRACELOG(LOG_WARNING, "MODEL: [%s] Failed to load glTF data", fileName);

    // WARNING: cgltf requires the file pointer available while reading data
    UnloadFileData(fileData);

    return model;
}

// Get interpolated pose for bone sampler at a specific time. Returns true on success
static bool GetPoseAtTimeGLTF(cgltf_interpolation_type interpolationType, cgltf_accessor *input, cgltf_accessor *output, float time, void *data)
{
    if (interpolationType >= cgltf_interpolation_type_max_enum) return false;

    // Input and output should have the same count
    float tstart = 0.0f;
    float tend = 0.0f;
    int keyframe = 0;       // Defaults to first pose

    for (int i = 0; i < (int)input->count - 1; i++)
    {
        cgltf_bool r1 = cgltf_accessor_read_float(input, i, &tstart, 1);
        if (!r1) return false;

        cgltf_bool r2 = cgltf_accessor_read_float(input, i + 1, &tend, 1);
        if (!r2) return false;

        if ((tstart <= time) && (time < tend))
        {
            keyframe = i;
            break;
        }
    }

    // Constant animation, no need to interpolate
    if (FloatEquals(tend, tstart)) return true;

    float duration = fmaxf((tend - tstart), EPSILON);
    float t = (time - tstart)/duration;
    t = (t < 0.0f)? 0.0f : t;
    t = (t > 1.0f)? 1.0f : t;

    if (output->component_type != cgltf_component_type_r_32f) return false;

    if (output->type == cgltf_type_vec3)
    {
        switch (interpolationType)
        {
            case cgltf_interpolation_type_step:
            {
                float tmp[3] = { 0.0f };
                cgltf_accessor_read_float(output, keyframe, tmp, 3);
                Vector3 v1 = {tmp[0], tmp[1], tmp[2]};
                Vector3 *r = data;

                *r = v1;
            } break;
            case cgltf_interpolation_type_linear:
            {
                float tmp[3] = { 0.0f };
                cgltf_accessor_read_float(output, keyframe, tmp, 3);
                Vector3 v1 = {tmp[0], tmp[1], tmp[2]};
                cgltf_accessor_read_float(output, keyframe+1, tmp, 3);
                Vector3 v2 = {tmp[0], tmp[1], tmp[2]};
                Vector3 *r = data;

                *r = Vector3Lerp(v1, v2, t);
            } break;
            case cgltf_interpolation_type_cubic_spline:
            {
                float tmp[3] = { 0.0f };
                cgltf_accessor_read_float(output, 3*keyframe+1, tmp, 3);
                Vector3 v1 = {tmp[0], tmp[1], tmp[2]};
                cgltf_accessor_read_float(output, 3*keyframe+2, tmp, 3);
                Vector3 tangent1 = {tmp[0], tmp[1], tmp[2]};
                cgltf_accessor_read_float(output, 3*(keyframe+1)+1, tmp, 3);
                Vector3 v2 = {tmp[0], tmp[1], tmp[2]};
                cgltf_accessor_read_float(output, 3*(keyframe+1), tmp, 3);
                Vector3 tangent2 = {tmp[0], tmp[1], tmp[2]};
                Vector3 *r = data;

                *r = Vector3CubicHermite(v1, tangent1, v2, tangent2, t);
            } break;
            default: break;
        }
    }
    else if (output->type == cgltf_type_vec4)
    {
        // Only v4 is for rotations, so we know it's a quaternion
        switch (interpolationType)
        {
            case cgltf_interpolation_type_step:
            {
                float tmp[4] = { 0.0f };
                cgltf_accessor_read_float(output, keyframe, tmp, 4);
                Vector4 v1 = {tmp[0], tmp[1], tmp[2], tmp[3]};
                Vector4 *r = data;

                *r = v1;
            } break;
            case cgltf_interpolation_type_linear:
            {
                float tmp[4] = { 0.0f };
                cgltf_accessor_read_float(output, keyframe, tmp, 4);
                Vector4 v1 = {tmp[0], tmp[1], tmp[2], tmp[3]};
                cgltf_accessor_read_float(output, keyframe+1, tmp, 4);
                Vector4 v2 = {tmp[0], tmp[1], tmp[2], tmp[3]};
                Vector4 *r = data;

                *r = QuaternionSlerp(v1, v2, t);
            } break;
            case cgltf_interpolation_type_cubic_spline:
            {
                float tmp[4] = { 0.0f };
                cgltf_accessor_read_float(output, 3*keyframe+1, tmp, 4);
                Vector4 v1 = {tmp[0], tmp[1], tmp[2], tmp[3]};
                cgltf_accessor_read_float(output, 3*keyframe+2, tmp, 4);
                Vector4 outTangent1 = {tmp[0], tmp[1], tmp[2], 0.0f};
                cgltf_accessor_read_float(output, 3*(keyframe+1)+1, tmp, 4);
                Vector4 v2 = {tmp[0], tmp[1], tmp[2], tmp[3]};
                cgltf_accessor_read_float(output, 3*(keyframe+1), tmp, 4);
                Vector4 inTangent2 = {tmp[0], tmp[1], tmp[2], 0.0f};
                Vector4 *r = data;

                v1 = QuaternionNormalize(v1);
                v2 = QuaternionNormalize(v2);

                if (Vector4DotProduct(v1, v2) < 0.0f)
                {
                    v2 = Vector4Negate(v2);
                }

                outTangent1 = Vector4Scale(outTangent1, duration);
                inTangent2 = Vector4Scale(inTangent2, duration);

                *r = QuaternionCubicHermiteSpline(v1, outTangent1, v2, inTangent2, t);
            } break;
            default: break;
        }
    }

    return true;
}

#define GLTF_ANIMDELAY 17    // Animation frames delay, (~1000 ms/60 FPS = 16.666666* ms)

static ModelAnimation *LoadModelAnimationsGLTF(const char *fileName, int *animCount)
{
    // glTF file loading
    int dataSize = 0;
    unsigned char *fileData = LoadFileData(fileName, &dataSize);

    ModelAnimation *animations = NULL;

    // glTF data loading
    cgltf_options options = { 0 };
    options.file.read = LoadFileGLTFCallback;
    options.file.release = ReleaseFileGLTFCallback;
    cgltf_data *data = NULL;
    cgltf_result result = cgltf_parse(&options, fileData, dataSize, &data);

    if (result != cgltf_result_success)
    {
        TRACELOG(LOG_WARNING, "MODEL: [%s] Failed to load glTF data", fileName);
        *animCount = 0;
        return NULL;
    }

    result = cgltf_load_buffers(&options, data, fileName);
    if (result != cgltf_result_success) TRACELOG(LOG_INFO, "MODEL: [%s] Failed to load animation buffers", fileName);

    if (result == cgltf_result_success)
    {
        if (data->skins_count > 0)
        {
            cgltf_skin skin = data->skins[0];
            *animCount = (int)data->animations_count;
            animations = RL_MALLOC(data->animations_count*sizeof(ModelAnimation));

            for (unsigned int i = 0; i < data->animations_count; i++)
            {
                animations[i].bones = LoadBoneInfoGLTF(skin, &animations[i].boneCount);

                cgltf_animation animData = data->animations[i];

                struct Channels {
                    cgltf_animation_channel *translate;
                    cgltf_animation_channel *rotate;
                    cgltf_animation_channel *scale;
                    cgltf_interpolation_type interpolationType;
                };

                struct Channels *boneChannels = RL_CALLOC(animations[i].boneCount, sizeof(struct Channels));
                float animDuration = 0.0f;

                for (unsigned int j = 0; j < animData.channels_count; j++)
                {
                    cgltf_animation_channel channel = animData.channels[j];
                    int boneIndex = -1;

                    for (unsigned int k = 0; k < skin.joints_count; k++)
                    {
                        if (animData.channels[j].target_node == skin.joints[k])
                        {
                            boneIndex = k;
                            break;
                        }
                    }

                    if (boneIndex == -1)
                    {
                        // Animation channel for a node not in the armature
                        continue;
                    }

                    boneChannels[boneIndex].interpolationType = animData.channels[j].sampler->interpolation;

                    if (animData.channels[j].sampler->interpolation != cgltf_interpolation_type_max_enum)
                    {
                        if (channel.target_path == cgltf_animation_path_type_translation)
                        {
                            boneChannels[boneIndex].translate = &animData.channels[j];
                        }
                        else if (channel.target_path == cgltf_animation_path_type_rotation)
                        {
                            boneChannels[boneIndex].rotate = &animData.channels[j];
                        }
                        else if (channel.target_path == cgltf_animation_path_type_scale)
                        {
                            boneChannels[boneIndex].scale = &animData.channels[j];
                        }
                        else
                        {
                            TRACELOG(LOG_WARNING, "MODEL: [%s] Unsupported target_path on channel %d's sampler for animation %d. Skipping.", fileName, j, i);
                        }
                    }
                    else TRACELOG(LOG_WARNING, "MODEL: [%s] Invalid interpolation curve encountered for GLTF animation.", fileName);

                    float t = 0.0f;
                    cgltf_bool r = cgltf_accessor_read_float(channel.sampler->input, channel.sampler->input->count - 1, &t, 1);

                    if (!r)
                    {
                        TRACELOG(LOG_WARNING, "MODEL: [%s] Failed to load input time", fileName);
                        continue;
                    }

                    animDuration = (t > animDuration)? t : animDuration;
                }

                if (animData.name != NULL)
                {
                    strncpy(animations[i].name, animData.name, sizeof(animations[i].name));
                    animations[i].name[sizeof(animations[i].name) - 1] = '\0';
                }

                animations[i].frameCount = (int)(animDuration*1000.0f/GLTF_ANIMDELAY) + 1;
                animations[i].framePoses = RL_MALLOC(animations[i].frameCount*sizeof(Transform *));

                for (int j = 0; j < animations[i].frameCount; j++)
                {
                    animations[i].framePoses[j] = RL_MALLOC(animations[i].boneCount*sizeof(Transform));
                    float time = ((float) j*GLTF_ANIMDELAY)/1000.0f;

                    for (int k = 0; k < animations[i].boneCount; k++)
                    {
                        Vector3 translation = {skin.joints[k]->translation[0], skin.joints[k]->translation[1], skin.joints[k]->translation[2]};
                        Quaternion rotation = {skin.joints[k]->rotation[0], skin.joints[k]->rotation[1], skin.joints[k]->rotation[2], skin.joints[k]->rotation[3]};
                        Vector3 scale = {skin.joints[k]->scale[0], skin.joints[k]->scale[1], skin.joints[k]->scale[2]};

                        if (boneChannels[k].translate)
                        {
                            if (!GetPoseAtTimeGLTF(boneChannels[k].interpolationType, boneChannels[k].translate->sampler->input, boneChannels[k].translate->sampler->output, time, &translation))
                            {
                                TRACELOG(LOG_INFO, "MODEL: [%s] Failed to load translate pose data for bone %s", fileName, animations[i].bones[k].name);
                            }
                        }

                        if (boneChannels[k].rotate)
                        {
                            if (!GetPoseAtTimeGLTF(boneChannels[k].interpolationType, boneChannels[k].rotate->sampler->input, boneChannels[k].rotate->sampler->output, time, &rotation))
                            {
                                TRACELOG(LOG_INFO, "MODEL: [%s] Failed to load rotate pose data for bone %s", fileName, animations[i].bones[k].name);
                            }
                        }

                        if (boneChannels[k].scale)
                        {
                            if (!GetPoseAtTimeGLTF(boneChannels[k].interpolationType, boneChannels[k].scale->sampler->input, boneChannels[k].scale->sampler->output, time, &scale))
                            {
                                TRACELOG(LOG_INFO, "MODEL: [%s] Failed to load scale pose data for bone %s", fileName, animations[i].bones[k].name);
                            }
                        }

                        animations[i].framePoses[j][k] = (Transform){
                            .translation = translation,
                            .rotation = rotation,
                            .scale = scale
                        };
                    }

                    BuildPoseFromParentJoints(animations[i].bones, animations[i].boneCount, animations[i].framePoses[j]);
                }

                TRACELOG(LOG_INFO, "MODEL: [%s] Loaded animation: %s (%d frames, %fs)", fileName, (animData.name != NULL)? animData.name : "NULL", animations[i].frameCount, animDuration);
                RL_FREE(boneChannels);
            }
        }

        if (data->skins_count > 1)
        {
            TRACELOG(LOG_WARNING, "MODEL: [%s] expected exactly one skin to load animation data from, but found %i", fileName, data->skins_count);
        }

        cgltf_free(data);
    }
    UnloadFileData(fileData);
    return animations;
}

