/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*
*	This product contains software technology licensed from Id
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc.
*	All Rights Reserved.
*
****/
// studio_render.cpp: routines for drawing Half-Life 3DStudio models
// updates:
// 1-4-99		fixed AdvanceFrame wraping bug
// 23-11-2018	moved from GLUT to GLFW

// External Libraries
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#pragma warning( disable : 4244 ) // double to float

#include "mathlib.h"
#include "studio_render.h"
#include "util.h"
#include <GLFW/glfw3.h>

vec3_t g_vright;		// needs to be set to viewer's right in order for chrome to work
float g_lambert;		// modifier for pseudo-hemispherical lighting


vec3_t			g_xformverts[MAXSTUDIOVERTS];	// transformed vertices
vec3_t			g_lightvalues[MAXSTUDIOVERTS];	// light surface normals
vec3_t* g_pxformverts;
vec3_t* g_pvlightvalues;

vec3_t			g_lightvec;						// light vector in model reference frame
vec3_t			g_blightvec[MAXSTUDIOBONES];	// light vectors in bone reference frames
int				g_ambientlight;					// ambient world light
float			g_shadelight;					// direct world light
vec3_t			g_lightcolor;

int				g_smodels_total;				// cookie

float			g_bonetransform[MAXSTUDIOBONES][3][4];	// bone transformation matrix

int				g_chrome[MAXSTUDIOVERTS][2];	// texture coords for surface normals
int				g_chromeage[MAXSTUDIOBONES];	// last time chrome vectors were updated
vec3_t			g_chromeup[MAXSTUDIOBONES];		// chrome vector "up" in bone reference frames
vec3_t			g_chromeright[MAXSTUDIOBONES];	// chrome vector "right" in bone reference frames

////////////////////////////////////////////////////////////////////////

void StudioModel::CalcBoneAdj()
{
	int					i, j;
	float				value;
	mstudiobonecontroller_t* pbonecontroller;

	pbonecontroller = (mstudiobonecontroller_t*)((unsigned char*)m_pstudiohdr + m_pstudiohdr->bonecontrollerindex);

	for (j = 0; j < m_pstudiohdr->numbonecontrollers; j++)
	{
		i = pbonecontroller[j].index;
		if (i <= 3)
		{
			// check for 360% wrapping
			if (pbonecontroller[j].type & STUDIO_RLOOP)
			{
				value = m_controller[i] * (360.0 / 256.0) + pbonecontroller[j].start;
			}
			else
			{
				value = m_controller[i] / 255.0;
				if (value < 0) value = 0;
				if (value > 1.0) value = 1.0;
				value = (1.0 - value) * pbonecontroller[j].start + value * pbonecontroller[j].end;
			}
			// Con_DPrintf( "%d %d %f : %f\n", m_controller[j], m_prevcontroller[j], value, dadt );
		}
		else
		{
			value = m_mouth / 64.0;
			if (value > 1.0) value = 1.0;
			value = (1.0 - value) * pbonecontroller[j].start + value * pbonecontroller[j].end;
			// Con_DPrintf("%d %f\n", mouthopen, value );
		}
		switch (pbonecontroller[j].type & STUDIO_TYPES)
		{
		case STUDIO_XR:
		case STUDIO_YR:
		case STUDIO_ZR:
			m_adj[j] = value * (PI / 180.0);
			break;
		case STUDIO_X:
		case STUDIO_Y:
		case STUDIO_Z:
			m_adj[j] = value;
			break;
		}
	}
}


void StudioModel::CalcBoneQuaternion(int frame, float s, mstudiobone_t* pbone, mstudioanim_t* panim, float* q)
{
	int					j, k;
	vec4_t				q1, q2;
	vec3_t				angle1, angle2;
	mstudioanimvalue_t* panimvalue;

	for (j = 0; j < 3; j++)
	{
		if (panim->offset[j + 3] == 0)
		{
			angle2[j] = angle1[j] = pbone->value[j + 3]; // default;
		}
		else
		{
			panimvalue = (mstudioanimvalue_t*)((unsigned char*)panim + panim->offset[j + 3]);
			k = frame;
			while (panimvalue->num.total <= k)
			{
				k -= panimvalue->num.total;
				panimvalue += panimvalue->num.valid + 1;
			}
			// Bah, missing blend!
			if (panimvalue->num.valid > k)
			{
				angle1[j] = panimvalue[k + 1].value;

				if (panimvalue->num.valid > k + 1)
				{
					angle2[j] = panimvalue[k + 2].value;
				}
				else
				{
					if (panimvalue->num.total > k + 1)
						angle2[j] = angle1[j];
					else
						angle2[j] = panimvalue[panimvalue->num.valid + 2].value;
				}
			}
			else
			{
				angle1[j] = panimvalue[panimvalue->num.valid].value;
				if (panimvalue->num.total > k + 1)
				{
					angle2[j] = angle1[j];
				}
				else
				{
					angle2[j] = panimvalue[panimvalue->num.valid + 2].value;
				}
			}
			angle1[j] = pbone->value[j + 3] + angle1[j] * pbone->scale[j + 3];
			angle2[j] = pbone->value[j + 3] + angle2[j] * pbone->scale[j + 3];
		}

		if (pbone->bonecontroller[j + 3] != -1)
		{
			angle1[j] += m_adj[pbone->bonecontroller[j + 3]];
			angle2[j] += m_adj[pbone->bonecontroller[j + 3]];
		}
	}

	if (!VectorCompare(angle1, angle2))
	{
		AngleQuaternion(angle1, q1);
		AngleQuaternion(angle2, q2);
		QuaternionSlerp(q1, q2, s, q);
	}
	else
	{
		AngleQuaternion(angle1, q);
	}
}


