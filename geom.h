/*************************************************************************
 *
 *  Copyright (c) 2019 Rajit Manohar
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA  02110-1301, USA.
 *
 **************************************************************************
 */
#ifndef __ACT_GEOM_H__
#define __ACT_GEOM_H__

#include <act/act.h>
#include <act/tech.h>
#include <act/passes/netlist.h>
#include "tile.h"


class TransformMat {
  long m[3][3];
public:
  TransformMat ();

  void mkI();

  void applyRot90 ();
  void applyTranslate (long dx, long dy);
  void mirrorLR ();
  void mirrorTB ();
  void inverse (TransformMat *m);

  void apply (long inx, long iny, long *outx, long *outy);
};


class Layer {
protected:
  Material *mat;		/* technology-specific
				   information. routing material for
				   the layer. */
  Tile *hint;			/* tile containing 0,0 / last lookup */

  Tile *vhint;			// tile layer containing vias to the
 				// next (upper) layer
  
  Layer *up, *down;		/* layer above and below */
  
  Material **other;	       // for the base layer, fet + diff
  int nother;

  netlist_t *N;

  unsigned int bbox:1;		// 1 if bbox below is valid
  long _llx, _lly, _urx, _ury;
  long _bllx, _blly, _burx, _bury; // bloated bbox
  /* BBox with spacing on all sides 
     This bloats the bounding box by the minimum spacing on all sides.
  */

 public:
  Layer (Material *, netlist_t *);
  ~Layer ();

  void allocOther (int sz);
  void setOther (int idx, Material *m);
  void setDownLink (Layer *x);

  int Draw (long llx, long lly, unsigned long wx, unsigned long wy, void *net, int type = 0);
  int Draw (long llx, long lly, unsigned long wx, unsigned long wy, int type = 0);
  int DrawVirt (int flavor, int type, long llx, long lly, unsigned long wx, unsigned long wy);

  int drawVia (long llx, long lly, unsigned long wx, unsigned long wy, void *net, int type = 0);
  int drawVia (long llx, long lly, unsigned long wx, unsigned long wy, int type = 0);

  int isMetal ();		// 1 if it is a metal layer or a via
				// layer

  void markPins (void *net, int isinput); // mark pin tiles
  
  list_t *searchMat (void *net);
  list_t *searchMat (int attr);
  list_t *searchVia (void *net);
  list_t *searchVia (int attr);
  list_t *allNonSpaceMat ();
  list_t *allNonSpaceVia ();	// looks at "up" vias only


  void getBBox (long *llx, long *lly, long *urx, long *ury);
  void getBloatBBox (long *llx, long *lly, long *urx, long *ury);

  void PrintRect (FILE *fp, TransformMat *t = NULL);

  const char *getRouteName() {
    RoutingMat *rmat = dynamic_cast<RoutingMat *> (mat);
    if (rmat) {
      return rmat->getLEFName();
    }
    else {
      return mat->getName();
    }
  }
  const char *getViaName() { return ((RoutingMat *)mat)->getUpC()->getName(); } 

  Tile *find (long x, long y);

  friend class Layout;
};


class Layout {
public:
  static bool _initdone;
  static void Init();
  static double getLeakAdjust () { return _leak_adjust; }
  
  /* 
     The base layer is special as this is where the transistors are
     drawn. It includes poly, fets, diffusion, and virtual diffusion.
  */
  Layout (netlist_t *);
  ~Layout();

  int DrawPoly (long llx, long lly, unsigned long wx, unsigned long wy, void *net);
  int DrawDiff (int flavor, int type, long llx, long lly, unsigned long wx, unsigned long wy, void *net);
  int DrawWellDiff (int flavor, int type, long llx, long lly, unsigned long wx, unsigned long wy, void *net);
  int DrawFet (int flavor, int type, long llx, long lly, unsigned long wx, unsigned long wy, void *net);
  int DrawDiffBBox (int flavor, int type, long llx, long lly, unsigned long wx, unsigned long wy);

  /* 0 = metal1, etc. */
  int DrawMetal (int num, long llx, long lly, unsigned long wx, unsigned long wy, void *net);
  
  int DrawMetalPin (int num, long llx, long lly,
		    unsigned long wx, unsigned long wy,
		    void *net, int dir); /* dir 0 = input, 1 = output */

  /* 0 = base to metal1, 1 = metal1 to metal2, etc. */
  int DrawVia (int num, long llx, long lly, unsigned long wx, unsigned long wy);

  Layer *getLayerPoly () { return base; }
  Layer *getLayerDiff () { return base; }
  Layer *getLayerWell () { return base; }
  Layer *getLayerFet ()  { return base; }
  Layer *getLayerMetal (int n) { return metals[n]; }


  void markPins ();
  

  PolyMat *getPoly ();
  FetMat *getFet (int type, int flavor = 0); // type == EDGE_NFET or EDGE_PFET
  DiffMat *getDiff (int type, int flavor = 0);
  WellMat *getWell (int type, int flavor = 0);
  // NOTE: WELL TYPE is NOT THE TYPE OF THE WELL MATERIAL, BUT THE TYPE OF
  // THE FET THAT NEEDS THE WELL.

  void getBBox (long *llx, long *lly, long *urx, long *ury);
  void getBloatBBox (long *llx, long *lly, long *urx, long *ury);

  void PrintRect (FILE *fp, TransformMat *t = NULL);
  void ReadRect (const char *file, int raw_mode = 0);

