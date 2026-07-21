#include "Model.h"
#include "Defines.h"
#include "DebugLog.h"

#include <assimp/postprocess.h>

#if _MSC_VER >= 1920
#ifdef _DEBUG
#pragma comment(lib, "assimp/x64/Debug/assimp-vc142-mtd.lib")
#else
#pragma comment(lib, "assimp/x64/Release/assimp-vc142-mt.lib")
#endif
#elif _MSC_VER >= 1910
#ifdef _DEBUG
#pragma comment(lib, "assimp/x64/Debug/assimp-vc141-mtd.lib")
#endif
#endif

std::shared_ptr<VertexShader> Model::m_defVS = nullptr;
std::shared_ptr<PixelShader> Model::m_defPS = nullptr;

Model::Model()
	: m_pVS(nullptr)
	, m_pPS(nullptr)
{
	if (!m_defVS && !m_defPS) // どちらもnullptr
	{
		MakeDefaultShader();
	}
	m_pVS = m_defVS.get();
	m_pPS = m_defPS.get();
	importer = new Assimp::Importer();
}
Model::~Model() {

}

void Model::SetVertexShader(Shader* vs)
{
	VertexShader* cast = reinterpret_cast<VertexShader*>(vs);
	if (cast)
		m_pVS = cast;
}
void Model::SetPixelShader(Shader* ps)
{
	PixelShader* cast = reinterpret_cast<PixelShader*>(ps);
	if (cast)
		m_pPS = cast;
}

const Model::Mesh* Model::GetMesh(unsigned int index) const
{
	if (index < 0 || m_meshes.size() <= index)
	{
		return nullptr;
	}
	return &m_meshes[index];
}

uint32_t Model::GetMeshNum() const
{
	return static_cast<uint32_t>(m_meshes.size());
}

