#pragma once
#include <stdint.h>
#include "vectors.h"
#include "bsplimits.h"
#include <vector>

#define BSP_MODEL_BYTES 64 // size of a BSP model in bytes

#define LUMP_ENTITIES      0
#define LUMP_PLANES        1
#define LUMP_TEXTURES      2
#define LUMP_VERTICES      3
#define LUMP_VISIBILITY    4
#define LUMP_NODES         5
#define LUMP_TEXINFO       6
#define LUMP_FACES         7
#define LUMP_LIGHTING      8
#define LUMP_CLIPNODES     9
#define LUMP_LEAVES       10
#define LUMP_MARKSURFACES 11
#define LUMP_EDGES        12
#define LUMP_SURFEDGES    13
#define LUMP_MODELS       14
#define HEADER_LUMPS      15

enum lump_copy_targets {
	ENTITIES = 1,
	PLANES = 2,
	TEXTURES = 4,
	VERTICES = 8,
	VISIBILITY = 16,
	NODES = 32,
	TEXINFO = 64,
	FACES = 128,
	LIGHTING = 256,
	CLIPNODES = 512,
	LEAVES = 1024,
	MARKSURFACES = 2048,
	EDGES = 4096,
	SURFEDGES = 8192,
	MODELS = 16384
};

#define CONTENTS_EMPTY        -1
#define CONTENTS_SOLID        -2
#define CONTENTS_WATER        -3
#define CONTENTS_SLIME        -4
#define CONTENTS_LAVA         -5
#define CONTENTS_SKY          -6
#define CONTENTS_ORIGIN       -7
#define CONTENTS_CLIP         -8
#define CONTENTS_CURRENT_0    -9
#define CONTENTS_CURRENT_90   -10
#define CONTENTS_CURRENT_180  -11
#define CONTENTS_CURRENT_270  -12
#define CONTENTS_CURRENT_UP   -13
#define CONTENTS_CURRENT_DOWN -14
#define CONTENTS_TRANSLUCENT  -15

#define PLANE_X 0     // Plane is perpendicular to given axis
#define PLANE_Y 1
#define PLANE_Z 2
#define PLANE_ANYX 3  // Non-axial plane is snapped to the nearest
#define PLANE_ANYY 4
#define PLANE_ANYZ 5

// maximum x/y hull extent a monster can have before it starts using hull 2
#define MAX_HULL1_EXTENT_MONSTER 18

// maximum x/y hull dimension a pushable can have before it starts using hull 2
#define MAX_HULL1_SIZE_PUSHABLE 34.0f

static const char* g_lump_names[HEADER_LUMPS] = {
	"ENTITIES",
	"PLANES",
	"TEXTURES",
	"VERTICES",
	"VISIBILITY",
	"NODES",
	"TEXINFO",
	"FACES",
	"LIGHTING",
	"CLIPNODES",
	"LEAVES",
	"MARKSURFACES",
	"EDGES",
	"SURFEDGES",
	"MODELS"
};

enum MODEL_SORT_MODES {
	SORT_VERTS,
	SORT_NODES,
	SORT_CLIPNODES,
	SORT_FACES,
	SORT_MODES
};

struct BSPLUMP
{
	int nOffset; // File offset to data
	unsigned int nLength; // Length of data
};

struct BSPHEADER
{
	int nVersion;           // Must be 30 for a valid HL BSP file
	BSPLUMP lump[HEADER_LUMPS]; // Stores the directory of lumps
};

struct LumpState {
	unsigned char* lumps[HEADER_LUMPS];
	int lumpLen[HEADER_LUMPS];
};

struct BSPPLANE {
	vec3 vNormal;
	float fDist;
	int nType;

	// returns true if the plane was flipped
	bool update(vec3 newNormal, float fdist);
};

struct CSGPLANE {
	double normal[3];
	double origin[3];
	double dist;
	int nType;
};

struct BSPTEXTUREINFO {
	vec3 vS;
	float shiftS;
	vec3 vT;
	float shiftT;
	unsigned int iMiptex;
	unsigned int nFlags;
};

struct BSPMIPTEX
{
	char szName[MAXTEXTURENAME];  // Name of texture
	unsigned int nWidth, nHeight;		  // Extends of the texture
	unsigned int nOffsets[MIPLEVELS];	  // Offsets to texture mipmaps, relative to the start of this structure
};

struct BSPFACE {
	unsigned short iPlane;          // Plane the face is parallel to
	unsigned short nPlaneSide;      // Set if different normals orientation
	unsigned int iFirstEdge;      // Index of the first surfedge
	unsigned short nEdges;          // Number of consecutive surfedges
	unsigned short iTextureInfo;    // Index of the texture info structure
	unsigned char nStyles[4];       // Specify lighting styles
	unsigned int nLightmapOffset; // Offsets into the raw lightmap data
};

struct BSPLEAF
{
	int nContents;                         // Contents enumeration
	int nVisOffset;                        // Offset into the visibility lump
	short nMins[3], nMaxs[3];                // Defines bounding box
	unsigned short iFirstMarkSurface, nMarkSurfaces; // Index and count into marksurfaces array
	unsigned char nAmbientLevels[4];                 // Ambient sound levels

	bool isEmpty();
};

struct BSPEDGE {
	unsigned short iVertex[2]; // Indices into vertex array

	BSPEDGE();
	BSPEDGE(unsigned short v1, unsigned short v2);
};

struct BSPMODEL
{
	vec3 nMins;
	vec3 nMaxs;
	vec3 vOrigin;                  // Coordinates to move the // coordinate system
	int iHeadnodes[MAX_MAP_HULLS]; // Index into nodes array
	int nVisLeafs;                 // ???
	int iFirstFace, nFaces;        // Index and count into faces
};


struct BSPNODE
{
	unsigned int iPlane;            // Index into Planes lump
	short iChildren[2];       // If > 0, then indices into Nodes // otherwise bitwise inverse indices into Leafs
	short nMins[3], nMaxs[3]; // Defines bounding box
	unsigned short firstFace, nFaces; // Index and count into Faces
};

struct BSPCLIPNODE
{
	int iPlane;       // Index into planes
	short iChildren[2]; // negative numbers are contents
};


/*
 * application types (not part of the BSP)
 */

struct ScalableTexinfo {
	int texinfoIdx;
	vec3 oldS, oldT;
	float oldShiftS, oldShiftT;
	int planeIdx;
	int faceIdx;
};

struct TransformVert {
	vec3 pos;
	vec3* ptr; // face vertex to move with (null for invisible faces)
	std::vector<int> iPlanes;
	vec3 startPos; // for dragging
	vec3 undoPos; // for undoing invalid solid stuff
	bool selected;
};

struct HullEdge {
	int verts[2]; // index into modelVerts/hullVerts
	int planes[2]; // index into iPlanes
	bool selected;
};

struct Face {
	std::vector<int> verts; // index into hullVerts
	BSPPLANE plane;
	int planeSide;
	int iTextureInfo;
};

struct Solid {
	std::vector<Face> faces;
	std::vector<TransformVert> hullVerts; // control points for hull 0
	std::vector<HullEdge> hullEdges; // for vertex manipulation (holds indexes into hullVerts)
};

// used to construct bounding volumes for solid leaves
struct NodeVolumeCuts {
	int nodeIdx;
	std::vector<BSPPLANE> cuts; // cuts which define the leaf boundaries when applied to a bounding box, in order.
};
