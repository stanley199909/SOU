#pragma once
#include <assimp/scene.h>
#include <cstring>
#include <vector>

// Import-time scene editing, done BEFORE PreTransformVertices merges every node
// into one static mesh (after that the node names are gone).
namespace ModelImport
{
    inline bool IsCollision(const char* name) { return name && std::strncmp(name, "UCX_", 4) == 0; }

    // FBX stores UCX on nodes, not necessarily on aiMesh names. Strip references
    // BEFORE PreTransformVertices merges meshes and loses their node identity.
    inline void StripCollisionNodes(aiNode* node, const aiScene* scene, bool collision = false)
    {
        collision = collision || IsCollision(node->mName.C_Str());
        unsigned count = 0;
        for (unsigned i = 0; i < node->mNumMeshes; ++i)
        {
            const unsigned mesh = node->mMeshes[i];
            if (!collision && !IsCollision(scene->mMeshes[mesh]->mName.C_Str()))
                node->mMeshes[count++] = mesh;
        }
        node->mNumMeshes = count;
        for (unsigned i = 0; i < node->mNumChildren; ++i) StripCollisionNodes(node->mChildren[i], scene, collision);
    }

    // Keep only the meshes inside the named part's subtree (keepOnly = true), or
    // everything except that subtree (keepOnly = false). Prefix match, because the
    // FBX importer may add pivot helper nodes like "Stone_low_$AssimpFbx$_Translation".
    inline void StripByNode(aiNode* node, const char* partName, bool keepOnly, bool inside = false)
    {
        inside = inside || std::strncmp(node->mName.C_Str(), partName, std::strlen(partName)) == 0;
        if (inside != keepOnly) node->mNumMeshes = 0;   // drop this node's mesh references
        for (unsigned i = 0; i < node->mNumChildren; ++i) StripByNode(node->mChildren[i], partName, keepOnly, inside);
    }

    inline void MarkUsed(aiNode* node, std::vector<bool>& used)
    {
        for (unsigned i = 0; i < node->mNumMeshes; ++i) used[node->mMeshes[i]] = true;
        for (unsigned i = 0; i < node->mNumChildren; ++i) MarkUsed(node->mChildren[i], used);
    }
    inline void Remap(aiNode* node, const std::vector<unsigned>& indices)
    {
        for (unsigned i = 0; i < node->mNumMeshes; ++i) node->mMeshes[i] = indices[node->mMeshes[i]];
        for (unsigned i = 0; i < node->mNumChildren; ++i) Remap(node->mChildren[i], indices);
    }

    // Delete meshes no node references any more, and renumber the rest.
    inline void DeleteUnusedMeshes(aiScene* scene)
    {
        std::vector<bool> used(scene->mNumMeshes, false);
        std::vector<unsigned> indices(scene->mNumMeshes);
        MarkUsed(scene->mRootNode, used);
        unsigned count = 0;
        for (unsigned i = 0; i < scene->mNumMeshes; ++i)
        {
            if (used[i]) { indices[i] = count; scene->mMeshes[count++] = scene->mMeshes[i]; }
            else delete scene->mMeshes[i];
        }
        scene->mNumMeshes = count;
        Remap(scene->mRootNode, indices);
    }

    inline void RemoveCollision(aiScene* scene)
    {
        if (!scene || !scene->mRootNode) return;
        StripCollisionNodes(scene->mRootNode, scene);
        DeleteUnusedMeshes(scene);
    }

    inline void FilterByNode(aiScene* scene, const char* partName, bool keepOnly)
    {
        if (!scene || !scene->mRootNode || !partName || !partName[0]) return;
        StripByNode(scene->mRootNode, partName, keepOnly);
        DeleteUnusedMeshes(scene);
    }
}
