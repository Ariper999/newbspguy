#include "Bsp.h"
#include "util.h"
#include <algorithm>
#include <sstream>
#include "lodepng.h"
#include "rad.h"

Bsp::Bsp() {
	lumps = new byte * [HEADER_LUMPS];

	header.nVersion = 30;

	for (int i = 0; i < HEADER_LUMPS; i++) {
		header.lump[i].nLength = 0;
		header.lump[i].nOffset = 0;
		lumps[i] = NULL;
	}

	name = "merged";
	valid = true;
}

Bsp::Bsp(std::string fpath)
{
	if (fpath.rfind(".bsp") != fpath.size() - 4) {
		fpath = fpath + ".bsp";
	}
	this->path = fpath;
	this->name = stripExt(basename(fpath));
	valid = false;

	bool exists = true;
	if (!fileExists(fpath)) {
		cout << "ERROR: " + fpath + " not found\n";
		return;
	}

	if (!load_lumps(fpath)) {
		cout << fpath + " is not a valid BSP file\n";
		return;
	}

	load_ents();

	valid = true;
}

Bsp::~Bsp()
{	 
	for (int i = 0; i < HEADER_LUMPS; i++)
		if (lumps[i])
			delete [] lumps[i];
	delete [] lumps;

	for (int i = 0; i < ents.size(); i++)
		delete ents[i];
}

void Bsp::get_bounding_box(vec3& mins, vec3& maxs) {
	BSPMODEL& thisWorld = ((BSPMODEL*)lumps[LUMP_MODELS])[0];

	// the model bounds are little bigger than the actual vertices bounds in the map,
	// but if you go by the vertices then there will be collision problems.

	mins = thisWorld.nMins;
	maxs = thisWorld.nMaxs;
}

bool Bsp::move(vec3 offset) {
	split_shared_model_structures();

	BSPPLANE* planes = (BSPPLANE*)lumps[LUMP_PLANES];
	BSPTEXTUREINFO* texInfo = (BSPTEXTUREINFO*)lumps[LUMP_TEXINFO];
	BSPLEAF* leaves = (BSPLEAF*)lumps[LUMP_LEAVES];
	BSPMODEL* models = (BSPMODEL*)lumps[LUMP_MODELS];
	BSPNODE* nodes = (BSPNODE*)lumps[LUMP_NODES];
	BSPFACE* faces = (BSPFACE*)lumps[LUMP_FACES];
	vec3* verts = (vec3*)lumps[LUMP_VERTICES];
	COLOR3* lightdata = (COLOR3*)lumps[LUMP_LIGHTING];

	int planeCount = header.lump[LUMP_PLANES].nLength / sizeof(BSPPLANE);
	int texInfoCount = header.lump[LUMP_TEXINFO].nLength / sizeof(BSPTEXTUREINFO);
	int leafCount = header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF);
	int modelCount = header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL);
	int nodeCount = header.lump[LUMP_NODES].nLength / sizeof(BSPNODE);
	int vertCount = header.lump[LUMP_VERTICES].nLength / sizeof(vec3);
	int faceCount = header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);
	int thisColorCount = header.lump[LUMP_LIGHTING].nLength / sizeof(COLOR3);

	bool hasLighting = thisColorCount > 0;

	LIGHTMAP* oldLightmaps = NULL;
	LIGHTMAP* newLightmaps = NULL;

	if (hasLighting) {
		progress_title = "Calculate lightmaps";
		progress = 0;
		progress_total = faceCount;

		oldLightmaps = new LIGHTMAP[faceCount];
		newLightmaps = new LIGHTMAP[faceCount];
		memset(oldLightmaps, 0, sizeof(LIGHTMAP) * faceCount);
		memset(newLightmaps, 0, sizeof(LIGHTMAP) * faceCount);

		qrad_init_globals(this);

		for (int i = 0; i < faceCount; i++) {
			BSPFACE& face = faces[i];

			int size[2];
			GetFaceLightmapSize(i, size);

			int lightmapSz = size[0] * size[1];

			int lightmapCount = 0;
			for (int k = 0; k < 4; k++) {
				lightmapCount += face.nStyles[k] != 255;
			}
			lightmapSz *= lightmapCount;

			oldLightmaps[i].width = size[0];
			oldLightmaps[i].height = size[1];
			oldLightmaps[i].layers = lightmapCount;

			qrad_get_lightmap_flags(this, i, oldLightmaps[i].luxelFlags);

			print_move_progress();
		}
	}
	
	progress_title = "Moving structures";
	progress = 0;
	progress_total = ents.size() + modelCount;

	bool* modelHasOrigin = new bool[modelCount];
	memset(modelHasOrigin, 0, modelCount * sizeof(bool));

	for (int i = 0; i < ents.size(); i++) {
		if (!ents[i]->hasKey("origin")) {
			continue;
		}
		if (ents[i]->isBspModel()) {
			modelHasOrigin[ents[i]->getBspModelIdx()] = true;
		}

		if (ents[i]->keyvalues["classname"] == "info_node") {
			ents[i]->keyvalues["classname"] = "info_bode";
		}

		Keyvalue keyvalue("origin", ents[i]->keyvalues["origin"]);
		vec3 ori = keyvalue.getVector() + offset;

		ents[i]->keyvalues["origin"] = ori.toKeyvalueString();

		print_move_progress();
	}

	update_ent_lump();
	
	for (int i = 0; i < modelCount; i++) {
		BSPMODEL& model = models[i];

		if (!modelHasOrigin[i]) {
			model.nMins += offset;
			model.nMaxs += offset;

			if (fabs(model.nMins.x) > MAX_MAP_COORD ||
				fabs(model.nMins.y) > MAX_MAP_COORD ||
				fabs(model.nMins.z) > MAX_MAP_COORD ||
				fabs(model.nMaxs.z) > MAX_MAP_COORD ||
				fabs(model.nMaxs.z) > MAX_MAP_COORD ||
				fabs(model.nMaxs.z) > MAX_MAP_COORD) {
				printf("\nWARNING: Model moved past safe world boundary!\n");
			}
		}
	}

	MOVEINFO shouldBeMoved(this);
	for (int i = 0; i < modelCount; i++) {
		if (!modelHasOrigin[i])
			mark_model_structures(i, &shouldBeMoved);
		print_move_progress();
	}

	for (int i = 0; i < nodeCount; i++) {
		if (!shouldBeMoved.nodes[i]) {
			continue; // don't move submodels with origins
		}

		BSPNODE& node = nodes[i];

		if (fabs((float)node.nMins[0] + offset.x) > MAX_MAP_COORD ||
			fabs((float)node.nMaxs[0] + offset.x) > MAX_MAP_COORD ||
			fabs((float)node.nMins[1] + offset.y) > MAX_MAP_COORD ||
			fabs((float)node.nMaxs[1] + offset.y) > MAX_MAP_COORD ||
			fabs((float)node.nMins[2] + offset.z) > MAX_MAP_COORD ||
			fabs((float)node.nMaxs[2] + offset.z) > MAX_MAP_COORD) {
			printf("\nWARNING: Bounding box for node moved past safe world boundary!\n");
		}
		node.nMins[0] += offset.x;
		node.nMaxs[0] += offset.x;
		node.nMins[1] += offset.y;
		node.nMaxs[1] += offset.y;
		node.nMins[2] += offset.z;
		node.nMaxs[2] += offset.z;
	}

	for (int i = 1; i < leafCount; i++) { // don't move the solid leaf (always has 0 size)
		if (!shouldBeMoved.leaves[i]) {
			continue; // don't move submodels with origins
		}

		BSPLEAF& leaf = leaves[i];

		if (fabs((float)leaf.nMins[0] + offset.x) > MAX_MAP_COORD ||
			fabs((float)leaf.nMaxs[0] + offset.x) > MAX_MAP_COORD ||
			fabs((float)leaf.nMins[1] + offset.y) > MAX_MAP_COORD ||
			fabs((float)leaf.nMaxs[1] + offset.y) > MAX_MAP_COORD ||
			fabs((float)leaf.nMins[2] + offset.z) > MAX_MAP_COORD ||
			fabs((float)leaf.nMaxs[2] + offset.z) > MAX_MAP_COORD) {
			printf("\nWARNING: Bounding box for leaf moved past safe world boundary!\n");
		}
		leaf.nMins[0] += offset.x;
		leaf.nMaxs[0] += offset.x;
		leaf.nMins[1] += offset.y;
		leaf.nMaxs[1] += offset.y;
		leaf.nMins[2] += offset.z;
		leaf.nMaxs[2] += offset.z;
	}

	for (int i = 0; i < vertCount; i++) {
		if (!shouldBeMoved.verts[i]) {
			continue; // don't move submodels with origins
		}

		vec3& vert = verts[i];

		vert += offset;

		if (fabs(vert.x) > MAX_MAP_COORD ||
			fabs(vert.y) > MAX_MAP_COORD ||
			fabs(vert.z) > MAX_MAP_COORD) {
			printf("\nWARNING: Vertex moved past safe world boundary!\n");
		}
	}

	for (int i = 0; i < planeCount; i++) {
		if (!shouldBeMoved.planes[i]) {
			continue; // don't move submodels with origins
		}

		BSPPLANE& plane = planes[i];
		vec3 newPlaneOri = offset + (plane.vNormal * plane.fDist);

		if (fabs(newPlaneOri.x) > MAX_MAP_COORD || fabs(newPlaneOri.y) > MAX_MAP_COORD ||
			fabs(newPlaneOri.z) > MAX_MAP_COORD) {
			printf("\nWARNING: Plane origin moved past safe world boundary!\n");
		}

		// get distance between new plane origin and the origin-aligned plane
		plane.fDist = dotProduct(plane.vNormal, newPlaneOri) / dotProduct(plane.vNormal, plane.vNormal);
	}

	uint32_t texCount = (uint32_t)(lumps[LUMP_TEXTURES])[0];
	byte* textures = lumps[LUMP_TEXTURES];
	vec3 offsetDir = offset.normalize();
	float offsetLen = offset.length();

	for (int i = 0; i < texInfoCount; i++) {
		if (!shouldBeMoved.texInfo[i]) {
			continue; // don't move submodels with origins
		}

		BSPTEXTUREINFO& info = texInfo[i];

		int32_t texOffset = ((int32_t*)textures)[info.iMiptex + 1];
		BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));

		float scaleS = info.vS.length();
		float scaleT = info.vT.length();
		vec3 nS = info.vS.normalize();
		vec3 nT = info.vT.normalize();

		vec3 newOriS = offset + (nS * info.shiftS);
		vec3 newOriT = offset + (nT * info.shiftT);

		float shiftScaleS = dotProduct(offsetDir, nS);
		float shiftScaleT = dotProduct(offsetDir, nT);

		int olds = info.shiftS;
		int oldt = info.shiftT;

		float shiftAmountS = shiftScaleS * offsetLen * scaleS;
		float shiftAmountT = shiftScaleT * offsetLen * scaleT;

		info.shiftS -= shiftAmountS;
		info.shiftT -= shiftAmountT;

		// minimize shift values (just to be safe. floats can be p wacky and zany)
		while (fabs(info.shiftS) > tex.nWidth) {
			info.shiftS += (info.shiftS < 0) ? (int)tex.nWidth : -(int)(tex.nWidth);
		}
		while (fabs(info.shiftT) > tex.nHeight) {
			info.shiftT += (info.shiftT < 0) ? (int)tex.nHeight : -(int)(tex.nHeight);
		}
	}

	if (hasLighting) {
		resize_lightmaps(oldLightmaps, newLightmaps);
	}

	delete[] oldLightmaps;
	delete[] newLightmaps;

	for (int i = 0; i < 12; i++) printf("\b\b\b\b");
	for (int i = 0; i < 12; i++) printf("    ");
	for (int i = 0; i < 12; i++) printf("\b\b\b\b");

	return true;
}