void StudioModel::CalcBonePosition(int frame, float s, mstudiobone_t* pbone, mstudioanim_t* panim, float* pos)
{
	int					j, k;
	mstudioanimvalue_t* panimvalue;

	for (j = 0; j < 3; j++)
	{
		pos[j] = pbone->value[j]; // default;
		if (panim->offset[j] != 0)
		{
			panimvalue = (mstudioanimvalue_t*)((unsigned char*)panim + panim->offset[j]);

			k = frame;
			// find span of values that includes the frame we want
			while (panimvalue->num.total <= k)
			{
				k -= panimvalue->num.total;
				panimvalue += panimvalue->num.valid + 1;
			}
			// if we're inside the span
			if (panimvalue->num.valid > k)
			{
				// and there's more data in the span
				if (panimvalue->num.valid > k + 1)
				{
					pos[j] += (panimvalue[k + 1].value * (1.0 - s) + s * panimvalue[k + 2].value) * pbone->scale[j];
				}
				else
				{
					pos[j] += panimvalue[k + 1].value * pbone->scale[j];
				}
			}
			else
			{
				// are we at the end of the repeating values section and there's another section with data?
				if (panimvalue->num.total <= k + 1)
				{
					pos[j] += (panimvalue[panimvalue->num.valid].value * (1.0 - s) + s * panimvalue[panimvalue->num.valid + 2].value) * pbone->scale[j];
				}
				else
				{
					pos[j] += panimvalue[panimvalue->num.valid].value * pbone->scale[j];
				}
			}
		}
		if (pbone->bonecontroller[j] != -1)
		{
			pos[j] += m_adj[pbone->bonecontroller[j]];
		}
	}
}


void StudioModel::CalcRotations(vec3_t* pos, vec4_t* q, mstudioseqdesc_t* pseqdesc, mstudioanim_t* panim, float f)
{
	int					i;
	int					frame;
	mstudiobone_t* pbone;
	float				s;

	frame = (int)f;
	s = (f - frame);

	// add in programatic controllers
	CalcBoneAdj();

	pbone = (mstudiobone_t*)((unsigned char*)m_pstudiohdr + m_pstudiohdr->boneindex);
	for (i = 0; i < m_pstudiohdr->numbones; i++, pbone++, panim++)
	{
		CalcBoneQuaternion(frame, s, pbone, panim, q[i]);
		CalcBonePosition(frame, s, pbone, panim, pos[i]);
	}

	if (pseqdesc->motiontype & STUDIO_X)
		pos[pseqdesc->motionbone][0] = 0.0;
	if (pseqdesc->motiontype & STUDIO_Y)
		pos[pseqdesc->motionbone][1] = 0.0;
	if (pseqdesc->motiontype & STUDIO_Z)
		pos[pseqdesc->motionbone][2] = 0.0;
}


mstudioanim_t* StudioModel::GetAnim(mstudioseqdesc_t* pseqdesc)
{
	mstudioseqgroup_t* pseqgroup;
	pseqgroup = (mstudioseqgroup_t*)((unsigned char*)m_pstudiohdr + m_pstudiohdr->seqgroupindex) + pseqdesc->seqgroup;

	if (pseqdesc->seqgroup == 0)
	{
		return (mstudioanim_t*)((unsigned char*)m_pstudiohdr + pseqgroup->unused2 /* was pseqgroup->data, will be almost always be 0 */ + pseqdesc->animindex);
	}

	return (mstudioanim_t*)((unsigned char*)m_panimhdr[pseqdesc->seqgroup] + pseqdesc->animindex);
}


void StudioModel::SlerpBones(vec4_t q1[], vec3_t pos1[], vec4_t q2[], vec3_t pos2[], float s)
{
	int			i;
	vec4_t		q3;
	float		s1;

	if (s < 0) s = 0;
	else if (s > 1.0) s = 1.0;

	s1 = 1.0 - s;

	for (i = 0; i < m_pstudiohdr->numbones; i++)
	{
		QuaternionSlerp(q1[i], q2[i], s, q3);
		q1[i][0] = q3[0];
		q1[i][1] = q3[1];
		q1[i][2] = q3[2];
		q1[i][3] = q3[3];
		pos1[i][0] = pos1[i][0] * s1 + pos2[i][0] * s;
		pos1[i][1] = pos1[i][1] * s1 + pos2[i][1] * s;
		pos1[i][2] = pos1[i][2] * s1 + pos2[i][2] * s;
	}
}


