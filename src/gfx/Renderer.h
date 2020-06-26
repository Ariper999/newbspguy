#include <GL/glew.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include <GLFW/glfw3.h>
#include "ShaderProgram.h"
#include "BspRenderer.h"
#include "Fgd.h"
#include <thread>
#include <future>

class Gui;

enum transform_modes {
	TRANSFORM_MOVE,
	TRANSFORM_SCALE
};

enum transform_targets {
	TRANSFORM_OBJECT,
	TRANSFORM_VERTEX
};

struct TransformAxes {
	cCube* model;
	VertexBuffer* buffer;
	vec3 origin;
	vec3 mins[6];
	vec3 maxs[6];
	COLOR3 dimColor[6];
	COLOR3 hoverColor[6];
	int numAxes;
};

struct HullEdge {
	int verts[2]; // index into modelVerts
	int planes[2]; // index into iPlanes
	bool selected;
};

struct AppSettings {
	int windowWidth = 800;
	int windowHeight = 600;
	int windowX = 0;
	int windowY = 0;
	int maximized = 0;
	int fontSize = 22;
	string gamedir;
	bool valid = false;

	bool debug_open = false;
	bool keyvalue_open = false;
	bool transform_open = false;
	bool log_open = false;
	bool settings_open = false;

	float fov;
	float zfar;
	float moveSpeed;
	float rotSpeed;
	int render_flags;

	vector<string> fgdPaths;

	void load();
	void save();
};

class Renderer;

extern AppSettings g_settings;
extern Renderer* g_app;

class Renderer {
	friend class Gui;

public:
	vector<BspRenderer*> mapRenderers;

	Renderer();
	Renderer(Renderer& renderer);
	~Renderer();

	void addMap(Bsp* map);

	void renderLoop();
	void reload();
	void saveSettings();
	void loadSettings();

private:
	GLFWwindow* window;
	ShaderProgram* bspShader;
	ShaderProgram* colorShader;
	PointEntRenderer* pointEntRenderer;
	PointEntRenderer* swapPointEntRenderer = NULL;
	Gui* gui;

	future<void> fgdFuture;
	bool reloading = false;
	bool reloadingGameDir = false;
	bool isLoading = false;

	Fgd* fgd = NULL;

	vec3 cameraOrigin;
	vec3 cameraAngles;
	vec3 cameraForward;
	vec3 cameraUp;
	vec3 cameraRight;
	bool cameraIsRotating;
	float frameTimeScale = 0.0f;
	float moveSpeed = 4.0f;
	float fov = 75.0f;
	float zNear = 1.0f;
	float zFar = 262144.0f;
	float rotationSpeed = 5.0f;
	int windowWidth;
	int windowHeight;
	mat4x4 model, view, projection, modelView, modelViewProjection;

	vec2 lastMousePos;
	vec2 totalMouseDrag;

	bool movingEnt = false; // grab an ent and move it with the camera
	vec3 grabStartOrigin;
	vec3 gragStartEntOrigin;
	float grabDist;

	TransformAxes moveAxes;
	TransformAxes scaleAxes;
	int hoverAxis; // axis being hovered
	int draggingAxis; // axis currently being dragged by the mouse
	bool gridSnappingEnabled = true;
	int gridSnapLevel = 0;
	int transformMode = TRANSFORM_MOVE;
	int transformTarget = TRANSFORM_OBJECT;
	bool showDragAxes = false;
	vec3 axisDragStart;
	vec3 axisDragEntOriginStart;
	vector<ScalableTexinfo> scaleTexinfos; // texture coordinates to scale
	bool textureLock = false;
	bool invalidSolid = false;
	bool isTransformableSolid = true;
	bool canTransform = false;
	bool anyEdgeSelected = false;
	bool anyVertSelected = false;

	vector<TransformVert> modelVerts;
	vector<HullEdge> modelEdges;
	cCube* modelVertCubes = NULL;
	VertexBuffer* modelVertBuff = NULL;
	int hoverVert = -1;
	int hoverEdge = -1;
	float vertExtentFactor = 0.01f;

	Entity* copiedEnt = NULL;

	int oldLeftMouse;
	int oldRightMouse;
	int oldScroll;
	bool pressed[GLFW_KEY_LAST];
	bool released[GLFW_KEY_LAST];
	char oldPressed[GLFW_KEY_LAST];
	char oldReleased[GLFW_KEY_LAST];
	bool anyCtrlPressed;
	bool anyAltPressed;
	bool anyShiftPressed;

	PickInfo pickInfo;
	int pickCount = 0; // used to give unique IDs to text inputs so switching ents doesn't update keys accidentally
	int vertPickCount = 0;

	vec3 debugPoint;
	vec3 debugVec0;
	vec3 debugVec1;
	vec3 debugVec2;
	vec3 debugVec3;
	int debugInt = 0;
	int debugIntMax = 0;
	bool debugClipnodes = false;

	vec3 getMoveDir();
	void controls();
	void cameraPickingControls();
	void vertexEditControls();
	void cameraRotationControls(vec2 mousePos);
	void cameraObjectHovering();
	void cameraContextMenus(); // right clicking on ents and things
	void moveGrabbedEnt(); // translates the grabbed ent
	void shortcutControls(); // hotkeys for menus and things
	void pickObject(); // select stuff with the mouse
	bool transformAxisControls(); // true if grabbing axes
	void applyTransform();
	void setupView();
	void getPickRay(vec3& start, vec3& pickDir);
	BspRenderer* getMapContainingCamera();

	void drawModelVerts();
	void drawTransformAxes();
	void drawLine(vec3 start, vec3 end, COLOR3 color);
	void drawPlane(BSPPLANE& plane, COLOR3 color);
	void drawClipnodes(Bsp* map, int iNode, int& currentPlane, int activePlane);

	vec3 getEntOrigin(Bsp* map, Entity* ent);
	vec3 getEntOffset(Bsp* map, Entity* ent);

	vec3 getAxisDragPoint(vec3 origin);

	void updateDragAxes();
	void updateModelVerts();
	void moveSelectedVerts(vec3 delta);

	vec3 snapToGrid(vec3 pos);

	void grabEnt();
	void cutEnt();
	void copyEnt();
	void pasteEnt(bool noModifyOrigin);
	void deleteEnt();
	void scaleSelectedObject(float x, float y, float z);
	void scaleSelectedObject(vec3 dir, vec3 fromDir);
	void scaleSelectedVerts(float x, float y, float z);
	vec3 getEdgeControlPoint(HullEdge& iEdge);

	void loadFgds();
};