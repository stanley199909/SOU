#pragma once
#include "MeshBuffer.h"
#include <DirectXMath.h>
#include <memory>
#include <vector>
#include <cmath>

// Shared game/editor geometry: low-poly charcoal built once, without a UV atlas.
namespace CoalBedMesh {
struct Vertex { DirectX::XMFLOAT3 pos; DirectX::XMFLOAT2 uv; DirectX::XMFLOAT4 shade; };
inline std::shared_ptr<MeshBuffer> Create() {
    constexpr int Columns=5,Rows=7,Sides=7;
    constexpr float Coverage=.94f,RadiusFraction=.46f,Height=.045f;
    constexpr float Tau=6.28318530718f,ShadeBase=.36f,ShadeRange=.30f;
    constexpr float VariationFrequency=2.17f,HeightVariation=.25f;
    constexpr float JitterFraction=.12f,ShoulderRadius=.72f,ShoulderHeight=.65f;
    constexpr float RowPhase=1.37f,SideVariation=.10f;
    std::vector<Vertex> vertices;
    auto vertex=[](float x,float y,float z,float shade,float ember) {
        return Vertex{{x,y,z},{(x+1)*.5f,(z+1)*.5f},{shade,shade,shade,ember}};
    };
    auto triangle=[&](Vertex a,Vertex b,Vertex c) {
        vertices.insert(vertices.end(),{a,b,c,c,b,a}); // Two sides for either raster winding.
    };
    triangle(vertex(-1,0,-1,0,1),vertex(-1,0,1,0,1),vertex(1,0,-1,0,1));
    triangle(vertex(1,0,-1,0,1),vertex(-1,0,1,0,1),vertex(1,0,1,0,1));
    for(int row=0;row<Rows;++row) for(int col=0;col<Columns;++col) {
        const float variation=std::sin((row*Columns+col)*VariationFrequency);
        const float x=Coverage*((col+.5f+JitterFraction*variation)*2/Columns-1);
        const float z=Coverage*((row+.5f+JitterFraction*std::cos(col+row*RowPhase))*2/Rows-1);
        const float height=Height*(1+HeightVariation*variation);
        for(int side=0;side<Sides;++side) {
            const float a=Tau*side/Sides+variation,b=Tau*(side+1)/Sides+variation;
            const float shade=ShadeBase+ShadeRange*std::cos(a);
            auto ring=[&](float angle,float radius,float y) {
                radius*=1+SideVariation*std::sin(angle*Sides+col+row);
                return vertex(x+std::cos(angle)*Coverage*2/Columns*RadiusFraction*radius,
                    y,z+std::sin(angle)*Coverage*2/Rows*RadiusFraction*radius,shade,0);
            };
            auto lowA=ring(a,1,0),lowB=ring(b,1,0);
            auto highA=ring(a,ShoulderRadius,height*ShoulderHeight),highB=ring(b,ShoulderRadius,height*ShoulderHeight);
            triangle(lowA,highA,lowB);triangle(lowB,highA,highB);
            triangle(highA,vertex(x,height,z,shade,0),highB);
        }
    }
    MeshBuffer::Description d{};d.pVtx=vertices.data();d.vtxSize=sizeof(Vertex);
    d.vtxCount=static_cast<UINT>(vertices.size());d.topology=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    return std::make_shared<MeshBuffer>(d);
}
}