void StudioModel::AdvanceFrame(float dt)
{
	mstudioseqdesc_t* pseqdesc;
	pseqdesc = (mstudioseqdesc_t*)((unsigned char*)m_pstudiohdr + m_pstudiohdr->seqindex) + m_sequence;

	if (dt > 0.1)
		dt = (float)0.1;
	m_frame += dt * pseqdesc->fps;

	if (pseqdesc->numframes <= 1)
	{
		m_frame = 0;
	}
	else
	{
		// wrap
		m_frame -= (int)(m_frame / (pseqdesc->numframes - 1)) * (pseqdesc->numframes - 1);
	}
}


void StudioModel::SetUpBones(void)
{
	int					i;

	mstudiobone_t* pbones;
	mstudioseqdesc_t* pseqdesc;
	mstudioanim_t* panim;

	static vec3_t		pos[MAXSTUDIOBONES];
	float				bonematrix[3][4];
	static vec4_t		q[MAXSTUDIOBONES];

	static vec3_t		pos2[MAXSTUDIOBONES];
	static vec4_t		q2[MAXSTUDIOBONES];
	static vec3_t		pos3[MAXSTUDIOBONES];
	static vec4_t		q3[MAXSTUDIOBONES];
	static vec3_t		pos4[MAXSTUDIOBONES];
	static vec4_t		q4[MAXSTUDIOBONES];


	if (m_sequence >= m_pstudiohdr->numseq) {
		m_sequence = 0;
	}

	pseqdesc = (mstudioseqdesc_t*)((unsigned char*)m_pstudiohdr + m_pstudiohdr->seqindex) + m_sequence;

	panim = GetAnim(pseqdesc);
	CalcRotations(pos, q, pseqdesc, panim, m_frame);

	if (pseqdesc->numblends > 1)
	{
		float				s;

		panim += m_pstudiohdr->numbones;
		CalcRotations(pos2, q2, pseqdesc, panim, m_frame);
		s = m_blending[0] / 255.0;

		SlerpBones(q, pos, q2, pos2, s);

		if (pseqdesc->numblends == 4)
		{
			panim += m_pstudiohdr->numbones;
			CalcRotations(pos3, q3, pseqdesc, panim, m_frame);

			panim += m_pstudiohdr->numbones;
			CalcRotations(pos4, q4, pseqdesc, panim, m_frame);

			s = m_blending[0] / 255.0;
			SlerpBones(q3, pos3, q4, pos4, s);

			s = m_blending[1] / 255.0;
			SlerpBones(q, pos, q3, pos3, s);
		}
	}

	pbones = (mstudiobone_t*)((unsigned char*)m_pstudiohdr + m_pstudiohdr->boneindex);

	for (i = 0; i < m_pstudiohdr->numbones; i++) {
		QuaternionMatrix(q[i], bonematrix);

		bonematrix[0][3] = pos[i][0];
		bonematrix[1][3] = pos[i][1];
		bonematrix[2][3] = pos[i][2];

		if (pbones[i].parent == -1) {
			memcpy(g_bonetransform[i], bonematrix, sizeof(float) * 12);
		}
		else {
			R_ConcatTransforms(g_bonetransform[pbones[i].parent], bonematrix, g_bonetransform[i]);
		}
	}
}



/*
================
StudioModel::TransformFinalVert
================
*/
void StudioModel::Lighting(float* lv, int bone, int flags, vec3_t normal)
{
	float 	illum;
	float	lightcos;

	illum = g_ambientlight;

	if (flags & STUDIO_NF_FLATSHADE)
	{
		illum += g_shadelight * 0.8;
	}
	else
	{
		float r;
		lightcos = mDotProduct(normal, g_blightvec[bone]); // -1 colinear, 1 opposite

		if (lightcos > 1.0)
			lightcos = 1;

		illum += g_shadelight;

		r = g_lambert;
		if (r <= 1.0) r = 1.0;

		lightcos = (lightcos + (r - 1.0)) / r; 		// do modified hemispherical lighting
		if (lightcos > 0.0)
		{
			illum -= g_shadelight * lightcos;
		}
		if (illum <= 0)
			illum = 0;
	}

	if (illum > 255)
		illum = 255;
	*lv = illum / 255.0;	// Light from 0 to 1.0
}


void StudioModel::Chrome(int* pchrome, int bone, vec3_t normal)
{
	float n;

	if (g_chromeage[bone] != g_smodels_total)
	{
		// calculate vectors from the viewer to the bone. This roughly adjusts for position
		vec3_t chromeupvec;		// g_chrome t vector in world reference frame
		vec3_t chromerightvec;	// g_chrome s vector in world reference frame
		vec3_t tmp;				// vector pointing at bone in world reference frame
		mVectorScale(m_origin, -1, tmp);
		tmp[0] += g_bonetransform[bone][0][3];
		tmp[1] += g_bonetransform[bone][1][3];
		tmp[2] += g_bonetransform[bone][2][3];
		VectorNormalize(tmp);
		mCrossProduct(tmp, g_vright, chromeupvec);
		VectorNormalize(chromeupvec);
		mCrossProduct(tmp, chromeupvec, chromerightvec);
		VectorNormalize(chromerightvec);

		VectorIRotate(chromeupvec, g_bonetransform[bone], g_chromeup[bone]);
		VectorIRotate(chromerightvec, g_bonetransform[bone], g_chromeright[bone]);

		g_chromeage[bone] = g_smodels_total;
	}

	// calc s coord
	n = mDotProduct(normal, g_chromeright[bone]);
	pchrome[0] = (n + 1.0) * 32; // FIX: make this a float

	// calc t coord
	n = mDotProduct(normal, g_chromeup[bone]);
	pchrome[1] = (n + 1.0) * 32; // FIX: make this a float
}


