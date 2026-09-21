// Shared cottage materials and draw path for game and stage editor.
#pragma once
#include "Model.h"
#include "SunStage.h"
#include "CameraBase.h"
#include "TextureCache.h"
#include <array>
#include <fstream>
#include <sstream>
#include <cstring>
#include "imgui/imgui.h"

namespace CottageRender
{
using namespace DirectX;
struct Material
{
    const char* key;
    std::string asset;
    float tile, roughness, metal;
    XMFLOAT4 tint;
    std::shared_ptr<Texture> base, normal, rough;
};
struct Settings
{
    std::array<Material, 12> materials{{
        {"stone", "rock_surface", 2.0f, 1.0f, 0.0f, {0.85f,0.88f,0.94f,1}},
        {"mortar", "rock_surface", 1.0f, 1.0f, 0.0f, {0.45f,0.44f,0.42f,1}},
        {"floor", "monastery_stone_floor", 4.0f, 1.0f, 0.0f, {0.85f,0.87f,0.90f,1}},
        {"dirt", "rock_surface", 2.0f, 1.0f, 0.0f, {0.35f,0.28f,0.20f,1}},
        {"wood", "wood_planks", 2.0f, 1.0f, 0.0f, {0.40f,0.32f,0.24f,1}},
        {"roof", "roof_slates_03", 3.0f, 1.0f, 0.0f, {0.12f,0.10f,0.08f,1}},
        {"iron", "rock_surface", 0.7f, 0.7f, 0.65f, {0.12f,0.09f,0.07f,1}},
        {"glass", "rock_surface", 1.0f, 0.1f, 0.0f, {0.65f,0.76f,0.80f,0.06f}},
        {"yard", "brown_mud_02", 1.3f, 1.0f, 0.0f, {0.85f,0.80f,0.72f,1}},
        {"grass", "grass_path_2", 1.2f, 1.0f, 0.0f, {0.23f,0.30f,0.085f,1}},
        {"bark", "wood_planks", 0.8f, 1.0f, 0.0f, {0.30f,0.27f,0.22f,1}},
        {"leaves", "grass_path_2", 1.0f, 1.0f, 0.0f, {0.14f,0.25f,0.055f,1}}
    }};
    // Direction points from the surface toward the sun. w is radiance.
    XMFLOAT4 sun{0.25f,0.60f,0.76f,3.0f};
    XMFLOAT4 ambient{0.38f,0.43f,0.52f,16.0f}; // rgb sky fill, w fire intensity
    float outdoorFill=2.4f; // Readable diffuse sky light outdoors.
    XMFLOAT4 exteriorSky{0.45f,0.65f,0.95f,1.0f};
    // Source FBX units (centimeters), measured from the glass pane.
    XMFLOAT4 window{0,170,285,28.4f}; // xyz center, w half width
    XMFLOAT4 windowExtra{38.4f,0.0f,0.0f,4.0f}; // x half height; yz mullions; w indoor fill strength
};
inline Settings& Data() { static Settings settings; return settings; }
inline void Load()
{
    auto& s = Data();
    std::ifstream file("Assets/cottage_materials.txt");
    std::string line;
    while (std::getline(file,line))
    {
        std::istringstream row(line);
        std::string kind; row >> kind;
        if (kind == "material")
        {
            std::string key; row >> key;
            for (auto& m : s.materials) if (key == m.key)
            {
                Material next = m;
                if (row >> next.asset >> next.tile >> next.roughness >> next.metal
                    >> next.tint.x >> next.tint.y >> next.tint.z >> next.tint.w)
                    if (next.tile > 0 && next.roughness > 0 && next.tint.w >= 0 && next.tint.w <= 1)
                        m = next;
            }
        }
        else if (kind == "outdoor_fill") row >> s.outdoorFill;
        else if (kind == "fill") row >> s.windowExtra.w;
        else if (kind == "sky") row >> s.exteriorSky.x >> s.exteriorSky.y >> s.exteriorSky.z;
        else if (kind == "sun") row >> s.sun.x >> s.sun.y >> s.sun.z >> s.sun.w;
        else if (kind == "ambient") row >> s.ambient.x >> s.ambient.y >> s.ambient.z >> s.ambient.w;
        else if (kind == "window") row >> s.window.x >> s.window.y >> s.window.z >> s.window.w
            >> s.windowExtra.x >> s.windowExtra.y >> s.windowExtra.z;
    }
    for (auto& m : s.materials)
    {
        const std::string path = "Assets/Cottage_Materials/" + m.asset;
        m.base = TextureCache::Get((path + "_base.jpg").c_str());
        m.normal = TextureCache::Get((path + "_normal.jpg").c_str(), false);
        m.rough = TextureCache::Get((path + "_rough.jpg").c_str(), false);
    }
}
inline Material& Select(const char* name)
{
    auto& m = Data().materials;
    if (!name) return m[0];
    if (strstr(name,"OutdoorGround")) return m[8];
    if (strstr(name,"OutdoorGrass")) return m[9];
    if (strstr(name,"OutdoorBark")) return m[10];
    if (strstr(name,"OutdoorLeaves")) return m[11];
    if (strstr(name,"Glass")) return m[7];
    if (strstr(name,"Floor_Stone")) return m[2];
    if (strstr(name,"Floor")) return m[3];
    if (strstr(name,"Wood")) return m[4];
    if (strstr(name,"Roof")) return m[5];
    if (strstr(name,"Iron")) return m[6];
    if (strstr(name,"Mortar")) return m[1];
    return m[0];
}
inline void Assign(Model* model)
{
    if (!model) return;
    for (size_t i=0; i<model->GetMaterialCount(); ++i)
        model->SetTextureAt(i,Select(model->GetMaterialName(i)).base);
}
struct Params
{
    XMFLOAT4 material; // tile, roughness multiplier, metallic, is glass
    XMFLOAT4 tint;
    XMFLOAT4 eye;
    XMFLOAT4 sun;
    XMFLOAT4 ambient;
    XMFLOAT4 fire;
    XMFLOAT4 window;
    XMFLOAT4 windowExtra;
    XMFLOAT4X4 worldToLocal;
    XMFLOAT4X4 lightVP;
    XMFLOAT4 shadow;
    XMFLOAT4 lighting;
};
static_assert(sizeof(Params)==288, "PS_Wall constant buffer layout");
inline void Draw(Model* model, FXMMATRIX world, CameraBase* camera,
                 VertexShader* vs, PixelShader* ps, const float* firePos, bool glassPass, bool outdoor=false)
{
    if (!model || !camera || !vs || !ps) return;
    auto& settings=Data();
    XMFLOAT4X4 matrices[3];
    XMStoreFloat4x4(&matrices[0],XMMatrixTranspose(world));
    matrices[1]=camera->GetView(); matrices[2]=camera->GetProj();
    vs->WriteBuffer(0,matrices);
    vs->Bind();
    SetBlendMode(glassPass ? BLEND_ALPHA : BLEND_NONE);
    SetDepthTest(glassPass ? DEPTH_ENABLE_TEST : DEPTH_ENABLE_WRITE_TEST);
    Params params{};
    auto eye=camera->GetPos();
    // FBX centimeters to baked UV meters. Uniform house scaling is supported.
    constexpr float kFbxUnitsPerMeter=100.0f;
    const float uvScale=XMVectorGetX(XMVector3Length(world.r[0]))*kFbxUnitsPerMeter;
    params.eye={eye.x,eye.y,eye.z,uvScale};
    params.sun=settings.sun; params.ambient=settings.ambient;
    params.fire={firePos[0],firePos[1],firePos[2],settings.ambient.w};
    params.lighting={settings.outdoorFill,0,0,0};
    params.lightVP=SunStage::Data().lightVP;
    params.shadow={SunStage::Data().hasShadow?1.0f:0.0f,1.0f/SunStage::ShadowSize,SunStage::ShadowDepthBias,SunStage::ReceiverOffset};
    params.window=settings.window; params.windowExtra=settings.windowExtra;
    XMStoreFloat4x4(&params.worldToLocal,XMMatrixTranspose(XMMatrixInverse(nullptr,world)));
    for (unsigned int i=0; i<model->GetMeshNum(); ++i)
    {
        const auto* mesh=model->GetMesh(i);
        const char* name=model->GetMaterialName(mesh->materialID);
        const bool glass=name && strstr(name,"Glass");
        if (glass!=glassPass) continue;
        auto& material=Select(name);
        if (!material.base || !material.normal || !material.rough) continue;
        params.material={material.tile,material.roughness,material.metal,glass ? 1.0f:0.0f};
        params.tint=material.tint;
        if(outdoor) {
            params.material.w=2.0f;
            if(name && strstr(name,"OutdoorGround")) params.material.w=3.0f;
            if(name && (strstr(name,"OutdoorLeaves") || strstr(name,"OutdoorGrass"))) params.material.w=4.0f;
        }
        if (name && strstr(name,"Soot"))
        {
            constexpr float kSootTint=0.55f;
            params.tint.x*=kSootTint; params.tint.y*=kSootTint; params.tint.z*=kSootTint;
        }
        ps->WriteBuffer(0,&params);
        ps->SetTexture(0,material.base.get());
        ps->SetTexture(1,material.normal.get());
        ps->SetTexture(2,material.rough.get());
        ps->SetTexture(4,settings.materials[9].base.get());
        ps->Bind();
        SunStage::BindShadow();
        mesh->mesh->Draw();
    }
    // No stale normal/roughness SRVs in subsequent water/particle passes.
    ps->SetTexture(1,nullptr); ps->SetTexture(2,nullptr); ps->SetTexture(4,nullptr);
    SetBlendMode(BLEND_ALPHA);
    SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
}

// Solid sky background, visible only where geometry leaves the view open.
// This is not an outdoor scene or a bright decal pasted over the glass.
inline void ClearExterior()
{
    ID3D11RenderTargetView* target=nullptr;
    GetContext()->OMGetRenderTargets(1,&target,nullptr);
    if (target)
    {
        GetContext()->ClearRenderTargetView(target,&Data().exteriorSky.x);
        target->Release();
    }
}
inline bool Save()
{
    const auto& s=Data();
    std::ofstream out("Assets/cottage_materials.txt");
    if (!out) return false;
    out << "# Shared cottage look; distances in world meters except window (source FBX units).\n";
    for (const auto& m : s.materials)
        out << "material " << m.key << ' ' << m.asset << ' ' << m.tile << ' '
            << m.roughness << ' ' << m.metal << ' ' << m.tint.x << ' ' << m.tint.y << ' '
            << m.tint.z << ' ' << m.tint.w << '\n';
    out << "sun " << s.sun.x << ' ' << s.sun.y << ' ' << s.sun.z << ' ' << s.sun.w << '\n';
    out << "ambient " << s.ambient.x << ' ' << s.ambient.y << ' ' << s.ambient.z << ' ' << s.ambient.w << '\n';
    out << "window " << s.window.x << ' ' << s.window.y << ' ' << s.window.z << ' '
        << s.window.w << ' ' << s.windowExtra.x << ' ' << s.windowExtra.y << ' ' << s.windowExtra.z << '\n';
    out << "outdoor_fill " << s.outdoorFill << '\n';
    out << "fill " << s.windowExtra.w << '\n';
    out << "sky " << s.exteriorSky.x << ' ' << s.exteriorSky.y << ' ' << s.exteriorSky.z << '\n';
    out.flush();
    return out.good();
}
inline void Controls()
{
    if (!ImGui::CollapsingHeader("Cottage materials / window light")) return;
    auto& s=Data();
    constexpr float kStep=0.01f, kMinTile=0.1f, kMaxTile=12.0f;
    constexpr float kMinRoughness=0.08f, kMaxRoughness=2.0f, kMaxLight=128.0f;
    ImGui::DragFloat3("Toward sun XYZ",&s.sun.x,kStep,-1.0f,1.0f);
    ImGui::SliderFloat("Sun intensity",&s.sun.w,0.0f,kMaxLight);
    ImGui::ColorEdit3("Sky fill color",&s.ambient.x);
    constexpr float kMaxFill=8.0f;
    ImGui::SliderFloat("Indoor fill strength",&s.windowExtra.w,0.0f,kMaxFill);
    ImGui::SliderFloat("Outdoor fill strength",&s.outdoorFill,0.0f,kMaxFill);
    ImGui::ColorEdit3("Exterior sky",&s.exteriorSky.x);
    bool vsync=GetVSyncEnabled();
    if (ImGui::Checkbox("VSync (limit GPU load)",&vsync)) SetVSyncEnabled(vsync);
    ImGui::Text("Frame: %.1f FPS / %.2f ms",ImGui::GetIO().Framerate,
        1000.0f / (ImGui::GetIO().Framerate > 0 ? ImGui::GetIO().Framerate : 1.0f));
    ImGui::SliderFloat("Fire intensity",&s.ambient.w,0.0f,kMaxLight);
    ImGui::SliderFloat("Glass opacity",&s.materials[7].tint.w,0.0f,1.0f);
    for (auto& m:s.materials)
    {
        ImGui::PushID(m.key);
        if (ImGui::TreeNode(m.key))
        {
            ImGui::DragFloat("Tile meters",&m.tile,kStep,kMinTile,kMaxTile);
            ImGui::SliderFloat("Roughness multiplier",&m.roughness,kMinRoughness,kMaxRoughness);
            ImGui::ColorEdit3("Tint",&m.tint.x);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    static int saved=0;
    if (ImGui::Button("Save cottage look")) saved=Save() ? 1:-1;
    if (saved==1) ImGui::TextDisabled("Saved to Assets/cottage_materials.txt");
    if (saved==-1) ImGui::TextUnformatted("Save failed; check file permissions.");
    ImGui::TextWrapped("Sun shadows follow geometry (2048px cached map). Glass transmits sunlight. No volumetric beams.");
}

}