void Bsp::resize_lightmaps(LIGHTMAP* oldLightmaps, LIGHTMAP* newLightmaps) {
	BSPTEXTUREINFO* texInfo = (BSPTEXTUREINFO*)lumps[LUMP_TEXINFO];
	BSPFACE* faces = (BSPFACE*)lumps[LUMP_FACES];
	COLOR3* lightdata = (COLOR3*)lumps[LUMP_LIGHTING];

	int texInfoCount = header.lump[LUMP_TEXINFO].nLength / sizeof(BSPTEXTUREINFO);
	int faceCount = header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);

	uint32_t texCount = (uint32_t)(lumps[LUMP_TEXTURES])[0];
	byte* textures = lumps[LUMP_TEXTURES];

	progress_title = "Recalculate lightmaps";
	progress = 0;
	progress_total = faceCount;

	// calculate new lightmap sizes
	qrad_init_globals(this);
	int newLightDataSz = 0;
	int totalLightmaps = 0;
	int lightmapsResizeCount = 0;
	for (int i = 0; i < faceCount; i++) {
		BSPFACE& face = faces[i];

		print_move_progress();

		if (face.nStyles[0] == 255 || texInfo[face.iTextureInfo].nFlags & TEX_SPECIAL)
			continue;

		BSPTEXTUREINFO& info = texInfo[face.iTextureInfo];
		int32_t texOffset = ((int32_t*)textures)[info.iMiptex + 1];
		BSPMIPTEX& tex = *((BSPMIPTEX*)(textures + texOffset));

		int size[2];
		GetFaceLightmapSize(i, size);

		int lightmapSz = size[0] * size[1];

		newLightmaps[i].width = size[0];
		newLightmaps[i].height = size[1];
		newLightmaps[i].layers = oldLightmaps[i].layers;

		newLightDataSz += (lightmapSz * newLightmaps[i].layers) * sizeof(COLOR3);

		totalLightmaps += newLightmaps[i].layers;
		if (oldLightmaps[i].width != newLightmaps[i].width || oldLightmaps[i].height != newLightmaps[i].height) {
			lightmapsResizeCount += newLightmaps[i].layers;
		}
	}

	if (lightmapsResizeCount > 0) {
		//printf(" %d lightmap(s) to resize", lightmapsResizeCount, totalLightmaps);

		progress_title = "Resize lightmaps";
		progress = 0;
		progress_total = faceCount;

		int newColorCount = newLightDataSz / sizeof(COLOR3);
		COLOR3* newLightData = new COLOR3[newColorCount];
		memset(newLightData, 255, newColorCount * sizeof(COLOR3));
		int lightmapOffset = 0;


		for (int i = 0; i < faceCount; i++) {
			BSPFACE& face = faces[i];

			print_move_progress();

			if (face.nStyles[0] == 255 || texInfo[face.iTextureInfo].nFlags & TEX_SPECIAL) // no lighting
				continue;

			LIGHTMAP& oldLight = oldLightmaps[i];
			LIGHTMAP& newLight = newLightmaps[i];
			int oldLayerSz = (oldLight.width * oldLight.height) * sizeof(COLOR3);
			int newLayerSz = (newLight.width * newLight.height) * sizeof(COLOR3);
			int oldSz = oldLayerSz * oldLight.layers;
			int newSz = newLayerSz * newLight.layers;

			totalLightmaps++;

			if (oldLight.width == newLight.width && oldLight.height == newLight.height) {
				memcpy((byte*)newLightData + lightmapOffset, (byte*)lightdata + face.nLightmapOffset, oldSz);
			}
			else {
				qrad_get_lightmap_flags(this, i, newLight.luxelFlags);

				int maxWidth = min(newLight.width, oldLight.width);
				int maxHeight = min(newLight.height, oldLight.height);

				int srcOffsetX, srcOffsetY;
				get_lightmap_shift(oldLight, newLight, srcOffsetX, srcOffsetY);

				for (int layer = 0; layer < newLight.layers; layer++) {
					int srcOffset = (face.nLightmapOffset + oldLayerSz * layer) / sizeof(COLOR3);
					int dstOffset = (lightmapOffset + newLayerSz * layer) / sizeof(COLOR3);

					int startX = newLight.width > oldLight.width ? -1 : 0;
					int startY = newLight.height > oldLight.height ? -1 : 0;

					for (int y = startY; y < newLight.height; y++) {
						for (int x = startX; x < newLight.width; x++) {
							int offsetX = x + srcOffsetX;
							int offsetY = y + srcOffsetY;

							int srcX = oldLight.width > newLight.width ? offsetX : x;
							int srcY = oldLight.height > newLight.height ? offsetY : y;
							int dstX = newLight.width > oldLight.width ? offsetX : x;
							int dstY = newLight.height > oldLight.height ? offsetY : y;

							srcX = max(0, min(oldLight.width - 1, srcX));
							srcY = max(0, min(oldLight.height - 1, srcY));
							dstX = max(0, min(newLight.width - 1, dstX));
							dstY = max(0, min(newLight.height - 1, dstY));

							COLOR3& src = lightdata[srcOffset + srcY * oldLight.width + srcX];
							COLOR3& dst = newLightData[dstOffset + dstY * newLight.width + dstX];

							dst = src;
						}
					}
				}
			}

			face.nLightmapOffset = lightmapOffset;
			lightmapOffset += newSz;
		}

		delete[] this->lumps[LUMP_LIGHTING];
		this->lumps[LUMP_LIGHTING] = (byte*)newLightData;
		header.lump[LUMP_LIGHTING].nLength = lightmapOffset;
	}
}