bool Model::Load(const char* file, float scaleBase, bool flip, bool simpleMode)
{
	DebugLog::log(DebugLog::INFO_LOG, "モデル読み込み開始");
	DebugLog::log(DebugLog::INFO_LOG, "path ", file);
	m_scaleBase = scaleBase;

	int flag = 0;
	if (simpleMode)
	{
		flag |= aiProcess_Triangulate;					// 非三角ポリゴンを三角に割る
		flag |= aiProcess_JoinIdenticalVertices;		// 同一位置頂点を一つに統合する
		flag |= aiProcess_FlipUVs;						//　UV値をY軸を基準に反転させる
		flag |= aiProcess_PreTransformVertices;			// ノードを一つに統合 !!アニメーション情報が消えることに注意!!
		if (flip) flag |= aiProcess_MakeLeftHanded;		// 左手系座標に変換
	}
	else
	{
		flag |= aiProcessPreset_TargetRealtime_MaxQuality;	// リアルタイム レンダリング用にデータを最適化するデフォルトの後処理構成。
		flag |= aiProcess_PopulateArmatureData;				// 標準的なボーン,アーマチュアの設定
		if (flip) flag |= aiProcess_ConvertToLeftHanded;	// 左手系変更オプションがまとまったもの
	}
	
	// assimpで読み込み
	m_pScene = importer->ReadFile(file, flag);
	if (!m_pScene) {
		Error(importer->GetErrorString());
		DebugLog::log(DebugLog::INFO_LOG, "Assimpモデルロード失敗", file);
		return false;
	}
	assert(m_pScene);
	
	// ボーン情報配列準備
	DebugLog::log(DebugLog::INFO_LOG, "ボーン情報配列準備");
	CreateBone(m_pScene->mRootNode);

	// ボーンの配列インデックスを格納する
	unsigned int num = 0;
	for (auto& data : m_Bone) {
		data.second.idx = num;
		num++;
	}

	// メッシュの作成
	aiVector3D zero(0.0f, 0.0f, 0.0f);
	for (unsigned int i = 0; i < m_pScene->mNumMeshes; ++i)
	{
		Mesh mesh = {};

		// 頂点の作成
		std::vector<Vertex> vtx;
		vtx.resize(m_pScene->mMeshes[i]->mNumVertices);
		for (unsigned int j = 0; j < vtx.size(); ++j)
		{
			// 値の吸出し
			aiVector3D pos = m_pScene->mMeshes[i]->mVertices[j];
			aiVector3D uv = m_pScene->mMeshes[i]->HasTextureCoords(0) ?
				m_pScene->mMeshes[i]->mTextureCoords[0][j] : zero;
			aiVector3D normal = m_pScene->mMeshes[i]->HasNormals() ?
				m_pScene->mMeshes[i]->mNormals[j] : zero;
			// 値を設定
			vtx[j] = {
				DirectX::XMFLOAT3(pos.x, pos.y, pos.z),
				DirectX::XMFLOAT3(normal.x, normal.y, normal.z),
				DirectX::XMFLOAT2(uv.x, uv.y),
				{-1,-1,-1,-1},
				{0.0f,0.0f,0.0f,0.0f}
			};
		}

		// ボーン情報取得
		std::vector<BONE> boneList = GetBoneInfo(m_pScene->mMeshes[i]);

		// 頂点情報にボーン情報紐づけ
		for (auto& bone : boneList)
		{
			for (auto& w : bone.weights)
			{
				int& idx = vtx[w.vertexindex].boneCount;
				vtx[w.vertexindex].boneIndex[idx] = m_Bone[w.bonename].idx;		// indexをセット
				vtx[w.vertexindex].boneWeight[idx] = w.weight;					// weight値をセット
				//ボーンの配列番号をセット
				idx++;
				assert(idx <= 4);
			}
		}

		// インデックスの作成
		std::vector<unsigned int> idx;
		idx.resize(m_pScene->mMeshes[i]->mNumFaces * 3); // faceはポリゴンの数を表す(１ポリゴンで3インデックス
		for (unsigned int j = 0; j < m_pScene->mMeshes[i]->mNumFaces; ++j)
		{
			aiFace face = m_pScene->mMeshes[i]->mFaces[j];
			int faceIdx = j * 3;
			idx[faceIdx + 0] = face.mIndices[0];
			idx[faceIdx + 1] = face.mIndices[1];
			idx[faceIdx + 2] = face.mIndices[2];
		}

		// マテリアルの割り当て
		mesh.materialID = m_pScene->mMeshes[i]->mMaterialIndex;

		// メッシュを元に頂点バッファ作成
		MeshBuffer::Description desc = {};
		desc.pVtx = vtx.data();
		desc.vtxSize = sizeof(Vertex);
		desc.vtxCount = static_cast<UINT>(vtx.size());
		desc.pIdx = idx.data();
		desc.idxSize = sizeof(unsigned int);
		desc.idxCount = static_cast<UINT>(idx.size());
		desc.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		mesh.mesh = std::make_shared<MeshBuffer>(desc);

		// メッシュ追加
		m_meshes.push_back(mesh);
	}

	// 定数バッファ生成　20230909-02
	CreateConstantBufferWrite(
		GetDevice(),					// デバイスオブジェクト
		sizeof(CBBoneCombMatrix),		// コンスタントバッファサイズ
		&m_BoneCombMtxCBuffer);			// コンスタントバッファ

	assert(m_BoneCombMtxCBuffer);

	//--- マテリアルの作成
	// ファイルの探索
	std::string dir = file;
	dir = dir.substr(0, dir.find_last_of('/') + 1);
	// マテリアル
	aiColor3D color(0.0f, 0.0f, 0.0f);
	DirectX::XMFLOAT4 diffuse(1.0f, 1.0f, 1.0f, 1.0f);
	DirectX::XMFLOAT4 ambient(0.3f, 0.3f, 0.3f, 1.0f);
	for (unsigned int i = 0; i < m_pScene->mNumMaterials; ++i)
	{
		Material material = {};

		// 各種パラメーター
		float shininess;
		material.diffuse = m_pScene->mMaterials[i]->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS ?
			DirectX::XMFLOAT4(color.r, color.g, color.b, 1.0f) : diffuse;
		material.ambient = m_pScene->mMaterials[i]->Get(AI_MATKEY_COLOR_AMBIENT, color) == AI_SUCCESS ?
			DirectX::XMFLOAT4(color.r, color.g, color.b, 1.0f) : ambient;
		shininess = m_pScene->mMaterials[i]->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS ? shininess : 0.0f;
		material.specular = m_pScene->mMaterials[i]->Get(AI_MATKEY_COLOR_SPECULAR, color) == AI_SUCCESS ?
			DirectX::XMFLOAT4(color.r, color.g, color.b, shininess) : DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, shininess);

		// テクスチャ
		aiString path;
		if (m_pScene->mMaterials[i]->Get(AI_MATKEY_TEXTURE_DIFFUSE(0), path) == AI_SUCCESS) {
			HRESULT hr;
			// ファイル形式チェック
			if (strstr(path.C_Str(), ".psd"))
			{
				DebugLog::log(DebugLog::ERROR_LOG, "モデルのテクスチャにpsdファイルが指定されています。psd読み込みには非対応。");
				Error("モデルのテクスチャにpsdファイルが指定されています。psd読み込みには非対応。");
			}
			// そのまま探索
			material.texture = std::make_shared<Texture>();
			hr = material.texture->Create(path.C_Str());
			// モデルと同じ階層を探索
			if (FAILED(hr)) {
				std::string file = dir;
				file += path.C_Str();
				hr = material.texture->Create(file.c_str());
			}
			// ファイル名のみで探索
			if (FAILED(hr)) {
				std::string file = path.C_Str();
				if (size_t idx = file.find_last_of('\\'); idx != std::string::npos)
				{
					file = file.substr(idx + 1);
					file = dir + file;
					hr = material.texture->Create(file.c_str());
				}

				// ファイル目のパスが"\\ではなく"/"の場合の対応
				if (FAILED(hr)) {
					if (size_t idx = file.find_last_of('/'); idx != std::string::npos)
					{
						file = file.substr(idx + 1);
						file = dir + file;
						hr = material.texture->Create(file.c_str());
					}
				}
			}
			// 失敗
			if (FAILED(hr)) {
				Error(path.C_Str());
				material.texture = nullptr;
			}
		}
		else {
			material.texture = nullptr;
		}

		// マテリアル追加
		m_materials.push_back(material);
	}

	DebugLog::log(DebugLog::INFO_LOG, "モデル読み込み完了");
	return true;
}


