#ifndef __MODEL_H__
#define __MODEL_H__

#include <DirectXMath.h>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include "Shader.h"
#include "MeshBuffer.h"
#include "Texture.h"

#include "assimp/Importer.hpp"
#include "assimp/cimport.h"
#include "assimp/scene.h"
#include "assimp/matrix4x4.h"

#include "DirectXTex/SimpleMath.h"

using namespace DirectX;
using namespace DirectX::SimpleMath;

class Model
{

public:
	struct Vertex
	{
		DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT3 normal;
		DirectX::XMFLOAT2 uv;
		int		boneIndex[4];
		float	boneWeight[4];
		int		boneCount = 0;
	};
	struct Material
	{
		DirectX::XMFLOAT4 diffuse;
		DirectX::XMFLOAT4 ambient;
		DirectX::XMFLOAT4 specular;
		std::shared_ptr<Texture> texture;
	};
	using Materials = std::vector<Material>;
	struct Mesh
	{
		std::shared_ptr<MeshBuffer> mesh;
		unsigned int materialID;
	};
	using Meshes = std::vector<Mesh>;

	struct RemakeInfo
	{
		UINT vtxNum;
		void* dest;
		const void* source;
		UINT idxNum;
		const void* idx;
	};

	// ウェイト情報　20231225 追加
	struct WEIGHT {
		std::string bonename;						// ボーン名
		std::string meshname;						// メッシュ名
		float weight;								// ウェイト値
		int	vertexindex;							// 頂点インデックス
	};

	//ボーン構造体
	struct BONE
	{
		std::string Bonename;						// ボーン名
		std::string Meshname;						// メッシュ名
		std::string Armaturename;					// アーマチュア名
		SimpleMath::Matrix Matrix;
		SimpleMath::Matrix AnimationMatrix;
		SimpleMath::Matrix OffsetMatrix;
		int			idx;
		std::vector<WEIGHT> weights;				// このボーンが影響を与える頂点インデックス・ウェイト値
	};

	struct CBBoneCombMatrix {
		XMFLOAT4X4 BoneCombMtx[400];						// ボーンコンビネーション行列
	};
public:
	Model();
	~Model();
	void SetVertexShader(Shader* vs);
	void SetPixelShader(Shader* ps);
	const Mesh* GetMesh(unsigned int index) const;
	unsigned int GetMeshNum() const;

public:
	bool Load(const char* file, float scaleBase = 1.0f, bool flip = false, bool simple = false);
	void LoadAnimation(const char* FileName, const char* Name, bool flip);
	void Draw(int texSlot = 0);
	
	void UpdateAnimation(const char* AnimationName, int Frame);
	void UpdateBoneMatrix(const aiNode* node, const Matrix& matrix);

	void RemakeVertex(int vtxSize, const std::function<void(RemakeInfo& data)>& func);
	void CreateBone(const aiNode* node);
	void BoneUpdate();
	SimpleMath::Matrix GetScaleBaseMatrix();
	float GetScaleBase();
private:
	void MakeDefaultShader();
	bool CreateConstantBufferWrite(
		ID3D11Device* device,					// デバイスオブジェクト
		unsigned int bytesize,					// コンスタントバッファサイズ
		ID3D11Buffer** pConstantBuffer			// コンスタントバッファ
	);
	std::vector<BONE> GetBoneInfo(const aiMesh* mesh);
	DirectX::SimpleMath::Matrix aiMtxToDxMtx(const aiMatrix4x4& aimatrix);

private:
	Assimp::Importer* importer = nullptr;					// assimpの設定
	const aiScene* m_pScene = nullptr;						// ロード済みモデル情報
	static std::shared_ptr<VertexShader> m_defVS;			// 頂点シェーダー
	static std::shared_ptr<PixelShader> m_defPS;			// ピクセルシェーダー
	std::vector<Matrix> m_bonecombmtxcontainer;				// ボーンコンビネーション行列の配列

	std::unordered_map<std::string, BONE> m_Bone;			// ボーンデータ（名前で参照）
	ID3D11Buffer* m_BoneCombMtxCBuffer = nullptr;						// 定数バッファ
	std::unordered_map<std::string, const aiScene*> m_Animation;

private:
	Meshes m_meshes;
	Materials m_materials;
	VertexShader* m_pVS;
	PixelShader* m_pPS;
	float m_scaleBase = 1.0f;		// モデルのスケール倍率 座標変換時に扱うのでmodelクラス内では現状扱わない
	char* filepath;
};


#endif // __MODEL_H__