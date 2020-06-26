#pragma once
#include "util.h"
#include "Fgd.h"
#include "VertexBuffer.h"

struct EntCube {
	vec3 mins;
	vec3 maxs;
	COLOR3 color;

	VertexBuffer* buffer;
	VertexBuffer* selectBuffer; // red coloring for selected ents
	VertexBuffer* wireframeBuffer; // yellow outline for selected ents
};

class PointEntRenderer {
public:
	Fgd* fgd;

	PointEntRenderer(Fgd* fgd, ShaderProgram* colorShader);
	~PointEntRenderer();

	EntCube* getEntCube(Entity* ent);

private:
	ShaderProgram* colorShader;
	map<string, EntCube*> cubeMap;
	vector<EntCube*> entCubes;

	void genPointEntCubes();
	EntCube* getCubeMatchingProps(EntCube* cube);
	void genCubeBuffers(EntCube* cube);
};