void Model::LoadAnimation(const char* FileName, const char* Name, bool flip)
{
	int flag = 0;
	if (flip) flag |= aiProcess_ConvertToLeftHanded;	// 左手系変更オプションがまとまったもの
	m_Animation[Name] = aiImportFile(FileName, flag);
	assert(m_Animation[Name]);
}

void Model::Draw(int texSlot)
{
	m_pVS->Bind();
	m_pPS->Bind();
	auto it = m_meshes.begin();
	// 20230909-02 レジスタ５にセット
	GetContext()->VSSetConstantBuffers(5, 1, &m_BoneCombMtxCBuffer);
	while (it != m_meshes.end())
	{
		if (texSlot >= 0)
			m_pPS->SetTexture(texSlot, m_materials[it->materialID].texture.get());
		it->mesh->Draw();
		++it;
	}
}

void Model::RemakeVertex(int vtxSize, const std::function<void(RemakeInfo& data)>& func)
{
	auto it = m_meshes.begin();

	while (it != m_meshes.end())
	{
		MeshBuffer::Description desc = it->mesh->GetDesc();

		char* newVtx = new char[vtxSize * desc.vtxCount];
		RemakeInfo data = {};
		data.vtxNum = desc.vtxCount;
		data.dest = newVtx;
		data.source = desc.pVtx;
		data.idxNum = desc.idxCount;
		data.idx = desc.pIdx;
		func(data);

		desc.pVtx = newVtx;
		desc.vtxSize = vtxSize;
		it->mesh = std::make_shared<MeshBuffer>(desc);

		delete[] newVtx;
		++it;
	}
}

void Model::MakeDefaultShader()
{
	const char* vsCode = R"EOT(
struct VS_IN {
	float3 pos : POSITION0;
	float3 normal : NORMAL0;
	float2 uv : TEXCOORD0;
};
struct VS_OUT {
	float4 pos : SV_POSITION;
};
VS_OUT main(VS_IN vin) {
	VS_OUT vout;
	vout.pos = float4(vin.pos, 1.0f);
	vout.pos.z *= 0.1f;
	return vout;
})EOT";
	const char* psCode = R"EOT(
struct PS_IN {
	float4 pos : SV_POSITION;
};
float4 main(PS_IN pin) : SV_TARGET
{
	return float4(1.0f, 0.0f, 1.0f, 1.0f);
})EOT";
	m_defVS = std::make_shared<VertexShader>();
	m_defVS->Compile(vsCode);
	m_defPS = std::make_shared<PixelShader>();
	m_defPS->Compile(psCode);
}

// 再帰的ボーン生成
void Model::CreateBone(const aiNode* node)
{
	BONE bone;

	m_Bone[node->mName.C_Str()] = bone;
	DebugLog::log(DebugLog::INFO_LOG,"BoneName = ", node->mName.C_Str());

	for (unsigned int n = 0; n < node->mNumChildren; n++)
	{
		CreateBone(node->mChildren[n]);
	}

}

