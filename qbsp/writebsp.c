// writebsp.c

#include "qbsp.h"
#include "wad.h"

int firstface;
int *planemapping;

void ExportNodePlanes_r (node_t *node)
{
	plane_t		*plane;
	dplane_t	*dplane;
	int i;

	if (node->planenum == -1)
		return;
	if (planemapping[node->planenum] == -1)
	{	
		plane = &pPlanes[node->planenum];
		dplane = pWorldEnt->pPlanes;

		// search for an equivalent plane
		for (i=0; i<pWorldEnt->iPlanes; i++, dplane++)
			if (DotProduct(dplane->normal, plane->normal) > 1-0.00001 &&
				fabs(dplane->dist - plane->dist) < 0.01 &&
				dplane->type == plane->type)
				break;

		// a new plane
		planemapping[node->planenum] = i;
		
		if (i == pWorldEnt->iPlanes)
		{
			if (pWorldEnt->iPlanes >= pWorldEnt->cPlanes)
				Message(msgError, errLowPlaneCount);
			plane =	&pPlanes[node->planenum];
			dplane = pWorldEnt->pPlanes + pWorldEnt->iPlanes;
			dplane->normal[0] = plane->normal[0];
			dplane->normal[1] = plane->normal[1];
			dplane->normal[2] = plane->normal[2];
			dplane->dist = plane->dist;
			dplane->type = plane->type;

			pWorldEnt->iPlanes++;
			map.cTotal[BSPPLANE]++;
		}
	}

	node->outputplanenum = planemapping[node->planenum];
	
	ExportNodePlanes_r (node->children[0]);
	ExportNodePlanes_r (node->children[1]);
}

/*
==================
ExportNodePlanes
==================
*/
void ExportNodePlanes (node_t *nodes)
{
	// OK just need one plane array, stick it in worldmodel
	if (pWorldEnt->pPlanes == NULL)
	{
		// I'd like to use numbrushplanes here but we haven't seen every entity yet...
		pWorldEnt->cPlanes = cPlanes;
		pWorldEnt->pPlanes = (dplane_t *)AllocMem(BSPPLANE, pWorldEnt->cPlanes);
	}

	// TODO: make one-time allocation?
	planemapping = (int *)AllocMem(OTHER, sizeof(int) * pWorldEnt->cPlanes);
	memset (planemapping, -1, sizeof(int *)*pWorldEnt->cPlanes);
	ExportNodePlanes_r (nodes);
	FreeMem(planemapping, OTHER, sizeof(int) * pWorldEnt->cPlanes);
}

//===========================================================================


/*
==================
CountClipNodes_r
==================
*/
void CountClipNodes_r (node_t *node)
{
	if (node->planenum == -1)
		return;
	
	pCurEnt->cClipnodes++;

	CountClipNodes_r(node->children[0]);
	CountClipNodes_r(node->children[1]);
}

/*
==================
ExportClipNodes_r
==================
*/
int ExportClipNodes_r (node_t *node)
{
	int			i, c;
	dclipnode_t	*cn;
	face_t *f, *next;
	
	// FIXME: free more stuff?	
	if (node->planenum == -1)
	{
		c = node->contents;
		FreeMem(node, NODE);
		return c;
	}
	
	// emit a clipnode
	c = map.cTotal[BSPCLIPNODE];
	cn = pCurEnt->pClipnodes + pCurEnt->iClipnodes;
	pCurEnt->iClipnodes++;
	map.cTotal[BSPCLIPNODE]++;

	cn->planenum = node->outputplanenum;
	for (i=0 ; i<2 ; i++)
		cn->children[i] = ExportClipNodes_r(node->children[i]);
	
	for (f = node->faces; f; f = next)
	{
		next = f->next;
		memset(f, 0, sizeof(face_t));
		FreeMem(f, FACE);
	}
	FreeMem(node, NODE);
	return c;
}