void Bsp::split_shared_model_structures() {
	BSPMODEL* models = (BSPMODEL*)lumps[LUMP_MODELS];
	int modelCount = header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL);

	bool* modelHasOrigin = new bool[modelCount];
	memset(modelHasOrigin, 0, modelCount * sizeof(bool));

	for (int i = 0; i < ents.size(); i++) {
		if (ents[i]->hasKey("origin") && ents[i]->isBspModel()) {
			modelHasOrigin[ents[i]->getBspModelIdx()] = true;
		}
	}

	// marks which structures should be/not be moved
	MOVEINFO shouldNotMove(this);

	int32_t* surfEdges = (int32_t*)lumps[LUMP_SURFEDGES];
	BSPEDGE* edges = (BSPEDGE*)lumps[LUMP_EDGES];

	progress_title = "Split model structures";
	progress = 0;
	progress_total = modelCount*2;

	for (int i = 0; i < modelCount; i++) {
		if (modelHasOrigin[i])
			mark_model_structures(i, &shouldNotMove);
		print_move_progress();
	}

	int duplicateCount = 0;
	for (int i = 0; i < modelCount; i++) {
		BSPMODEL& model = models[i];

		if (!modelHasOrigin[i]) {
			duplicateCount += remap_shared_model_structures(i, &shouldNotMove);
		}

		print_move_progress();
	}

	//if (duplicateCount)
	//	printf("\nDuplicated %d shared model planes to allow independent movement\n", duplicateCount);

	delete[] modelHasOrigin;
}

int Bsp::remap_shared_model_structures(int modelIdx, MOVEINFO* doNotMoveLists) {
	BSPMODEL& model = ((BSPMODEL*)lumps[LUMP_MODELS])[modelIdx];

	MOVEINFO stuffToMove(this);
	mark_model_structures(modelIdx, &stuffToMove);

	BSPPLANE* planes = (BSPPLANE*)lumps[LUMP_PLANES];
	BSPTEXTUREINFO* texInfo = (BSPTEXTUREINFO*)lumps[LUMP_TEXINFO];
	BSPLEAF* leaves = (BSPLEAF*)lumps[LUMP_LEAVES];
	BSPNODE* nodes = (BSPNODE*)lumps[LUMP_NODES];
	BSPFACE* faces = (BSPFACE*)lumps[LUMP_FACES];
	vec3* verts = (vec3*)lumps[LUMP_VERTICES];

	int planeCount = header.lump[LUMP_PLANES].nLength / sizeof(BSPPLANE);
	int texInfoCount = header.lump[LUMP_TEXINFO].nLength / sizeof(BSPTEXTUREINFO);
	int leafCount = header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF);
	int nodeCount = header.lump[LUMP_NODES].nLength / sizeof(BSPNODE);
	int vertCount = header.lump[LUMP_VERTICES].nLength / sizeof(vec3);
	int faceCount = header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);

	REMAPINFO remappedStuff(this);

	// TODO: handle all of these, assuming it's possible these are ever shared
	for (int i = 0; i < doNotMoveLists->clipnodeCount; i++) {
		if (stuffToMove.clipnodes[i] && doNotMoveLists->clipnodes[i]) {
			printf("\nError: clipnode shared with models of different origin types. Something will break.\n");
			break;
		}
	}
	for (int i = 1; i < doNotMoveLists->leafCount; i++) { // skip solid leaf - it doesn't matter
		if (stuffToMove.leaves[i] && doNotMoveLists->leaves[i]) {
			printf("\nError: leaf shared with models of different origin types. Something will break.\n");
			break;
		}
	}
	for (int i = 0; i < doNotMoveLists->nodeCount; i++) {
		if (stuffToMove.nodes[i] && doNotMoveLists->nodes[i]) {
			printf("\nError: leaf shared with models of different origin types. Something will break.\n");
			break;
		}
	}
	for (int i = 0; i < doNotMoveLists->texInfoCount; i++) {
		if (stuffToMove.texInfo[i] && doNotMoveLists->texInfo[i]) {
			printf("\nError: texinfo shared with models of different origin types. Something will break.\n");
			break;
		}
	}
	for (int i = 0; i < doNotMoveLists->vertCount; i++) {
		if (stuffToMove.verts[i] && doNotMoveLists->verts[i]) {
			printf("\nError: vertex shared with models of different origin types. Something will break.\n");
			break;
		}
	}

	int duplicateCount = 0;
	for (int i = 0; i < doNotMoveLists->planeCount; i++) {
		if (stuffToMove.planes[i] && doNotMoveLists->planes[i]) {
			duplicateCount++;
		}
	}
	int newPlaneCount = planeCount + duplicateCount;

	BSPPLANE* newPlanes = new BSPPLANE[newPlaneCount];
	memcpy(newPlanes, planes, planeCount *sizeof(BSPPLANE));

	int addIdx = planeCount;
	for (int i = 0; i < doNotMoveLists->planeCount; i++) {
		if (stuffToMove.planes[i] && doNotMoveLists->planes[i]) {
			// TODO: Check if there's an existing plane that can be used that is not in the do-not-move list
			newPlanes[addIdx] = planes[i];
			remappedStuff.planes[i] = addIdx;
			addIdx++;
		}
		else {
			remappedStuff.planes[i] = i;
		}
	}

	delete lumps[LUMP_PLANES];
	lumps[LUMP_PLANES] = (byte*)newPlanes;
	header.lump[LUMP_PLANES].nLength = newPlaneCount * sizeof(BSPPLANE);

	remap_model_structures(modelIdx, &remappedStuff);

	return duplicateCount;
}