// アニメーション更新
void Model::UpdateAnimation(const char* AnimationName, int Frame)
{
	if (m_Animation.count(AnimationName) == 0) return;
	if (!m_Animation[AnimationName]->HasAnimations()) return;

	Matrix rootMatrix;
	// SRTから行列を生成
	Vector3 scale = { 1.0f, 1.0f, 1.0f };		// 20231231 DX化
	Vector3 position = { 0.0f, 0.0f, 0.0f };	// 20231231 DX化
	Quaternion rotation;						// 20231231 DX化
	rotation.x = 0.0f;							// 20231231 DX化
	rotation.y = 0.0f;							// 20231231 DX化
	rotation.z = 0.0f;							// 20231231 DX化
	rotation.w = 1.0f;							// 20231231 DX化

	Matrix scalemtx = Matrix::CreateScale(scale.x, scale.y, scale.z);					// 20231231 DX化
	Matrix rotmtx = Matrix::CreateFromQuaternion(rotation);								// 20231231 DX化
	Matrix transmtx = Matrix::CreateTranslation(position.x, position.y, position.z);	// 20231231 DX化

	rootMatrix = scalemtx * rotmtx * transmtx;							// 20231231 DX化

	//アニメーションデータからボーンマトリクス算出
	aiAnimation* animation = m_Animation[AnimationName]->mAnimations[0];

	for (unsigned int c = 0; c < animation->mNumChannels; c++)
	{
		aiNodeAnim* nodeAnim = animation->mChannels[c];

		BONE* bone = &m_Bone[nodeAnim->mNodeName.C_Str()];
		int f;

		f = Frame % nodeAnim->mNumRotationKeys;				//簡易実装
		aiQuaternion rot = nodeAnim->mRotationKeys[f].mValue;

		f = Frame % nodeAnim->mNumPositionKeys;				//簡易実装
		aiVector3D pos = nodeAnim->mPositionKeys[f].mValue;

		// SRTから行列を生成
		Vector3 scale = { 1.0f, 1.0f, 1.0f };				// 20231231 DX化
		Vector3 position = { pos.x, pos.y, pos.z };			// 20231231 DX化
		Quaternion rotation;								// 20231231 DX化
		rotation.x = rot.x;									// 20231231 DX化
		rotation.y = rot.y;									// 20231231 DX化
		rotation.z = rot.z;									// 20231231 DX化
		rotation.w = rot.w;									// 20231231 DX化

		Matrix scalemtx = Matrix::CreateScale(scale.x, scale.y, scale.z);					// 20231231 DX化
		Matrix rotmtx = Matrix::CreateFromQuaternion(rotation);								// 20231231 DX化
		Matrix transmtx = Matrix::CreateTranslation(position.x, position.y, position.z);	// 20231231 DX化

		bone->AnimationMatrix = scalemtx * rotmtx * transmtx;								// 20231231 DX化
	}

	UpdateBoneMatrix(m_pScene->mRootNode, rootMatrix);

	// 定数バッファに書き込むための情報を生成
	m_bonecombmtxcontainer.clear();
	m_bonecombmtxcontainer.resize(m_Bone.size());
	for (auto data : m_Bone) {
		m_bonecombmtxcontainer[data.second.idx] = data.second.Matrix;
	}

	// 20230909 転置
	/*for (auto& bcmtx : m_bonecombmtxcontainer)
	{
		// 転置する
		bcmtx.Transpose();
	}*/

	BoneUpdate();

}

void Model::UpdateBoneMatrix(const aiNode* node, const Matrix& matrix)
{
	if (node->mName.length <= 0)
	{
		DebugLog::log(DebugLog::ERROR_LOG, "ノード(ボーン)名が取得できていないorない");
		Error("ノード(ボーン)名が取得できていないorない");
		return;
	}
	// 引数で渡されたノード名をキーとしてボーン情報を取得する
	BONE* bone = &m_Bone[node->mName.C_Str()];

	//マトリクスの乗算順番に注意

	// ボーンコンビネーション行列生成				
	Matrix bonecombinationmtx = bone->OffsetMatrix * bone->AnimationMatrix * matrix;	// ボーンオフセット行列×ボーンアニメメーション行列×逆ボーンオフセット行列

	bone->Matrix = bonecombinationmtx;			// ボーンコンビネーション行列をボーン情報に反映させる

	// 自分の姿勢を表す行列を作成
	Matrix mybonemtx = bone->AnimationMatrix * matrix;

	for (unsigned int n = 0; n < node->mNumChildren; n++)
	{
		UpdateBoneMatrix(node->mChildren[n], mybonemtx);
	}
}