/*
================
StudioModel::SetupLighting
	set some global variables based on entity position
inputs:
outputs:
	g_ambientlight
	g_shadelight
================
*/
void StudioModel::SetupLighting()
{
	int i;
	g_ambientlight = 32;
	g_shadelight = 192;

	g_lightvec[0] = 0;
	g_lightvec[1] = 0;
	g_lightvec[2] = -1.0;

	g_lightcolor[0] = 1.0;
	g_lightcolor[1] = 1.0;
	g_lightcolor[2] = 1.0;

	// TODO: only do it for bones that actually have textures
	for (i = 0; i < m_pstudiohdr->numbones; i++)
	{
		VectorIRotate(g_lightvec, g_bonetransform[i], g_blightvec[i]);
	}
}


/*
=================
StudioModel::SetupModel
	based on the body part, figure out which mesh it should be using.
inputs:
	currententity
outputs:
	pstudiomesh
	pmdl
=================
*/

void StudioModel::SetupModel(int bodypart)
{
	int index;

	if (bodypart > m_pstudiohdr->numbodyparts)
	{
		// Con_DPrintf ("StudioModel::SetupModel: no such bodypart %d\n", bodypart);
		bodypart = 0;
	}

	mstudiobodyparts_t* pbodypart = (mstudiobodyparts_t*)((unsigned char*)m_pstudiohdr + m_pstudiohdr->bodypartindex) + bodypart;

	index = m_bodynum / pbodypart->base;
	index = index % pbodypart->nummodels;

	m_pmodel = (mstudiomodel_t*)((unsigned char*)m_pstudiohdr + pbodypart->modelindex) + index;
}


/*
================
StudioModel::DrawModel
inputs:
	currententity
	r_entorigin
================
*/
void StudioModel::DrawModel()
{
	if (!m_pstudiohdr || m_pstudiohdr->numbodyparts == 0)
		return;

	int i;

	g_smodels_total++; // render data cache cookie

	g_pxformverts = &g_xformverts[0];
	g_pvlightvalues = &g_lightvalues[0];


	glPushMatrix();

	glTranslatef(m_origin[0], m_origin[1], m_origin[2]);

	glRotatef(m_angles[1], 0, 0, 1);
	glRotatef(m_angles[0], 0, 1, 0);
	glRotatef(m_angles[2], 1, 0, 0);

	// glShadeModel (GL_SMOOTH);

	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	// glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);

	SetUpBones();

	SetupLighting();

	for (i = 0; i < m_pstudiohdr->numbodyparts; i++)
	{
		SetupModel(i);
		DrawPoints();
	}

	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

	// glShadeModel (GL_FLAT);

	// glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

	glPopMatrix();
}