void Bsp::get_lightmap_shift(const LIGHTMAP& oldLightmap, const LIGHTMAP& newLightmap, int& srcOffsetX, int& srcOffsetY) {
	int minWidth = min(newLightmap.width, oldLightmap.width);
	int minHeight = min(newLightmap.height, oldLightmap.height);

	int bestMatch = 0;
	int bestShiftCombo = 0;
	
	// Try different combinations of shifts to find the best alignment of the lightmaps.
	// Example (2 = unlit, 3 = lit)
	//  old         new
	// 3 3 3      2 3 3 3
	// 3 3 3  ->  2 3 3 3  =  old lightmap matches more luxels when it's shifted right 1 pixel in the new lightmap
	// 3 3 3      2 3 3 3
	// Only works for lightmap resizes caused by precision errors. Faces that are actually different sizes will
	// likely have more than 1 pixel of difference in either dimension.
	for (int t = 0; t < 4; t++) {
		int numMatch = 0;
		for (int y = 0; y < minHeight; y++) {
			for (int x = 0; x < minWidth; x++) {
				int offsetX = x;
				int offsetY = y;

				if (t == 1) {
					offsetX = x + 1;
				}
				if (t == 2) {
					offsetY = y + 1;
				}
				if (t == 3) {
					offsetX = x + 1;
					offsetY = y + 1;
				}

				int srcX = oldLightmap.width > newLightmap.width ? offsetX : x;
				int srcY = oldLightmap.height > newLightmap.height ? offsetY : y;
				int dstX = newLightmap.width > oldLightmap.width ? offsetX : x;
				int dstY = newLightmap.height > oldLightmap.height ? offsetY : y;

				srcX = max(0, min(oldLightmap.width - 1, srcX));
				srcY = max(0, min(oldLightmap.height - 1, srcY));
				dstX = max(0, min(newLightmap.width - 1, dstX));
				dstY = max(0, min(newLightmap.height - 1, dstY));

				int oldLuxelFlag = oldLightmap.luxelFlags[srcY * oldLightmap.width + srcX];
				int newLuxelFlag = newLightmap.luxelFlags[dstY * newLightmap.width + dstX];

				if (oldLuxelFlag == newLuxelFlag) {
					numMatch += 1;
				}
			}
		}

		if (numMatch > bestMatch) {
			bestMatch = numMatch;
			bestShiftCombo = t;
		}
	}

	int shouldShiftLeft = bestShiftCombo == 1 || bestShiftCombo == 3;
	int shouldShiftTop = bestShiftCombo == 2 || bestShiftCombo == 3;

	srcOffsetX = newLightmap.width != oldLightmap.width ? shouldShiftLeft : 0;
	srcOffsetY = newLightmap.height != oldLightmap.height ? shouldShiftTop : 0;
}

void Bsp::update_ent_lump() {
	stringstream ent_data;

	for (int i = 0; i < ents.size(); i++) {
		ent_data << "{\n";

		for (int k = 0; k < ents[i]->keyOrder.size(); k++) {
			string key = ents[i]->keyOrder[k];
			ent_data << "\"" << key << "\" \"" << ents[i]->keyvalues[key] << "\"\n";
		}

		ent_data << "}";
		if (i < ents.size() - 1) {
			ent_data << "\n"; // trailing newline crashes sven, and only sven, and only sometimes
		}
	}

	string str_data = ent_data.str();

	delete[] lumps[LUMP_ENTITIES];
	header.lump[LUMP_ENTITIES].nLength = str_data.size()+1;
	lumps[LUMP_ENTITIES] = new byte[str_data.size()+1];
	memcpy((char*)lumps[LUMP_ENTITIES], str_data.c_str(), str_data.size());
	lumps[LUMP_ENTITIES][str_data.size()] = 0; // null terminator required too(?)
}

vec3 Bsp::get_model_center(int modelIdx) {
	if (modelIdx < 0 || modelIdx > header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL)) {
		printf("Invalid model index %d. Must be 0 - %d\n", modelIdx);
		return vec3();
	}

	BSPMODEL& model = ((BSPMODEL*)lumps[LUMP_MODELS])[modelIdx];

	return model.nMins + (model.nMaxs - model.nMins) * 0.5f;
}

void Bsp::write(string path) {
	if (path.rfind(".bsp") != path.size() - 4) {
		path = path + ".bsp";
	}

	cout << "Writing " << path << endl;

	// calculate lump offsets
	int offset = sizeof(BSPHEADER);
	for (int i = 0; i < HEADER_LUMPS; i++) {
		header.lump[i].nOffset = offset;
		offset += header.lump[i].nLength;
	}

	ofstream file(path, ios::out | ios::binary | ios::trunc);


	file.write((char*)&header, sizeof(BSPHEADER));

	// write the lumps
	for (int i = 0; i < HEADER_LUMPS; i++) {
		file.write((char*)lumps[i], header.lump[i].nLength);
	}
}

bool Bsp::load_lumps(string fpath)
{
	bool valid = true;

	// Read all BSP Data
	ifstream fin(fpath, ios::binary | ios::ate);
	int size = fin.tellg();
	fin.seekg(0, fin.beg);

	if (size < sizeof(BSPHEADER) + sizeof(BSPLUMP)*HEADER_LUMPS)
		return false;

	fin.read((char*)&header.nVersion, sizeof(int));
	
	for (int i = 0; i < HEADER_LUMPS; i++)
		fin.read((char*)&header.lump[i], sizeof(BSPLUMP));

	lumps = new byte*[HEADER_LUMPS];
	memset(lumps, 0, sizeof(byte*)*HEADER_LUMPS);
	
	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		if (header.lump[i].nLength == 0) {
			lumps[i] = NULL;
			continue;
		}

		fin.seekg(header.lump[i].nOffset);
		if (fin.eof()) {
			cout << "FAILED TO READ BSP LUMP " + to_string(i) + "\n";
			valid = false;
		}
		else
		{
			lumps[i] = new byte[header.lump[i].nLength];
			fin.read((char*)lumps[i], header.lump[i].nLength);
		}
	}	
	
	fin.close();

	return valid;
}

void Bsp::load_ents()
{
	for (int i = 0; i < ents.size(); i++)
		delete ents[i];
	ents.clear();

	bool verbose = true;
	membuf sbuf((char*)lumps[LUMP_ENTITIES], header.lump[LUMP_ENTITIES].nLength);
	istream in(&sbuf);

	int lineNum = 0;
	int lastBracket = -1;
	Entity* ent = NULL;

	string line = "";
	while (getline(in, line))
	{
		lineNum++;
		if (line.length() < 1 || line[0] == '\n')
			continue;

		if (line[0] == '{')
		{
			if (lastBracket == 0)
			{
				cout << path + ".bsp ent data (line " + to_string(lineNum) + "): Unexpected '{'\n";
				continue;
			}
			lastBracket = 0;

			if (ent != NULL)
				delete ent;
			ent = new Entity();
		}
		else if (line[0] == '}')
		{
			if (lastBracket == 1)
				cout << path + ".bsp ent data (line " + to_string(lineNum) + "): Unexpected '}'\n";
			lastBracket = 1;

			if (ent == NULL)
				continue;

			ents.push_back(ent);
			ent = NULL;

			// you can end/start an ent on the same line, you know
			if (line.find("{") != string::npos)
			{
				ent = new Entity();
				lastBracket = 0;
			}
		}
		else if (lastBracket == 0 && ent != NULL) // currently defining an entity
		{
			Keyvalue k(line);
			if (k.key.length() && k.value.length())
				ent->addKeyvalue(k);
		}
	}	
	//cout << "got " << ents.size() <<  " entities\n";

	if (ent != NULL)
		delete ent;
}

void Bsp::print_stat(string name, uint val, uint max, bool isMem) {
	const float meg = 1024 * 1024;
	float percent = (val / (float)max) * 100;

	if (val > max) {
		print_color(PRINT_RED | PRINT_BRIGHT);
	}
	else if (percent >= 90) {
		print_color(PRINT_RED | PRINT_GREEN | PRINT_BRIGHT);
	}
	else if (percent >= 75) {
		print_color(PRINT_RED | PRINT_GREEN | PRINT_BLUE | PRINT_BRIGHT);
	}
	else {
		print_color(PRINT_RED | PRINT_GREEN | PRINT_BLUE);
	}

	printf("%-12s  ", name.c_str());
	if (isMem) {
		printf("%8.1f / %-5.1f MB", val/meg, max/meg);
	}
	else {
		printf("%8u / %-8u", val, max);
	}
	printf("  %6.1f%%", percent);

	if (val > max) {
		printf("  (OVERFLOW!!!)");
	}

	printf("\n");

	print_color(PRINT_RED | PRINT_GREEN | PRINT_BLUE);
}

