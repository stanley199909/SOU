#pragma once
#include <assimp/scene.h>
#include <cstring>
#include <vector>

namespace ModelImport {
inline bool IsCollision(const char* name) { return name && std::strncmp(name,"UCX_",4)==0; }

// FBX stores UCX on nodes, not necessarily on aiMesh names. Strip references
// BEFORE PreTransformVertices merges meshes and loses their node identity.
inline void StripCollisionNodes(aiNode* node, const aiScene* scene, bool collision=false) {
    collision = collision || IsCollision(node->mName.C_Str());
    unsigned count=0;
    for(unsigned i=0;i<node->mNumMeshes;++i) {
        const unsigned mesh=node->mMeshes[i];
        if(!collision && !IsCollision(scene->mMeshes[mesh]->mName.C_Str()))
            node->mMeshes[count++]=mesh;
    }
    node->mNumMeshes=count;
    for(unsigned i=0;i<node->mNumChildren;++i) StripCollisionNodes(node->mChildren[i],scene,collision);
}
inline void MarkUsed(aiNode* node,std::vector<bool>& used) {
    for(unsigned i=0;i<node->mNumMeshes;++i) used[node->mMeshes[i]]=true;
    for(unsigned i=0;i<node->mNumChildren;++i) MarkUsed(node->mChildren[i],used);
}
inline void Remap(aiNode* node,const std::vector<unsigned>& indices) {
    for(unsigned i=0;i<node->mNumMeshes;++i) node->mMeshes[i]=indices[node->mMeshes[i]];
    for(unsigned i=0;i<node->mNumChildren;++i) Remap(node->mChildren[i],indices);
}
inline void RemoveCollision(aiScene* scene) {
    if(!scene || !scene->mRootNode) return;
    StripCollisionNodes(scene->mRootNode,scene);
    std::vector<bool> used(scene->mNumMeshes,false);
    std::vector<unsigned> indices(scene->mNumMeshes);
    MarkUsed(scene->mRootNode,used);
    unsigned count=0;
    for(unsigned i=0;i<scene->mNumMeshes;++i) {
        if(used[i]) { indices[i]=count; scene->mMeshes[count++]=scene->mMeshes[i]; }
        else delete scene->mMeshes[i];
    }
    scene->mNumMeshes=count;
    Remap(scene->mRootNode,indices);
}
}
