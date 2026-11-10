#pragma once

class Texture;

//====================================================================
//  Auto exposure (eye adaptation). The eye's pupil closes when you look at something bright and
//  opens again in the dark, so a bright window stops glaring after a moment and a dark corner
//  becomes readable after a moment. Fixed exposure cannot do that.
//
//  1) Measure: the HDR scene -> a small image of log(luminance) -> its mip chain averages it to
//     1 pixel (log-average = geometric mean brightness). That pixel is copied to a CPU-readable
//     texture and read a few frames LATER (no stall: the CPU never waits for the GPU).
//  2) Adapt: target exposure = KEY / average brightness (KEY = the brightness a "normal" scene
//     should have), clamped, and approached smoothly over time (exponential, frame-rate
//     independent). Getting used to the light (closing) is fast, to the dark (opening) slower,
//     like the real eye.
//  The exposure multiplies the HDR color right before the ACES tonemap (PostProcess).
//  Search: "auto exposure eye adaptation log average luminance" / JP: jidou roshutsu, meijunnou anjunnou
//====================================================================
namespace AutoExposure
{
    struct Settings
    {
        bool  enabled     = true;
        float key         = 0.18f;   // target average brightness (0.18 = photographic middle grey)
        float minExposure = 0.35f;   // limits: never darker / brighter than this (the eye has limits too)
        float maxExposure = 1.8f;
        float toBrightSec = 0.4f;    // time constant when the scene gets brighter (pupil closes fast)
        float toDarkSec   = 1.6f;    // time constant when the scene gets darker (eyes adjust slowly)
    };
    Settings& Get();

    void  Measure(Texture* hdrScene);   // once per frame, after the scene is drawn, before tonemapping
    void  Update(float dt);             // move the exposure toward the target
    float Exposure();                   // the multiplier to apply (1 when disabled)
    void  Uninit();
}