void Bsp::print_model_stat(MOVEINFO* modelInfo, uint val, uint max, bool isMem)
{
	string classname = modelInfo->modelIdx == 0 ? "worldspawn" : "???";
	string targetname = modelInfo->modelIdx == 0 ? "" : "???";
	for (int k = 0; k < ents.size(); k++) {
		if (ents[k]->getBspModelIdx() == modelInfo->modelIdx) {
			targetname = ents[k]->keyvalues["targetname"];
			classname = ents[k]->keyvalues["classname"];
		}
	}

	const float meg = 1024 * 1024;
	float percent = (val / (float)max) * 100;

	if (isMem) {
		printf("%8.1f / %-5.1f MB", val / meg, max / meg);
	}
	else {
		printf("%-26s %-26s *%-6d %9d", classname.c_str(), targetname.c_str(), modelInfo->modelIdx, val);
	}
	if (percent >= 0.1f)
		printf("  %6.1f%%", percent);

	printf("\n");
}

int g_sort_mode = SORT_CLIPNODES;

bool sortModelInfos(const MOVEINFO* a, const MOVEINFO* b) {
	switch (g_sort_mode) {
	case SORT_VERTS:
		return a->vertSum > b->vertSum;
	case SORT_NODES:
		return a->nodeSum > b->nodeSum;
	case SORT_CLIPNODES:
		return a->clipnodeSum > b->clipnodeSum;
	case SORT_FACES:
		return a->faceSum > b->faceSum;
	}
	
}

void Bsp::print_info(bool perModelStats, int perModelLimit, int sortMode) {
	int planeCount = header.lump[LUMP_PLANES].nLength / sizeof(BSPPLANE);
	int texInfoCount = header.lump[LUMP_TEXINFO].nLength / sizeof(BSPTEXTUREINFO);
	int leafCount = header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF);
	int modelCount = header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL);
	int nodeCount = header.lump[LUMP_NODES].nLength / sizeof(BSPNODE);
	int vertCount = header.lump[LUMP_VERTICES].nLength / sizeof(vec3);
	int faceCount = header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);
	int clipnodeCount = header.lump[LUMP_CLIPNODES].nLength / sizeof(BSPCLIPNODE);
	int marksurfacesCount = header.lump[LUMP_MARKSURFACES].nLength / sizeof(uint16_t);
	int surfedgeCount = header.lump[LUMP_SURFEDGES].nLength / sizeof(int32_t);
	int edgeCount = header.lump[LUMP_EDGES].nLength / sizeof(BSPEDGE);
	int textureCount = *((int32_t*)(lumps[LUMP_TEXTURES]));
	int lightDataLength = header.lump[LUMP_LIGHTING].nLength;
	int visDataLength = header.lump[LUMP_VISIBILITY].nLength;
	int entCount = ents.size();

	if (perModelStats) {
		g_sort_mode = sortMode;

		if (planeCount >= MAX_MAP_PLANES || texInfoCount >= MAX_MAP_TEXINFOS || leafCount >= MAX_MAP_LEAVES ||
			modelCount >= MAX_MAP_MODELS || nodeCount >= MAX_MAP_NODES || vertCount >= MAX_MAP_VERTS ||
			faceCount >= MAX_MAP_FACES || clipnodeCount >= MAX_MAP_CLIPNODES || marksurfacesCount >= MAX_MAP_MARKSURFS ||
			surfedgeCount >= MAX_MAP_SURFEDGES || edgeCount >= MAX_MAP_EDGES || textureCount >= MAX_MAP_TEXTURES ||
			lightDataLength >= MAX_MAP_LIGHTDATA || visDataLength >= MAX_MAP_VISDATA) 
		{
			printf("Unable to show model stats while BSP limits are exceeded.\n");
			return;
		}

		vector<MOVEINFO*> modelStructs;
		modelStructs.resize(modelCount);
		
		for (int i = 0; i < modelCount; i++) {
			modelStructs[i] = new MOVEINFO(this);
			modelStructs[i]->modelIdx = i;
			mark_model_structures(i, modelStructs[i]);
			modelStructs[i]->compute_sums();
		}

		int maxCount;
		char* countName;

		switch (g_sort_mode) {
		case SORT_VERTS:		maxCount = vertCount; countName = "  Verts";  break;
		case SORT_NODES:		maxCount = nodeCount; countName = "  Nodes";  break;
		case SORT_CLIPNODES:	maxCount = clipnodeCount; countName = "Clipnodes";  break;
		case SORT_FACES:		maxCount = faceCount; countName = "  Faces";  break;
		}

		sort(modelStructs.begin(), modelStructs.end(), sortModelInfos);
		printf("       Classname                  Targetname          Model  %-10s  Usage\n", countName);
		printf("-------------------------  -------------------------  -----  ----------  --------\n");

		for (int i = 0; i < modelCount && i < perModelLimit; i++) {

			int val;
			switch (g_sort_mode) {
			case SORT_VERTS:		val = modelStructs[i]->vertSum; break;
			case SORT_NODES:		val = modelStructs[i]->nodeSum; break;
			case SORT_CLIPNODES:	val = modelStructs[i]->clipnodeSum; break;
			case SORT_FACES:		val = modelStructs[i]->faceSum; break;
			}

			if (val == 0)
				break;

			print_model_stat(modelStructs[i], val, maxCount, false);
		}
	}
	else {
		printf(" Data Type     Current / Max       Fullness\n");
		printf("------------  -------------------  --------\n");
		print_stat("models", modelCount, MAX_MAP_MODELS, false);
		print_stat("planes", planeCount, MAX_MAP_PLANES, false);
		print_stat("vertexes", vertCount, MAX_MAP_VERTS, false);
		print_stat("nodes", nodeCount, MAX_MAP_NODES, false);
		print_stat("texinfos", texInfoCount, MAX_MAP_TEXINFOS, false);
		print_stat("faces", faceCount, MAX_MAP_FACES, false);
		print_stat("clipnodes", clipnodeCount, MAX_MAP_CLIPNODES, false);
		print_stat("leaves", leafCount, MAX_MAP_LEAVES, false);
		print_stat("marksurfaces", marksurfacesCount, MAX_MAP_MARKSURFS, false);
		print_stat("surfedges", surfedgeCount, MAX_MAP_SURFEDGES, false);
		print_stat("edges", edgeCount, MAX_MAP_SURFEDGES, false);
		print_stat("textures", textureCount, MAX_MAP_TEXTURES, false);
		print_stat("lightdata", lightDataLength, MAX_MAP_LIGHTDATA, true);
		print_stat("visdata", visDataLength, MAX_MAP_VISDATA, true);
		print_stat("entities", entCount, MAX_MAP_ENTS, false);
	}
}

void Bsp::print_model_bsp(int modelIdx) {
	BSPMODEL* models = (BSPMODEL*)lumps[LUMP_MODELS];

	int node = models[modelIdx].iHeadnodes[0];
	recurse_node(node, 0);
}

void Bsp::print_clipnode_tree(int iNode, int depth) {
	BSPCLIPNODE* clipnodes = (BSPCLIPNODE*)lumps[LUMP_CLIPNODES];

	for (int i = 0; i < depth; i++) {
		cout << "    ";
	}

	if (iNode < 0) {
		print_contents(iNode);
		printf("\n");
		return;
	}
	else {
		BSPPLANE* planes = (BSPPLANE*)lumps[LUMP_PLANES];
		BSPPLANE& plane = planes[clipnodes[iNode].iPlane];
		printf("NODE (%.2f, %.2f, %.2f) @ %.2f\n", plane.vNormal.x, plane.vNormal.y, plane.vNormal.z, plane.fDist);
	}
	

	for (int i = 0; i < 2; i++) {
		print_clipnode_tree(clipnodes[iNode].iChildren[i], depth+1);
	}
}

void Bsp::print_model_hull(int modelIdx, int hull_number) {
	if (modelIdx < 0 || modelIdx > header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL)) {
		printf("Invalid model index %d. Must be 0 - %d\n", modelIdx);
		return;
	}

	// the first hull is used for point-sized clipping, but uses nodes and not clipnodes.
	if (hull_number < 0 || hull_number >= MAX_MAP_HULLS) {
		printf("Invalid hull number. Clipnode hull numbers are 0 - %d\n", MAX_MAP_HULLS);
		return;
	}

	BSPMODEL& model = ((BSPMODEL*)lumps[LUMP_MODELS])[modelIdx];

	printf("Model %d Hull %d - %s\n", modelIdx, hull_number, get_model_usage(modelIdx).c_str());

	if (hull_number == 0)
		print_model_bsp(modelIdx);
	else
		print_clipnode_tree(model.iHeadnodes[hull_number], 0);
}