/*
==================
ExportClipNodes

Called after the clipping hull is completed.  Generates a disk format
representation and frees the original memory.

This gets real ugly.  Gets called twice per entity, once for each clip hull.  First time
just store away data, second time fix up reference points to accomodate new data
interleaved with old.
==================
*/
void ExportClipNodes (node_t *nodes)
{
	int oldcount, i, diff;
	int clipcount = 0;
	dclipnode_t *pTemp;

	oldcount = pCurEnt->cClipnodes;

	// Count nodes before this one
	for (i=0; i < map.iEntities; i++)
		clipcount += map.rgEntities[i].cClipnodes;
	pCurEnt->pModels->headnode[hullnum] = clipcount+oldcount;

	CountClipNodes_r(nodes);

	if (pCurEnt->cClipnodes > MAX_BSP_CLIPNODES)
		Message(msgError, errTooManyClipnodes);
	pTemp = pCurEnt->pClipnodes;
	pCurEnt->pClipnodes = (dclipnode_t *)AllocMem(BSPCLIPNODE, pCurEnt->cClipnodes);
	if (pTemp != NULL)
	{
		memcpy(pCurEnt->pClipnodes, pTemp, oldcount*rgcMemSize[BSPCLIPNODE]);
		FreeMem(pTemp, BSPCLIPNODE, oldcount);

		// Worthwhile to special-case this for entity 0 (no modification needed)
		diff = clipcount - pCurEnt->pModels->headnode[1];
		if (diff != 0)
		{
			pCurEnt->pModels->headnode[1] += diff;
			for (i=0; i<oldcount; i++)
			{
				pTemp = pCurEnt->pClipnodes+i;
				if (pTemp->children[0] >= 0)
					pTemp->children[0] += diff;
				if (pTemp->children[1] >= 0)
					pTemp->children[1] += diff;
			}
		}
	}

	map.cTotal[BSPCLIPNODE] = clipcount + oldcount;
	ExportClipNodes_r(nodes);
}

//===========================================================================


/*
==================
CountLeaves
==================
*/
void CountLeaves (node_t *node)
{
	face_t **fp, *f;

	pCurEnt->cLeaves++;
	for (fp=node->markfaces; *fp; fp++)
		for (f=*fp; f; f=f->original)
			pCurEnt->cMarksurfaces++;
}

/*
==================
CountNodes_r
==================
*/
void CountNodes_r (node_t *node)
{
	int		i;

	pCurEnt->cNodes++;

	for (i=0; i<2; i++)
	{
		if (node->children[i]->planenum == -1)
		{
			if (node->children[i]->contents != CONTENTS_SOLID)
				CountLeaves(node->children[i]);
		}
		else
			CountNodes_r (node->children[i]);
	}
}

/*
==================
CountNodes
==================
*/
void CountNodes (node_t *headnode)
{
	if (headnode->contents < 0)	
		CountLeaves(headnode);
	else
		CountNodes_r(headnode);
}

/*
==================
ExportLeaf
==================
*/
void ExportLeaf (node_t *node)
{
	face_t **fp, *f;
	dleaf_t *leaf_p;

	// ptr arithmetic to get correct leaf in memory
	leaf_p = pCurEnt->pLeaves + pCurEnt->iLeaves;
	pCurEnt->iLeaves++;
	map.cTotal[BSPLEAF]++;

	leaf_p->contents = node->contents;

//
// write bounding box info
//	
	// VectorCopy don't work since dest are shorts
	leaf_p->mins[0] = (short)node->mins[0];
	leaf_p->mins[1] = (short)node->mins[1];
	leaf_p->mins[2] = (short)node->mins[2];
	leaf_p->maxs[0] = (short)node->maxs[0];
	leaf_p->maxs[1] = (short)node->maxs[1];
	leaf_p->maxs[2] = (short)node->maxs[2];
	
	leaf_p->visofs = -1;	// no vis info yet
	
	// write the marksurfaces
	leaf_p->firstmarksurface = map.cTotal[BSPMARKSURF];

	for (fp=node->markfaces ; *fp ; fp++)
	{
		// emit a marksurface
		f = *fp;
		do
		{
			*(pCurEnt->pMarksurfaces + pCurEnt->iMarksurfaces) = f->outputnumber;
			pCurEnt->iMarksurfaces++;
			map.cTotal[BSPMARKSURF]++;
			f=f->original;		// grab tjunction split faces
		} while (f);
	}

	leaf_p->nummarksurfaces = map.cTotal[BSPMARKSURF] - leaf_p->firstmarksurface;
}