/*
================

================
*/
void StudioModel::DrawPoints()
{
	int					i, j;
	mstudiomesh_t* pmesh;
	unsigned char* pvertbone;
	unsigned char* pnormbone;
	vec3_t* pstudioverts;
	vec3_t* pstudionorms;
	mstudiotexture_t* ptexture;
	float* av;
	float* lv;
	float				lv_tmp;
	short* pskinref;

	pvertbone = ((unsigned char*)m_pstudiohdr + m_pmodel->vertinfoindex);
	pnormbone = ((unsigned char*)m_pstudiohdr + m_pmodel->norminfoindex);
	ptexture = (mstudiotexture_t*)((unsigned char*)m_ptexturehdr + m_ptexturehdr->textureindex);

	pmesh = (mstudiomesh_t*)((unsigned char*)m_pstudiohdr + m_pmodel->meshindex);

	pstudioverts = (vec3_t*)((unsigned char*)m_pstudiohdr + m_pmodel->vertindex);
	pstudionorms = (vec3_t*)((unsigned char*)m_pstudiohdr + m_pmodel->normindex);

	pskinref = (short*)((unsigned char*)m_ptexturehdr + m_ptexturehdr->skinindex);
	if (m_skinnum != 0 && m_skinnum < m_ptexturehdr->numskinfamilies)
		pskinref += (m_skinnum * m_ptexturehdr->numskinref);

	for (i = 0; i < m_pmodel->numverts; i++)
	{
		VectorTransform(pstudioverts[i], g_bonetransform[pvertbone[i]], g_pxformverts[i]);
	}

	//
	// clip and draw all triangles
	//

	lv = (float*)g_pvlightvalues;
	for (j = 0; j < m_pmodel->nummesh; j++)
	{
		int flags;
		flags = ptexture[pskinref[pmesh[j].skinref]].flags;
		for (i = 0; i < pmesh[j].numnorms; i++, lv += 3, pstudionorms++, pnormbone++)
		{
			Lighting(&lv_tmp, *pnormbone, flags, (float*)pstudionorms);

			// FIX: move this check out of the inner loop
			if (flags & STUDIO_NF_CHROME)
				Chrome(g_chrome[(float(*)[3])lv - g_pvlightvalues], *pnormbone, (float*)pstudionorms);

			lv[0] = lv_tmp * g_lightcolor[0];
			lv[1] = lv_tmp * g_lightcolor[1];
			lv[2] = lv_tmp * g_lightcolor[2];
		}
	}

	glCullFace(GL_FRONT);

	for (j = 0; j < m_pmodel->nummesh; j++)
	{
		short* ptricmds;

		pmesh = (mstudiomesh_t*)((unsigned char*)m_pstudiohdr + m_pmodel->meshindex) + j;
		ptricmds = (short*)((unsigned char*)m_pstudiohdr + pmesh->triindex);

		glBindTexture(GL_TEXTURE_2D, ptexture[pskinref[pmesh->skinref]].index);

		const int MAX_TRIS_PER_BODYGROUP = 4080;
		const int MAX_VERTS_PER_CALL = MAX_TRIS_PER_BODYGROUP * 3;
		static float vertexData[MAX_VERTS_PER_CALL * 3];
		static float texCoordData[MAX_VERTS_PER_CALL * 2];
		static float colorData[MAX_VERTS_PER_CALL * 4];

		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glEnableClientState(GL_COLOR_ARRAY);

		int totalElements = 0;
		int texCoordIdx = 0;
		int colorIdx = 0;
		int vertexIdx = 0;
		int stripIdx = 0;
		while (i = *(ptricmds++))
		{
			int drawMode = GL_TRIANGLE_STRIP;
			if (i < 0)
			{
				i = -i;
				drawMode = GL_TRIANGLE_FAN;
			}

			int polies = i - 2;
			int elementsThisStrip = 0;
			int fanStartVertIdx = vertexIdx;
			int fanStartTexIdx = texCoordIdx;
			int fanStartColorIdx = colorIdx;

			for (; i > 0; i--, ptricmds += 4)
			{

				if (elementsThisStrip++ >= 3) {
					int v1PosIdx = fanStartVertIdx;
					int v2PosIdx = vertexIdx - 3 * 1;
					int v1TexIdx = fanStartTexIdx;
					int v2TexIdx = texCoordIdx - 2 * 1;
					int v1ColorIdx = fanStartColorIdx;
					int v2ColorIdx = colorIdx - 4 * 1;

					if (drawMode == GL_TRIANGLE_STRIP) {
						v1PosIdx = vertexIdx - 3 * 2;
						v2PosIdx = vertexIdx - 3 * 1;
						v1TexIdx = texCoordIdx - 2 * 2;
						v2TexIdx = texCoordIdx - 2 * 1;
						v1ColorIdx = colorIdx - 4 * 2;
						v2ColorIdx = colorIdx - 4 * 1;
					}

					texCoordData[texCoordIdx++] = texCoordData[v1TexIdx];
					texCoordData[texCoordIdx++] = texCoordData[v1TexIdx + 1];
					colorData[colorIdx++] = colorData[v1ColorIdx];
					colorData[colorIdx++] = colorData[v1ColorIdx + 1];
					colorData[colorIdx++] = colorData[v1ColorIdx + 2];
					colorData[colorIdx++] = colorData[v1ColorIdx + 3];
					vertexData[vertexIdx++] = vertexData[v1PosIdx];
					vertexData[vertexIdx++] = vertexData[v1PosIdx + 1];
					vertexData[vertexIdx++] = vertexData[v1PosIdx + 2];

					texCoordData[texCoordIdx++] = texCoordData[v2TexIdx];
					texCoordData[texCoordIdx++] = texCoordData[v2TexIdx + 1];
					colorData[colorIdx++] = colorData[v2ColorIdx];
					colorData[colorIdx++] = colorData[v2ColorIdx + 1];
					colorData[colorIdx++] = colorData[v2ColorIdx + 2];
					colorData[colorIdx++] = colorData[v2ColorIdx + 3];
					vertexData[vertexIdx++] = vertexData[v2PosIdx];
					vertexData[vertexIdx++] = vertexData[v2PosIdx + 1];
					vertexData[vertexIdx++] = vertexData[v2PosIdx + 2];

					totalElements += 2;
					elementsThisStrip += 2;
				}


				float s = 1.0 / (float)ptexture[pskinref[pmesh->skinref]].width;
				float t = 1.0 / (float)ptexture[pskinref[pmesh->skinref]].height;

				// FIX: put these in as integer coords, not floats
				if (ptexture[pskinref[pmesh->skinref]].flags & STUDIO_NF_CHROME)
				{
					texCoordData[texCoordIdx++] = g_chrome[ptricmds[1]][0] * s;
					texCoordData[texCoordIdx++] = g_chrome[ptricmds[1]][1] * t;
				}
				else
				{
					texCoordData[texCoordIdx++] = ptricmds[2] * s;
					texCoordData[texCoordIdx++] = ptricmds[3] * t;
				}

				lv = g_pvlightvalues[ptricmds[1]];
				colorData[colorIdx++] = lv[0];
				colorData[colorIdx++] = lv[1];
				colorData[colorIdx++] = lv[2];
				colorData[colorIdx++] = 1.0;

				av = g_pxformverts[ptricmds[0]];
				vertexData[vertexIdx++] = av[0];
				vertexData[vertexIdx++] = av[1];
				vertexData[vertexIdx++] = av[2];


				totalElements++;
			}
			if (drawMode == GL_TRIANGLE_STRIP) {
				for (int p = 1; p < polies; p += 2) {
					int polyOffset = p * 3;

					for (int k = 0; k < 3; k++)
					{
						int vstart = polyOffset * 3 + fanStartVertIdx + k;
						float t = vertexData[vstart];
						vertexData[vstart] = vertexData[vstart + 3];
						vertexData[vstart + 3] = t;
					}
					for (int k = 0; k < 2; k++)
					{
						int vstart = polyOffset * 2 + fanStartTexIdx + k;
						float t = texCoordData[vstart];
						texCoordData[vstart] = texCoordData[vstart + 2];
						texCoordData[vstart + 2] = t;
					}
					for (int k = 0; k < 4; k++)
					{
						int vstart = polyOffset * 4 + fanStartColorIdx + k;
						float t = colorData[vstart];
						colorData[vstart] = colorData[vstart + 4];
						colorData[vstart + 4] = t;
					}
				}
			}

			glVertexPointer(3, GL_FLOAT, sizeof(float) * 3, vertexData);
			glTexCoordPointer(2, GL_FLOAT, sizeof(float) * 2, texCoordData);
			glColorPointer(4, GL_FLOAT, sizeof(float) * 4, colorData);

			glDrawArrays(GL_TRIANGLES, 0, totalElements);
		}
	}
}