string Bsp::get_model_usage(int modelIdx) {
	for (int i = 0; i < ents.size(); i++) {
		if (ents[i]->getBspModelIdx() == modelIdx) {
			return "\"" + ents[i]->keyvalues["targetname"] + "\" (" + ents[i]->keyvalues["classname"] + ")";
		}
	}
	return "(unused)";
}

void Bsp::recurse_node(int16_t nodeIdx, int depth) {

	BSPNODE* nodes = (BSPNODE*)lumps[LUMP_NODES];

	for (int i = 0; i < depth; i++) {
		cout << "    ";
	}

	if (nodeIdx < 0) {
		BSPLEAF* leaves = (BSPLEAF * )lumps[LUMP_LEAVES];
		BSPLEAF leaf = leaves[~nodeIdx];
		print_leaf(leaf);
		cout << " (LEAF " << ~nodeIdx << ")" << endl;
		return;
	}
	else {
		print_node(nodes[nodeIdx]);
		cout << endl;
	}
	
	recurse_node(nodes[nodeIdx].iChildren[0], depth+1);
	recurse_node(nodes[nodeIdx].iChildren[1], depth+1);
}

void Bsp::print_node(BSPNODE node) {
	BSPPLANE* planes = (BSPPLANE * )lumps[LUMP_PLANES];
	BSPPLANE plane = planes[node.iPlane];

	cout << "Plane (" << plane.vNormal.x << " " << plane.vNormal.y << " " << plane.vNormal.z << ") d: " << plane.fDist;
}

int Bsp::pointContents(int iNode, vec3 p)
{
	BSPNODE* nodes = (BSPNODE*)lumps[LUMP_NODES];
	BSPPLANE* planes = (BSPPLANE*)lumps[LUMP_PLANES];
	BSPLEAF* leaves = (BSPLEAF*)lumps[LUMP_LEAVES];

	float       d;
	BSPNODE* node;
	BSPPLANE* plane;

	while (iNode >= 0)
	{
		node = &nodes[iNode];
		plane = &planes[node->iPlane];

		d = dotProduct(plane->vNormal, p) - plane->fDist;
		if (d < 0)
			iNode = node->iChildren[1];
		else
			iNode = node->iChildren[0];
	}

	cout << "Contents at " << p.x << " " << p.y << " " << p.z << " is ";
	print_leaf(leaves[~iNode]);
	cout << " (LEAF " << ~iNode << ")\n";

	return leaves[~iNode].nContents;
}

void Bsp::mark_clipnodes(int iNode, bool* markList) {
	BSPCLIPNODE* clipnodes = (BSPCLIPNODE*)lumps[LUMP_CLIPNODES];

	markList[iNode] = true;

	for (int i = 0; i < 2; i++) {
		if (clipnodes[iNode].iChildren[i] >= 0) {
			mark_clipnodes(clipnodes[iNode].iChildren[i], markList);
		}
	}
}

void Bsp::mark_node_structures(int iNode, MOVEINFO* markList) {
	BSPNODE* nodes = (BSPNODE*)lumps[LUMP_NODES];
	BSPNODE& node = nodes[iNode];

	markList->nodes[iNode] = true;
	markList->planes[node.iPlane] = true;
	
	BSPEDGE* edges = (BSPEDGE*)lumps[LUMP_EDGES];
	BSPFACE* faces = (BSPFACE*)lumps[LUMP_FACES];
	int32_t* surfEdges = (int32_t*)lumps[LUMP_SURFEDGES];
	vec3* verts = (vec3*)lumps[LUMP_VERTICES];

	for (int i = 0; i < node.nFaces; i++) {
		BSPFACE& face = faces[node.firstFace + i];
		markList->faces[node.firstFace + i] = true;
		
		for (int e = 0; e < face.nEdges; e++) {
			int32_t edgeIdx = surfEdges[face.iFirstEdge + e];
			BSPEDGE& edge = edges[abs(edgeIdx)];
			int vertIdx = edgeIdx >= 0 ? edge.iVertex[1] : edge.iVertex[0];
			markList->verts[vertIdx] = true;
		}

		markList->texInfo[face.iTextureInfo] = true;
		markList->planes[face.iPlane] = true;
	}

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			mark_node_structures(node.iChildren[i], markList);
		}
		else {
			markList->leaves[~node.iChildren[i]] = true;
		}
	}
}

void Bsp::mark_clipnode_structures(int iNode, MOVEINFO* markList) {
	BSPCLIPNODE* clipnodes = (BSPCLIPNODE*)lumps[LUMP_CLIPNODES];
	BSPCLIPNODE& node = clipnodes[iNode];

	markList->clipnodes[iNode] = true;
	markList->planes[node.iPlane] = true;

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			mark_clipnode_structures(node.iChildren[i], markList);
		}
	}
}

void Bsp::mark_model_structures(int modelIdx, MOVEINFO* moveInfo) {
	BSPCLIPNODE* clipnodes = (BSPCLIPNODE*)lumps[LUMP_CLIPNODES];
	int numClipnodes = header.lump[LUMP_CLIPNODES].nLength / sizeof(BSPCLIPNODE);

	BSPMODEL& model = ((BSPMODEL*)lumps[LUMP_MODELS])[modelIdx];

	mark_node_structures(model.iHeadnodes[0], moveInfo);
	for (int k = 1; k < MAX_MAP_HULLS; k++) {
		if (model.iHeadnodes[k] >= 0 && model.iHeadnodes[k] < numClipnodes)
			mark_clipnode_structures(model.iHeadnodes[k], moveInfo);
	}
}

void Bsp::remap_node_structures(int iNode, REMAPINFO* remapInfo) {
	BSPNODE* nodes = (BSPNODE*)lumps[LUMP_NODES];
	BSPEDGE* edges = (BSPEDGE*)lumps[LUMP_EDGES];
	BSPFACE* faces = (BSPFACE*)lumps[LUMP_FACES];
	int32_t* surfEdges = (int32_t*)lumps[LUMP_SURFEDGES];
	vec3* verts = (vec3*)lumps[LUMP_VERTICES];

	BSPNODE& node = nodes[iNode];

	node.iPlane = remapInfo->planes[node.iPlane];

	for (int i = 0; i < node.nFaces; i++) {
		BSPFACE& face = faces[node.firstFace + i];
		face.iPlane = remapInfo->planes[face.iPlane];
	}

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			remap_node_structures(node.iChildren[i], remapInfo);
		}
	}
}

void Bsp::remap_clipnode_structures(int iNode, REMAPINFO* remapInfo) {
	BSPCLIPNODE* clipnodes = (BSPCLIPNODE*)lumps[LUMP_CLIPNODES];
	int numClipnodes = header.lump[LUMP_CLIPNODES].nLength / sizeof(BSPCLIPNODE);
	BSPCLIPNODE& node = clipnodes[iNode];

	remapInfo->visitedClipnodes[iNode] = true;

	node.iPlane = remapInfo->planes[node.iPlane];

	for (int i = 0; i < 2; i++) {
		if (node.iChildren[i] >= 0) {
			if (!remapInfo->visitedClipnodes[iNode])
				remap_clipnode_structures(clipnodes[iNode].iChildren[i], remapInfo);
		}
	}
}

void Bsp::remap_model_structures(int modelIdx, REMAPINFO* remapInfo) {
	BSPCLIPNODE* clipnodes = (BSPCLIPNODE*)lumps[LUMP_CLIPNODES];
	int numClipnodes = header.lump[LUMP_CLIPNODES].nLength / sizeof(BSPCLIPNODE);

	BSPMODEL& model = ((BSPMODEL*)lumps[LUMP_MODELS])[modelIdx];

	remap_node_structures(model.iHeadnodes[0], remapInfo);
	for (int k = 1; k < MAX_MAP_HULLS; k++) {
		if (model.iHeadnodes[k] >= 0 && model.iHeadnodes[k] < numClipnodes)
			remap_clipnode_structures(model.iHeadnodes[k], remapInfo);
	}
}

