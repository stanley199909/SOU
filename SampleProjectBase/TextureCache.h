#ifndef __TEXTURE_CACHE_H__
#define __TEXTURE_CACHE_H__

#include <memory>

class Texture;

// Path-keyed texture cache (ASCII comments only; this file has no BOM).
//  First Get(path) decodes the image once and keeps the shared_ptr; every later Get(path)
//  - even from another scene after a scene switch - returns the SAME texture instantly.
//  The map is static (whole-app lifetime), so switching scenes no longer re-decodes the
//  forge/water/UI PNGs = the last chunk of the scene-switch stall. Returns nullptr on failure
//  (failures are not cached, so a fixed file can load next time).
namespace TextureCache
{
	std::shared_ptr<Texture> Get(const char* path, bool srgb = true);
	void Clear();	// drop all cached textures (optional; e.g. on shutdown)
}

#endif // __TEXTURE_CACHE_H__
