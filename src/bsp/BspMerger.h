#include "util.h"
#include "Bsp.h"

// bounding box for a map, used for arranging maps for merging
struct MAPBLOCK
{
	vec3 mins, maxs, size, offset;
	Bsp* map;
	std::string merge_name;

	bool intersects(MAPBLOCK& other) {
		return (mins.x <= other.maxs.x && maxs.x >= other.mins.x) &&
			(mins.y <= other.maxs.y && maxs.y >= other.mins.y) &&
			(mins.z <= other.maxs.z && maxs.y >= other.mins.z);
	}
};

class BspMerger {
public:
	BspMerger() = default;

	// merges all maps into one
	// noripent - don't change any entity logic
	// noscript - don't add support for the bspguy map script (worse performance + buggy, but simpler)
	Bsp* merge(std::vector<Bsp*> maps, const vec3& gap, const std::string& output_name, bool noripent, bool noscript);

private:
	int merge_ops = 0;

	// wrapper around BSP data merging for nicer console output
	void merge(MAPBLOCK& dst, MAPBLOCK& src, std::string resultName);

	// merge BSP data
	bool merge(Bsp& mapA, Bsp& mapB);

	std::vector<std::vector<std::vector<MAPBLOCK>>> separate(std::vector<Bsp*>& maps, const vec3& gap);

	// for maps in a series:
	// - changelevels should be replaced with teleports or respawn triggers
	// - monsters should spawn only when the current map is active
	// - entities might need map name prefixes
	// - entities in previous levels should be cleaned up
	void update_map_series_entity_logic(Bsp* mergedMap, std::vector<MAPBLOCK>& sourceMaps, std::vector<Bsp*>& mapOrder, const std::string& output_name, const std::string& firstMapName, bool noscript);

	// renames any entity that shares a name with an entity in another map
	int force_unique_ent_names_per_map(Bsp* mergedMap);

	BSPPLANE separate(Bsp& mapA, Bsp& mapB);

	void merge_ents(Bsp& mapA, Bsp& mapB);
	void merge_planes(Bsp& mapA, Bsp& mapB);
	void merge_textures(Bsp& mapA, Bsp& mapB);
	void merge_vertices(Bsp& mapA, Bsp& mapB);
	void merge_texinfo(Bsp& mapA, Bsp& mapB);
	void merge_faces(Bsp& mapA, Bsp& mapB);
	void merge_leaves(Bsp& mapA, Bsp& mapB);
	void merge_marksurfs(Bsp& mapA, Bsp& mapB);
	void merge_edges(Bsp& mapA, Bsp& mapB);
	void merge_surfedges(Bsp& mapA, Bsp& mapB);
	void merge_nodes(Bsp& mapA, Bsp& mapB);
	void merge_clipnodes(Bsp& mapA, Bsp& mapB);
	void merge_models(Bsp& mapA, Bsp& mapB);
	void merge_vis(Bsp& mapA, Bsp& mapB);
	void merge_lighting(Bsp& mapA, Bsp& mapB);

	void create_merge_headnodes(Bsp& mapA, Bsp& mapB, BSPPLANE separationPlane);


	// remapped structure indexes for mapB when merging
	std::vector<int> texRemap;
	std::vector<int> texInfoRemap;
	std::vector<int> planeRemap;
	std::vector<int> leavesRemap;

	// remapped leaf indexes for mapA's submodel leaves
	std::vector<int> modelLeafRemap;

	int thisLeafCount;
	int otherLeafCount;
	int thisFaceCount;
	int thisWorldFaceCount;
	int otherFaceCount;
	int thisNodeCount;
	int thisClipnodeCount;
	int thisWorldLeafCount; // excludes solid leaf 0
	int otherWorldLeafCount; // excluding solid leaf 0
	int thisSurfEdgeCount;
	int thisMarkSurfCount;
	int thisEdgeCount;
	int thisVertCount;
};