  list_t *search (void *net);
  list_t *search (int attr);
  list_t *searchAllMetal ();
  
  void propagateAllNets();

  bool readRectangles() { return _readrect; }

  void flushBBox() { _rllx = 0; _rlly = 0; _rurx = -1; _rury = -1; }

  double leak_adjust() {
    if (!N->leak_correct) { return 0.0; }
    else { return _leak_adjust; }
  }

  node_t *getVdd() { return N->Vdd; }
  node_t *getGND() { return N->GND; }

private:
  bool _readrect;
  long _rllx, _rlly, _rurx, _rury;
  Layer *base;
  Layer **metals;
  int nflavors;
  int nmetals;
  netlist_t *N;
  struct Hashtable *lmap;	// map from layer string to base layer
				// name

  static double _leak_adjust;
};


class LayoutBlob;

enum blob_type { BLOB_BASE,  /* some layout */
		 BLOB_MACRO, /* macro */
		 BLOB_HORIZ, /* horizontal composition */
		 BLOB_VERT,  /* vertical composition */
		 BLOB_MERGE  /* overlay */
};

enum mirror_type { MIRROR_NONE, MIRROR_LR, MIRROR_TB, MIRROR_BOTH };
   // do we want to support 90 degree rotation?

struct blob_list {
  LayoutBlob *b;
  long gap;
  long shift;			// shift in the other direction
  mirror_type mirror;
  struct blob_list *next;
};

struct tile_listentry {
  TransformMat m;  /**< the coordinate transformation matrix */
  list_t *tiles;   /**< a list alternating between Layer pointer and a
		      list of tiles  */
};

class LayoutEdgeAttrib {

private:
  // well attributes
  struct well_info {
    int offset;
    unsigned int plugged:1;
    unsigned int flavor:7;
  } *wells;
  int wellcnt;

  struct mat_info {
    int offset;
    int width;
    Material *m;
  } *mats;
  int matcnt;

public:
  int getWellCount () { return wellcnt; }

};

class LayoutBlob {
private:
  union {
    struct {
      blob_list *hd, *tl;
    } l;			// a blob list
    struct {
      Layout *l;		// ... layout block
    } base;
    ExternMacro *macro;
  };
  blob_type t;			// type field: 0 = base, 1 = horiz,
				// 2 = vert

  long llx, lly, urx, ury;	// bounding box
  long bloatllx, bloatlly, bloaturx, bloatury; // bloated bounding box

#define LAYOUT_EDGE_LEFT 0  
#define LAYOUT_EDGE_RIGHT 2
#define LAYOUT_EDGE_TOP 1
#define LAYOUT_EDGE_BOTTOM 3

  LayoutEdgeAttrib *edges[4];	// 0 = l, 1 = t, 2 = r, 3 = b

  unsigned long count;

  bool readRect;

  void _printRect (FILE *fp, TransformMat *t);
  
public:
  LayoutBlob (blob_type type, Layout *l = NULL);
  LayoutBlob (ExternMacro *m);
  ~LayoutBlob ();

  int isMacro() { return t == BLOB_MACRO ? true : false; }
  const char *getMacroName() { return macro->getName(); }
  const char *getLEFFile() { return macro->getLEFFile(); }

  void applyTransform (TransformMat *t);

  void appendBlob (LayoutBlob *b, long gap = 0, mirror_type m = MIRROR_NONE);

  void markRead () { readRect = true; }
  bool getRead() { return readRect; }
  
  void PrintRect (FILE *fp, TransformMat *t = NULL);

  /**
   * Computes the actual bounding box of the layout blob
   *
   * @param llxp, llyp, urxp, uryp are used to return the boundary.
   */
  void getBBox (long *llxp, long *llyp, long *urxp, long *uryp);
  void getBloatBBox (long *llxp, long *llyp, long *urxp, long *uryp);

  /**
   * Set bounding box: only applies to BLOB_BASE with no layout 
   */
  void setBBox (long _llx, long _lly, long _urx, long _ury);

  /**
   * Remove any bounding box blobs. Returns updated blob.
   * If it is called on base bbox blob, then it returns NULL after
   * deleting it.
   */
  static LayoutBlob *delBBox (LayoutBlob *b);

  /**
   * Returns a list of tiles in the layout that match the net
   *  @param net is the net pointer (a node_t)
   *  @param m should not be used at the top-level, but provides the
   *  current transformatiom matrix used by the recursive call to the
   *  search function.
   *  @return a list_t of tile_listentry tiles.
   */
  list_t *search (void *net, TransformMat *m = NULL);
  list_t *search (int type, TransformMat *m = NULL); // this is for
						     // base layers
  list_t *searchAllMetal (TransformMat *m = NULL);

  /* 
   * Uses the return value from the search function and returns its
   * bounding box
   */
  static void searchBBox (list_t *slist, long *bllx, long *blly, long *burx,
			  long *bury);
  static void searchFree (list_t *tiles);

  /**
   *  Calculate the edge alignment between two edge atttributes
   *  @return 0 if there is no possible alignment,
   *          1 if any alignment is fine,
   *	      2 if the alignment is specified by the range d1 to d2
  */
  int GetAlignment (LayoutEdgeAttrib *a1, LayoutEdgeAttrib *a2,
		    int *d1, int *d2);


  /**
   * Stats 
   */
  void incCount () { count++; }
  unsigned long getCount () { return count; }
  
};


#endif /* __ACT_GEOM_H__ */