int Bsp::strip_clipping_hull(int hull_number) {
	int modelCount = header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL);

	int removed = 0;
	for (int i = 0; i < modelCount; i++) {
		removed += strip_clipping_hull(hull_number, i, true);
	}

	return removed;
}

int Bsp::strip_clipping_hull(int hull_number, int modelIdx, bool ignoreSharedIfSameHull) {
	if (modelIdx < 0 || modelIdx > header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL)) {
		printf("Invalid model index %d. Must be 0 - %d\n", modelIdx);
		return 0;
	}

	// the first hull is used for point-sized clipping, but uses nodes and not clipnodes.
	if (hull_number < 1 || hull_number >= MAX_MAP_HULLS) {
		printf("Invalid hull number. Clipnode hull numbers are 1 - %d\n", MAX_MAP_HULLS);
		return 0;
	}

	BSPMODEL* models = (BSPMODEL*)lumps[LUMP_MODELS];
	int thisModelCount = header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL);

	BSPMODEL& model = ((BSPMODEL*)lumps[LUMP_MODELS])[modelIdx];

	BSPCLIPNODE* clipnodes = (BSPCLIPNODE*)lumps[LUMP_CLIPNODES];
	int numClipnodes = header.lump[LUMP_CLIPNODES].nLength / sizeof(BSPCLIPNODE);
	int* newClipnodeIndex = new int[numClipnodes];

	MOVEINFO shouldNotDelete(this);
	MOVEINFO shouldDelete(this);
	for (int i = 0; i < thisModelCount; i++) {
		for (int k = 1; k < MAX_MAP_HULLS; k++) {
			if (i != modelIdx && k == hull_number && ignoreSharedIfSameHull) {
				continue;
			}
			MOVEINFO* info = (i == modelIdx && k == hull_number) ? &shouldDelete : &shouldNotDelete;
			if (models[i].iHeadnodes[k] >= 0 && models[i].iHeadnodes[k] < numClipnodes)
				mark_clipnode_structures(models[i].iHeadnodes[k], info);
		}
	}

	int sharedNodes = 0;
	int removed = 0;
	for (int i = 0; i < numClipnodes; i++) {
		if (shouldDelete.clipnodes[i]) {
			if (shouldNotDelete.clipnodes[i]) {
				shouldDelete.clipnodes[i] = false;
				sharedNodes++;
			}
			else {
				removed++;
			}
		} 
	}
	//if (sharedNodes)
	//	printf("Not deleting %d clipnodes shared with other models\n", sharedNodes);

	int newNumClipnodes = numClipnodes - removed;
	BSPCLIPNODE* newClipnodes = new BSPCLIPNODE[newNumClipnodes];

	int insertIdx = 0;
	for (int i = 0; i < numClipnodes; i++) {
		if (shouldDelete.clipnodes[i]) {
			newClipnodeIndex[i] = CONTENTS_EMPTY; // indicate it was removed, also disables the hull if set for headnode
		}
		else {
			newClipnodeIndex[i] = insertIdx;
			newClipnodes[insertIdx] = clipnodes[i];
			insertIdx++;
		}
	}

	for (int i = 0; i < newNumClipnodes; i++) {
		for (int k = 0; k < 2; k++) {
			int16_t& child = newClipnodes[i].iChildren[k];
			if (child >= 0) {
				child = newClipnodeIndex[child];
			}
		}
	}

	for (int i = 0; i < thisModelCount; i++) {
		for (int k = 1; k < MAX_MAP_HULLS; k++) {
			int32_t& headnode = models[i].iHeadnodes[k];
			if (headnode >= 0 && headnode < numClipnodes) {
				headnode = newClipnodeIndex[headnode];
			}
		}
	}

	delete[] lumps[LUMP_CLIPNODES];
	delete[] newClipnodeIndex;

	lumps[LUMP_CLIPNODES] = (byte*)newClipnodes;
	header.lump[LUMP_CLIPNODES].nLength = newNumClipnodes * sizeof(BSPCLIPNODE);

	// TODO: Remove unused planes

	return removed;
}

void Bsp::dump_lightmap(int faceIdx, string outputPath)
{
	BSPFACE* faces = (BSPFACE*)lumps[LUMP_FACES];
	int faceCount = header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);
	byte* lightdata = (byte*)lumps[LUMP_LIGHTING];

	BSPFACE& face = faces[faceIdx];

	qrad_init_globals(this);

	int mins[2];
	int extents[2];
	GetFaceExtents(faceIdx, mins, extents);

	int lightmapSz = extents[0] * extents[1];

	lodepng_encode24_file(outputPath.c_str(), lightdata + face.nLightmapOffset, extents[0], extents[1]);
}

void Bsp::dump_lightmap_atlas(string outputPath) {
	BSPFACE* faces = (BSPFACE*)lumps[LUMP_FACES];
	int faceCount = header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);
	byte* lightdata = (byte*)lumps[LUMP_LIGHTING];

	int lightmapWidth = MAX_SURFACE_EXTENT;

	int lightmapsPerDim = ceil(sqrt(faceCount));
	int atlasDim = lightmapsPerDim * lightmapWidth;
	int sz = atlasDim * atlasDim;
	printf("ATLAS SIZE %d x %d (%.2f KB)", lightmapsPerDim, lightmapsPerDim, (sz * sizeof(COLOR3))/1024.0f);

	COLOR3* pngData = new COLOR3[sz];

	memset(pngData, 0, sz * sizeof(COLOR3));

	qrad_init_globals(this);

	for (int i = 0; i < faceCount; i++) {
		BSPFACE& face = faces[i];

		if (face.nStyles[0] == 255)
			continue; // no lighting info

		int atlasX = (i % lightmapsPerDim)*lightmapWidth;
		int atlasY = (i / lightmapsPerDim)*lightmapWidth;

		int size[2];
		GetFaceLightmapSize(i, size);

		int lightmapWidth = size[0];
		int lightmapHeight = size[1];

		for (int y = 0; y < lightmapHeight; y++) {
			for (int x = 0; x < lightmapWidth; x++) {
				int dstX = atlasX + x;
				int dstY = atlasY + y;

				int lightmapOffset = (y * lightmapWidth + x)*sizeof(COLOR3);

				COLOR3* src = (COLOR3*)(lightdata + face.nLightmapOffset + lightmapOffset);

				pngData[dstY * atlasDim + dstX] = *src;
			}
		}
	}

	lodepng_encode24_file(outputPath.c_str(), (byte*)pngData, atlasDim, atlasDim);
}

