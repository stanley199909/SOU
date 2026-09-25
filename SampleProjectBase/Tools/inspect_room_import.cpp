#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/config.h>
#include <cstdio>
#include "ModelImport.h"
int main() {
 for (bool keep : {false,true}) {
  Assimp::Importer importer;
  importer.SetPropertyBool(AI_CONFIG_PP_PTV_KEEP_HIERARCHY,keep);
  auto* s=importer.ReadFile("Assets/MM_Blacksmith_Pack/Forges/SM_BS_Forge_2_.fbx", aiProcess_Triangulate|aiProcess_JoinIdenticalVertices|aiProcess_FlipUVs);
  if(!s)return 1;
  if(keep) ModelImport::RemoveCollision(const_cast<aiScene*>(s));
  s=importer.ApplyPostProcessing(aiProcess_PreTransformVertices);
  if(!s)return 1;
  printf("KEEP %d meshes %u\n",keep,s->mNumMeshes);
  for(unsigned i=0;i<s->mNumMeshes;++i)printf("%s faces %u mat %u\n",s->mMeshes[i]->mName.C_Str(),s->mMeshes[i]->mNumFaces,s->mMeshes[i]->mMaterialIndex);
  if(keep) for(unsigned i=0;i<s->mNumMeshes;++i) {
   auto* mesh=s->mMeshes[i];aiVector3D lo(1e8f),hi(-1e8f);aiString name;s->mMaterials[mesh->mMaterialIndex]->Get(AI_MATKEY_NAME,name);
   for(unsigned v=0;v<mesh->mNumVertices;++v)for(int axis=0;axis<3;++axis) {
    lo[axis]=std::min(lo[axis],mesh->mVertices[v][axis]);hi[axis]=std::max(hi[axis],mesh->mVertices[v][axis]);
   }
   printf("MAT %s bounds %f %f %f -- %f %f %f\n",name.C_Str(),lo.x,lo.y,lo.z,hi.x,hi.y,hi.z);
  }
 }
}