void StudioModel::UploadTexture(mstudiotexture_t* ptexture, unsigned char* data, unsigned char* pal)
{
	// unsigned *in, int inwidth, int inheight, unsigned *out,  int outwidth, int outheight;
	int outwidth, outheight;
	int		i, j;
	int		row1[256], row2[256], col1[256], col2[256];
	unsigned char* pix1, * pix2, * pix3, * pix4;
	unsigned char* tex, * out;

	// convert texture to power of 2
	for (outwidth = 1; outwidth < ptexture->width; outwidth <<= 1)
		;

	if (outwidth > 256)
		outwidth = 256;

	for (outheight = 1; outheight < ptexture->height; outheight <<= 1)
		;

	if (outheight > 256)
		outheight = 256;

	tex = out = (unsigned char*)malloc(outwidth * outheight * 4);

	for (i = 0; i < outwidth; i++)
	{
		col1[i] = (i + 0.25) * (ptexture->width / (float)outwidth);
		col2[i] = (i + 0.75) * (ptexture->width / (float)outwidth);
	}

	for (i = 0; i < outheight; i++)
	{
		row1[i] = (int)((i + 0.25) * (ptexture->height / (float)outheight)) * ptexture->width;
		row2[i] = (int)((i + 0.75) * (ptexture->height / (float)outheight)) * ptexture->width;
	}

	// scale down and convert to 32bit RGB
	for (i = 0; i < outheight; i++)
	{
		for (j = 0; j < outwidth; j++, out += 4)
		{
			pix1 = &pal[data[row1[i] + col1[j]] * 3];
			pix2 = &pal[data[row1[i] + col2[j]] * 3];
			pix3 = &pal[data[row2[i] + col1[j]] * 3];
			pix4 = &pal[data[row2[i] + col2[j]] * 3];

			out[0] = (pix1[0] + pix2[0] + pix3[0] + pix4[0]) >> 2;
			out[1] = (pix1[1] + pix2[1] + pix3[1] + pix4[1]) >> 2;
			out[2] = (pix1[2] + pix2[2] + pix3[2] + pix4[2]) >> 2;
			out[3] = 0xFF;
		}
	}
	GLuint g_texnum;
	glGenTextures(1, &g_texnum);
	glBindTexture(GL_TEXTURE_2D, g_texnum);
	glTexImage2D(GL_TEXTURE_2D, 0, 3/*??*/, outwidth, outheight, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);


	// ptexture->width = outwidth;
	// ptexture->height = outheight;
	ptexture->index = g_texnum;

	g_texnum++;

	free(tex);
}




studiohdr_t* StudioModel::LoadModel(char* modelname)
{
	FILE* fp;
	long size;
	void* buffer;

	// load the model
	fopen_s(&fp, modelname, "rb");
	if (!fp)
	{
		printf("unable to open %s\n", modelname);
		return NULL;
	}

	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	buffer = malloc(size);
	fread(buffer, size, 1, fp);

	int					i;
	unsigned char* pin;
	studiohdr_t* phdr;
	mstudiotexture_t* ptexture;

	pin = (unsigned char*)buffer;
	phdr = (studiohdr_t*)pin;

	ptexture = (mstudiotexture_t*)(pin + phdr->textureindex);
	if (phdr->textureindex != 0)
	{
		for (i = 0; i < phdr->numtextures; i++)
		{
			// strncpy( name, mod->name );
			// strncpy( name, ptexture[i].name );
			UploadTexture(&ptexture[i], pin + ptexture[i].index, pin + ptexture[i].width * ptexture[i].height + ptexture[i].index);
		}
	}

	// UNDONE: free texture memory

	fclose(fp);

	return (studiohdr_t*)buffer;
}