void Bsp::write_csg_outputs(string path) {
	BSPPLANE* thisPlanes = (BSPPLANE*)lumps[LUMP_PLANES];
	int numPlanes = header.lump[LUMP_PLANES].nLength / sizeof(BSPPLANE);

	// add flipped version of planes since face output files can't specify plane side
	BSPPLANE* newPlanes = new BSPPLANE[numPlanes*2];
	memcpy(newPlanes, thisPlanes, numPlanes * sizeof(BSPPLANE));
	for (int i = 0; i < numPlanes; i++) {
		BSPPLANE flipped = thisPlanes[i];
		flipped.vNormal = { flipped.vNormal.x > 0 ? -flipped.vNormal.x : flipped.vNormal.x,
							flipped.vNormal.y > 0 ? -flipped.vNormal.y : flipped.vNormal.y,
							flipped.vNormal.z > 0 ? -flipped.vNormal.z : flipped.vNormal.z, };
		flipped.fDist = -flipped.fDist;
		newPlanes[numPlanes + i] = flipped;
	}
	delete [] lumps[LUMP_PLANES];
	lumps[LUMP_PLANES] = (byte*)newPlanes;
	numPlanes *= 2;
	header.lump[LUMP_PLANES].nLength = numPlanes * sizeof(BSPPLANE);
	thisPlanes = newPlanes;

	ofstream pln_file(path + name + ".pln", ios::out | ios::binary | ios::trunc);
	for (int i = 0; i < numPlanes; i++) {
		BSPPLANE& p = thisPlanes[i];
		CSGPLANE csgplane = {
			{p.vNormal.x, p.vNormal.y, p.vNormal.z},
			{0,0,0},
			p.fDist,
			p.nType
		};
		pln_file.write((char*)&csgplane, sizeof(CSGPLANE));
	}
	cout << "Wrote " << numPlanes << " planes\n";

	BSPFACE* thisFaces = (BSPFACE*)lumps[LUMP_FACES];
	int thisFaceCount = header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);

	BSPMODEL* models = (BSPMODEL*)lumps[LUMP_MODELS];
	BSPMODEL world = models[0];

	for (int i = 0; i < 4; i++) {
		FILE* polyfile = fopen((path + name + ".p" + to_string(i)).c_str(), "wb");
		write_csg_polys(world.iHeadnodes[i], polyfile, numPlanes/2, i == 0);
		fprintf(polyfile, "-1 -1 -1 -1 -1\n"); // end of file marker (parsing fails without this)
		fclose(polyfile);

		FILE* detailfile = fopen((path + name + ".b" + to_string(i)).c_str(), "wb");
		fprintf(detailfile, "-1\n");
		fclose(detailfile);
	}

	ofstream hsz_file(path + name + ".hsz", ios::out | ios::binary | ios::trunc);
	const char* hullSizes = "0 0 0 0 0 0\n"
							"-16 -16 -36 16 16 36\n"
							"-32 -32 -32 32 32 32\n"
							"-16 -16 -18 16 16 18\n";
	hsz_file.write(hullSizes, strlen(hullSizes));

	ofstream bsp_file(path + name + "_new.bsp", ios::out | ios::binary | ios::trunc);
	// calculate lump offsets
	int offset = sizeof(BSPHEADER);
	for (int i = 0; i < HEADER_LUMPS; i++) {
		header.lump[i].nOffset = offset;
		if (i == LUMP_ENTITIES || i == LUMP_PLANES || i == LUMP_TEXTURES || i == LUMP_TEXINFO) {
			offset += header.lump[i].nLength;
			if (i == LUMP_PLANES) {
				int count = header.lump[i].nLength / sizeof(BSPPLANE);
				cout << "BSP HAS " << count << " PLANES\n";
			}
		}
		else {
			header.lump[i].nLength = 0;
		}
	}
	bsp_file.write((char*)&header, sizeof(BSPHEADER));
	// write the lumps
	for (int i = 0; i < HEADER_LUMPS; i++) {
		bsp_file.write((char*)lumps[i], header.lump[i].nLength);
	}
}

void Bsp::write_csg_polys(int16_t nodeIdx, FILE* polyfile, int flipPlaneSkip, bool debug) {
	BSPNODE* nodes = (BSPNODE*)lumps[LUMP_NODES];

	if (nodeIdx >= 0) {
		write_csg_polys(nodes[nodeIdx].iChildren[0], polyfile, flipPlaneSkip, debug);
		write_csg_polys(nodes[nodeIdx].iChildren[1], polyfile, flipPlaneSkip, debug);
		return;
	}

	BSPLEAF* leaves = (BSPLEAF*)lumps[LUMP_LEAVES];
	BSPLEAF leaf = leaves[~nodeIdx];

	int detaillevel = 0; // no way to know which faces came from a func_detail
	int32_t contents = leaf.nContents;

	uint16* marksurfs = (uint16*)lumps[LUMP_MARKSURFACES];
	int32_t* surfEdges = (int32_t*)lumps[LUMP_SURFEDGES];
	vec3* verts = (vec3*)lumps[LUMP_VERTICES];
	int numVerts = header.lump[LUMP_VERTICES].nLength / sizeof(vec3);
	BSPEDGE* edges = (BSPEDGE*)lumps[LUMP_EDGES];
	BSPFACE* faces = (BSPFACE*)lumps[LUMP_FACES];

	BSPPLANE* planes = (BSPPLANE*)lumps[LUMP_PLANES];

	
	for (int i = leaf.iFirstMarkSurface; i < leaf.iFirstMarkSurface + leaf.nMarkSurfaces; i++) {
		for (int z = 0; z < 2; z++) {
			if (z == 0)
				continue;
			BSPFACE& face = faces[marksurfs[i]];

			bool flipped = (z == 1 || face.nPlaneSide) && !(z == 1 && face.nPlaneSide);

			int iPlane = !flipped ? face.iPlane : face.iPlane + flipPlaneSkip;

			// contents in front of the face
			int faceContents = z == 1 ? leaf.nContents : CONTENTS_SOLID;

			//int texInfo = z == 1 ? face.iTextureInfo : -1;

			if (debug) {
				BSPPLANE plane = planes[iPlane];
				printf("Writing face (%2.0f %2.0f %2.0f) %4.0f  %s\n", 
					plane.vNormal.x, plane.vNormal.y, plane.vNormal.z, plane.fDist,
					(faceContents == CONTENTS_SOLID ? "SOLID" : "EMPTY"));
				if (flipped && false) {
					cout << " (flipped)";
				}
			}

			fprintf(polyfile, "%i %i %i %i %u\n", detaillevel, iPlane, face.iTextureInfo, faceContents, face.nEdges);

			if (flipped) {
				for (int e = (face.iFirstEdge + face.nEdges) - 1; e >= (int)face.iFirstEdge; e--) {
					int32_t edgeIdx = surfEdges[e];
					BSPEDGE& edge = edges[abs(edgeIdx)];
					vec3 v = edgeIdx >= 0 ? verts[edge.iVertex[1]] : verts[edge.iVertex[0]];
					fprintf(polyfile, "%5.8f %5.8f %5.8f\n", v.x, v.y, v.z);
				}
			}
			else {
				for (int e = face.iFirstEdge; e < face.iFirstEdge + face.nEdges; e++) {
					int32_t edgeIdx = surfEdges[e];
					BSPEDGE& edge = edges[abs(edgeIdx)];
					vec3 v = edgeIdx >= 0 ? verts[edge.iVertex[1]] : verts[edge.iVertex[0]];
					fprintf(polyfile, "%5.8f %5.8f %5.8f\n", v.x, v.y, v.z);
				}
			}

			fprintf(polyfile, "\n");
		}
		if (debug)
			printf("\n");
	}
}

void Bsp::print_contents(int contents) {
	switch (contents) {
	case CONTENTS_EMPTY:
		cout << "EMPTY"; break;
	case CONTENTS_SOLID:
		cout << "SOLID"; break;
	case CONTENTS_WATER:
		cout << "WATER"; break;
	case CONTENTS_SLIME:
		cout << "SLIME"; break;
	case CONTENTS_LAVA:
		cout << "LAVA"; break;
	case CONTENTS_SKY:
		cout << "SKY"; break;
	case CONTENTS_ORIGIN:
		cout << "ORIGIN"; break;
	case CONTENTS_CURRENT_0:
		cout << "CURRENT_0"; break;
	case CONTENTS_CURRENT_90:
		cout << "CURRENT_90"; break;
	case CONTENTS_CURRENT_180:
		cout << "CURRENT_180"; break;
	case CONTENTS_CURRENT_270:
		cout << "CURRENT_270"; break;
	case CONTENTS_CURRENT_UP:
		cout << "CURRENT_UP"; break;
	case CONTENTS_CURRENT_DOWN:
		cout << "CURRENT_DOWN"; break;
	case CONTENTS_TRANSLUCENT:
		cout << "TRANSLUCENT"; break;
	default:
		cout << "UNKNOWN"; break;
	}
}

void Bsp::print_leaf(BSPLEAF leaf) {
	print_contents(leaf.nContents);
	cout << " " << leaf.nMarkSurfaces << " surfs";
}

void Bsp::print_move_progress() {
	if (progress++ > 0) {
		auto now = std::chrono::system_clock::now();
		std::chrono::duration<double> delta = now - last_progress;
		if (delta.count() < 0.016) {
			return;
		}
		last_progress = now;
	}

	int percent = (progress / (float)progress_total) * 100;

	for (int i = 0; i < 12; i++) printf("\b\b\b\b");
	printf("        %-32s %2d%%", progress_title, percent);
}