// Path-keyed texture cache. ASCII comments only (no BOM).
#include "TextureCache.h"
#include "Texture.h"
#include <string>
#include <unordered_map>

namespace
{
	// key = file path, value = the one decoded texture shared by every caller.
	std::unordered_map<std::string, std::shared_ptr<Texture>> g_cache;
}

namespace TextureCache
{
	std::shared_ptr<Texture> Get(const char* path)
	{
		if (!path || !path[0]) return nullptr;

		std::string key(path);
		auto it = g_cache.find(key);
		if (it != g_cache.end()) return it->second;	// cache hit: no re-decode

		auto tex = std::make_shared<Texture>();
		if (FAILED(tex->Create(path))) return nullptr;	// don't cache a failed load
		g_cache[key] = tex;
		return tex;
	}

	void Clear()
	{
		g_cache.clear();
	}
}