studioseqhdr_t* StudioModel::LoadDemandSequences(char* modelname)
{
	FILE* fp;
	long size;
	void* buffer;

	// load the model
	fopen_s(&fp, modelname, "rb");
	if (!fp)
	{
		printf("unable to open %s\n", modelname);
		return NULL;
	}

	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	buffer = malloc(size);
	fread(buffer, size, 1, fp);

	fclose(fp);

	return (studioseqhdr_t*)buffer;
}


void StudioModel::Init(char* modelname)
{
	m_pstudiohdr = LoadModel(modelname);

	char texturename[256];

	memcpy(texturename, modelname, strlen(modelname) + 1);
	texturename[255] = '\0';

	// preload textures
	if (m_pstudiohdr->numtextures == 0 && strlen(texturename) > 4)
	{
		memcpy(&texturename[strlen(texturename) - 4], "T.mdl", 5);

		m_ptexturehdr = LoadModel(texturename);
	}
	else
	{
		m_ptexturehdr = m_pstudiohdr;
	}

	// preload animations
	if (m_pstudiohdr->numseqgroups > 1)
	{
		auto mdllen = strlen(modelname);
		for (int i = 1; i < m_pstudiohdr->numseqgroups; i++)
		{
			char seqgroupname[256];

			memcpy(seqgroupname, modelname, mdllen + 1);
			if (strlen(seqgroupname) > 4)
			{
				snprintf(&seqgroupname[strlen(seqgroupname) - 4], 20, "%02d.mdl", i);
			}
			m_panimhdr[i] = LoadDemandSequences(seqgroupname);
		}
	}
}


////////////////////////////////////////////////////////////////////////

int StudioModel::GetSequence()
{
	return m_sequence;
}

int StudioModel::SetSequence(int iSequence)
{
	if (iSequence > m_pstudiohdr->numseq)
		iSequence = 0;
	if (iSequence < 0)
		iSequence = m_pstudiohdr->numseq - 1;

	m_sequence = iSequence;
	m_frame = 0;

	return m_sequence;
}


void StudioModel::ExtractBbox(float* mins, float* maxs)
{
	mstudioseqdesc_t* pseqdesc;

	pseqdesc = (mstudioseqdesc_t*)((unsigned char*)m_pstudiohdr + m_pstudiohdr->seqindex);

	mins[0] = pseqdesc[m_sequence].bbmin[0];
	mins[1] = pseqdesc[m_sequence].bbmin[1];
	mins[2] = pseqdesc[m_sequence].bbmin[2];

	maxs[0] = pseqdesc[m_sequence].bbmax[0];
	maxs[1] = pseqdesc[m_sequence].bbmax[1];
	maxs[2] = pseqdesc[m_sequence].bbmax[2];
}



void StudioModel::GetSequenceInfo(float* pflFrameRate, float* pflGroundSpeed)
{
	mstudioseqdesc_t* pseqdesc;

	pseqdesc = (mstudioseqdesc_t*)((unsigned char*)m_pstudiohdr + m_pstudiohdr->seqindex) + (int)m_sequence;

	if (pseqdesc->numframes > 1)
	{
		*pflFrameRate = 256 * pseqdesc->fps / (pseqdesc->numframes - 1);
		*pflGroundSpeed = sqrt(pseqdesc->linearmovement[0] * pseqdesc->linearmovement[0] + pseqdesc->linearmovement[1] * pseqdesc->linearmovement[1] + pseqdesc->linearmovement[2] * pseqdesc->linearmovement[2]);
		*pflGroundSpeed = *pflGroundSpeed * pseqdesc->fps / (pseqdesc->numframes - 1);
	}
	else
	{
		*pflFrameRate = 256.0;
		*pflGroundSpeed = 0.0;
	}
}


float StudioModel::SetController(int iController, float flValue)
{
	int i;
	mstudiobonecontroller_t* pbonecontroller = (mstudiobonecontroller_t*)((unsigned char*)m_pstudiohdr + m_pstudiohdr->bonecontrollerindex);

	// find first controller that matches the index
	for (i = 0; i < m_pstudiohdr->numbonecontrollers; i++, pbonecontroller++)
	{
		if (pbonecontroller->index == iController)
			break;
	}
	if (i >= m_pstudiohdr->numbonecontrollers)
		return flValue;

	// wrap 0..360 if it's a rotational controller
	if (pbonecontroller->type & (STUDIO_XR | STUDIO_YR | STUDIO_ZR))
	{
		// ugly hack, invert value if end < start
		if (pbonecontroller->end < pbonecontroller->start)
			flValue = -flValue;

		// does the controller not wrap?
		if (pbonecontroller->start + 359.0 >= pbonecontroller->end)
		{
			if (flValue > ((pbonecontroller->start + pbonecontroller->end) / 2.0) + 180)
				flValue = flValue - 360;
			if (flValue < ((pbonecontroller->start + pbonecontroller->end) / 2.0) - 180)
				flValue = flValue + 360;
		}
		else
		{
			if (flValue > 360)
				flValue = flValue - (int)(flValue / 360.0) * 360.0;
			else if (flValue < 0)
				flValue = flValue + (int)((flValue / -360.0) + 1) * 360.0;
		}
	}

	int setting = 255 * (flValue - pbonecontroller->start) / (pbonecontroller->end - pbonecontroller->start);

	if (setting < 0) setting = 0;
	if (setting > 255) setting = 255;
	m_controller[iController] = setting;

	return setting * (1.0 / 255.0) * (pbonecontroller->end - pbonecontroller->start) + pbonecontroller->start;
}