/*----------------------------
コンスタントバッファを作成(MAPで書き換え可能)
------------------------------*/
bool  Model::CreateConstantBufferWrite(
	ID3D11Device* device,					// デバイスオブジェクト
	unsigned int bytesize,					// コンスタントバッファサイズ
	ID3D11Buffer** pConstantBuffer			// コンスタントバッファ
) {

	// コンスタントバッファ生成
	D3D11_BUFFER_DESC bd;

	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DYNAMIC;							// バッファ使用方法
	bd.ByteWidth = bytesize;								// バッファの大き
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;				// コンスタントバッファ
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;				// CPUアクセス可能

	HRESULT hr = device->CreateBuffer(&bd, nullptr, pConstantBuffer);
	if (FAILED(hr)) {
		MessageBox(nullptr, "CreateBuffer(constant buffer) error", "Error", MB_OK);
		return false;
	}

	return true;
}

// ボーンコンビネーション行列バッファ格納
void Model::BoneUpdate()
{
	// メッシュから更新されたボーンコンビネーション行列群を取得
	const std::vector<Matrix> mtxcontainer = m_bonecombmtxcontainer;			// 20231231 DX化

	// 定数バッファ更新
	D3D11_MAPPED_SUBRESOURCE MappedResource;
	CBBoneCombMatrix* pData = nullptr;

	HRESULT hr = GetContext()->Map(
		m_BoneCombMtxCBuffer,
		0,
		D3D11_MAP_WRITE_DISCARD,
		0,
		&MappedResource);

	if (SUCCEEDED(hr)) {
		pData = (CBBoneCombMatrix*)MappedResource.pData;
		memcpy(pData->BoneCombMtx,
			mtxcontainer.data(),
			sizeof(Matrix) * mtxcontainer.size());									// 20231231 DX化
		GetContext()->Unmap(m_BoneCombMtxCBuffer, 0);
	}
}

// サブセットに紐づいているボーン情報を取得する
std::vector<Model::BONE> Model::GetBoneInfo(const aiMesh* mesh) {

	std::vector<BONE> bones;		// このサブセットメッシュで使用されているボーンコンテナ

	// ボーン数分ループ
	for (unsigned int bidx = 0; bidx < mesh->mNumBones; bidx++) {

		BONE bone{};

		// ボーン名取得
		bone.Bonename = std::string(mesh->mBones[bidx]->mName.C_Str());
		// メッシュノード名
		if (mesh->mBones[bidx]->mNode != nullptr)
		{
			bone.Meshname = std::string(mesh->mBones[bidx]->mNode->mName.C_Str());
		}
		else {
			DebugLog::log(DebugLog::WARNING_LOG,"ノード情報がNULL");
		}

		// アーマチュアノード名
		if (mesh->mBones[bidx]->mArmature != nullptr)
		{
			bone.Armaturename = std::string(mesh->mBones[bidx]->mArmature->mName.C_Str());
		}
		else {
			DebugLog::log(DebugLog::WARNING_LOG,"アーマチュア情報がNULL");
		}
		// ボーンオフセット行列取得
		bone.OffsetMatrix = aiMtxToDxMtx(mesh->mBones[bidx]->mOffsetMatrix);

		// ウェイト情報抽出
		bone.weights.clear();
		for (unsigned int widx = 0; widx < mesh->mBones[bidx]->mNumWeights; widx++) {

			WEIGHT w;
			w.meshname = bone.Meshname;										// メッシュ名
			w.bonename = bone.Bonename;										// ボーン名

			w.weight = mesh->mBones[bidx]->mWeights[widx].mWeight;			// 重み
			w.vertexindex = mesh->mBones[bidx]->mWeights[widx].mVertexId;	// 頂点インデックス
			bone.weights.emplace_back(w);
		}

		// コンテナに登録
		bones.emplace_back(bone);

		// ボーン辞書にも反映させる
		m_Bone[mesh->mBones[bidx]->mName.C_Str()].OffsetMatrix = aiMtxToDxMtx(mesh->mBones[bidx]->mOffsetMatrix);   // 20231231 DX化

	}

	return bones;
}


SimpleMath::Matrix Model::GetScaleBaseMatrix()
{
	return DirectX::XMMatrixScaling(m_scaleBase, m_scaleBase, m_scaleBase);
}

float Model::GetScaleBase()
{
	return m_scaleBase;
}


// assimp行列をDIRECTXの行列に変換する
DirectX::SimpleMath::Matrix Model::aiMtxToDxMtx(const aiMatrix4x4& aimatrix) {

	DirectX::SimpleMath::Matrix dxmtx = {
	   aimatrix.a1,aimatrix.b1,aimatrix.c1,aimatrix.d1,
	   aimatrix.a2,aimatrix.b2,aimatrix.c2,aimatrix.d2,
	   aimatrix.a3,aimatrix.b3,aimatrix.c3,aimatrix.d3,
	   aimatrix.a4,aimatrix.b4,aimatrix.c4,aimatrix.d4
	};

	return dxmtx;
}