/*
==================
ExportDrawNodes_r
==================
*/
void ExportDrawNodes_r (node_t *node)
{
	dnode_t	*n;
	int		i;

	// ptr arithmetic to get correct node in memory
	n = pCurEnt->pNodes + pCurEnt->iNodes;
	pCurEnt->iNodes++;
	map.cTotal[BSPNODE]++;

	// VectorCopy doesn't work since dest are shorts
	n->mins[0] = (short)node->mins[0];
	n->mins[1] = (short)node->mins[1];
	n->mins[2] = (short)node->mins[2];
	n->maxs[0] = (short)node->maxs[0];
	n->maxs[1] = (short)node->maxs[1];
	n->maxs[2] = (short)node->maxs[2];

	n->planenum = node->outputplanenum;
	n->firstface = node->firstface;
	n->numfaces = node->numfaces;

	// recursively output the other nodes
	for (i=0; i<2; i++)
	{
		if (node->children[i]->planenum == -1)
		{
			if (node->children[i]->contents == CONTENTS_SOLID)
				n->children[i] = -1;
			else
			{
				n->children[i] = -(map.cTotal[BSPLEAF] + 1);
				ExportLeaf (node->children[i]);
			}
		}
		else
		{
			n->children[i] = map.cTotal[BSPNODE];
			ExportDrawNodes_r (node->children[i]);
		}
	}
}

/*
==================
ExportDrawNodes
==================
*/
void ExportDrawNodes (node_t *headnode)
{
	int i;
	dmodel_t *bm;

	// Get a feel for how many of these things there are.
	CountNodes(headnode);

	// emit a model
	pCurEnt->pNodes = (dnode_t *)AllocMem(BSPNODE, pCurEnt->cNodes);
	pCurEnt->pLeaves = (dleaf_t *)AllocMem(BSPLEAF, pCurEnt->cLeaves);
	pCurEnt->pMarksurfaces = (unsigned short *)AllocMem(BSPMARKSURF, pCurEnt->cMarksurfaces);

	// Set leaf 0 properly (must be solid).  cLeaves etc incremented in BeginBSPFile.
	pWorldEnt->pLeaves->contents = CONTENTS_SOLID;

	bm = pCurEnt->pModels;
	
	bm->headnode[0] = map.cTotal[BSPNODE];
	bm->firstface = firstface;
	bm->numfaces = map.cTotal[BSPFACE] - firstface;	
	firstface = map.cTotal[BSPFACE];
	
	if (headnode->contents < 0)	
		ExportLeaf (headnode);
	else
		ExportDrawNodes_r (headnode);

	// Not counting initial vis leaf
	bm->visleafs = pCurEnt->cLeaves;
	if (map.iEntities == 0)
		bm->visleafs--;

	for (i=0 ; i<3 ; i++)
	{
		bm->mins[i] = headnode->mins[i] + SIDESPACE + 1;	// remove the padding
		bm->maxs[i] = headnode->maxs[i] - SIDESPACE - 1;
	}
}

//=============================================================================

/*
==================
BeginBSPFile
==================
*/
void BeginBSPFile (void)
{
	firstface = 0;

	// First edge must remain unused because 0 can't be negated
	pWorldEnt->cEdges++;
	pWorldEnt->iEdges++;
	map.cTotal[BSPEDGE]++;

	// Leave room for leaf 0 (must be solid)
	pWorldEnt->cLeaves++;
	pWorldEnt->iLeaves++;
	map.cTotal[BSPLEAF]++;
}


/*
==================
FinishBSPFile
==================
*/
void FinishBSPFile (void)
{
	dplane_t *pTemp;

	options.fVerbose = true;
	Message(msgProgress, "WriteBSPFile");

	// TODO: Fix this somewhere else?
	pTemp = (dplane_t *)AllocMem(BSPPLANE, map.cTotal[BSPPLANE]);
	memcpy(pTemp, pWorldEnt->pPlanes, map.cTotal[BSPPLANE] * rgcMemSize[BSPPLANE]);
	FreeMem(pWorldEnt->pPlanes, BSPPLANE, pWorldEnt->cPlanes);
	pWorldEnt->pPlanes = pTemp;
	pWorldEnt->cPlanes = map.cTotal[BSPPLANE];

	PrintBSPFileSizes ();
	WriteBSPFile();

	options.fVerbose = options.fAllverbose;
}