float StudioModel::SetMouth(float flValue)
{
	mstudiobonecontroller_t* pbonecontroller = (mstudiobonecontroller_t*)((unsigned char*)m_pstudiohdr + m_pstudiohdr->bonecontrollerindex);

	// find first controller that matches the mouth
	for (int i = 0; i < m_pstudiohdr->numbonecontrollers; i++, pbonecontroller++)
	{
		if (pbonecontroller->index == 4)
			break;
	}

	// wrap 0..360 if it's a rotational controller
	if (pbonecontroller->type & (STUDIO_XR | STUDIO_YR | STUDIO_ZR))
	{
		// ugly hack, invert value if end < start
		if (pbonecontroller->end < pbonecontroller->start)
			flValue = -flValue;

		// does the controller not wrap?
		if (pbonecontroller->start + 359.0 >= pbonecontroller->end)
		{
			if (flValue > ((pbonecontroller->start + pbonecontroller->end) / 2.0) + 180)
				flValue = flValue - 360;
			if (flValue < ((pbonecontroller->start + pbonecontroller->end) / 2.0) - 180)
				flValue = flValue + 360;
		}
		else
		{
			if (flValue > 360)
				flValue = flValue - (int)(flValue / 360.0) * 360.0;
			else if (flValue < 0)
				flValue = flValue + (int)((flValue / -360.0) + 1) * 360.0;
		}
	}

	int setting = 64 * (flValue - pbonecontroller->start) / (pbonecontroller->end - pbonecontroller->start);

	if (setting < 0) setting = 0;
	if (setting > 64) setting = 64;
	m_mouth = setting;

	return setting * (1.0 / 64.0) * (pbonecontroller->end - pbonecontroller->start) + pbonecontroller->start;
}


float StudioModel::SetBlending(int iBlender, float flValue)
{
	mstudioseqdesc_t* pseqdesc;

	pseqdesc = (mstudioseqdesc_t*)((unsigned char*)m_pstudiohdr + m_pstudiohdr->seqindex) + (int)m_sequence;

	if (pseqdesc->blendtype[iBlender] == 0)
		return flValue;

	if (pseqdesc->blendtype[iBlender] & (STUDIO_XR | STUDIO_YR | STUDIO_ZR))
	{
		// ugly hack, invert value if end < start
		if (pseqdesc->blendend[iBlender] < pseqdesc->blendstart[iBlender])
			flValue = -flValue;

		// does the controller not wrap?
		if (pseqdesc->blendstart[iBlender] + 359.0 >= pseqdesc->blendend[iBlender])
		{
			if (flValue > ((pseqdesc->blendstart[iBlender] + pseqdesc->blendend[iBlender]) / 2.0) + 180)
				flValue = flValue - 360;
			if (flValue < ((pseqdesc->blendstart[iBlender] + pseqdesc->blendend[iBlender]) / 2.0) - 180)
				flValue = flValue + 360;
		}
	}

	int setting = 255 * (flValue - pseqdesc->blendstart[iBlender]) / (pseqdesc->blendend[iBlender] - pseqdesc->blendstart[iBlender]);

	if (setting < 0) setting = 0;
	if (setting > 255) setting = 255;

	m_blending[iBlender] = setting;

	return setting * (1.0 / 255.0) * (pseqdesc->blendend[iBlender] - pseqdesc->blendstart[iBlender]) + pseqdesc->blendstart[iBlender];
}



int StudioModel::SetBodygroup(int iGroup, int iValue)
{
	if (iGroup > m_pstudiohdr->numbodyparts)
		return -1;

	mstudiobodyparts_t* pbodypart = (mstudiobodyparts_t*)((unsigned char*)m_pstudiohdr + m_pstudiohdr->bodypartindex) + iGroup;

	int iCurrent = (m_bodynum / pbodypart->base) % pbodypart->nummodels;

	if (iValue >= pbodypart->nummodels)
		return iCurrent;

	m_bodynum = (m_bodynum - (iCurrent * pbodypart->base) + (iValue * pbodypart->base));

	return iValue;
}


int StudioModel::SetSkin(int iValue)
{
	if (iValue < m_pstudiohdr->numskinfamilies)
	{
		return m_skinnum;
	}

	m_skinnum = iValue;

	return iValue;
}

