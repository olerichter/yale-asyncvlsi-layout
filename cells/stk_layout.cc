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
#include <stdio.h>
#include <act/layout/stk_pass.h>
#include <act/layout/stk_layout.h>
#include <act/iter.h>
#include <config.h>
#include <string>

static unsigned long snap_to (unsigned long w, unsigned long pitch)
{
  if (w % pitch != 0) {
    w += pitch - (w % pitch);
  }
  return w;
}

ActStackLayoutPass::ActStackLayoutPass(Act *a) : ActPass (a, "stk2layout")
{
  layoutmap = NULL;
  
  if (!a->pass_find ("net2stk")) {
    stk = new ActStackPass (a);
  }
  AddDependency ("net2stk");

  ActPass *pass = a->pass_find ("net2stk");
  Assert (pass, "Hmm...");
  stk = dynamic_cast<ActStackPass *>(pass);
  Assert (stk, "Hmm too...");
}

void ActStackLayoutPass::cleanup()
{
  if (layoutmap) {
    std::map<Process *, LayoutBlob *>::iterator it;
    for (it = (*layoutmap).begin(); it != (*layoutmap).end(); it++) {
      /* something here */
    }
    delete layoutmap;
  }
}

ActStackLayoutPass::~ActStackLayoutPass()
{
  cleanup();
}

#define EDGE_FLAGS_LEFT 0x1
#define EDGE_FLAGS_RIGHT 0x2

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif


#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

struct BBox {
  int flavor;
  struct {
    int llx, lly, urx, ury;
  } p, n;
};


/* calculate actual edge width */
static int _fold_n_width, _fold_p_width, _min_width;
static int getwidth (int idx, edge_t *e)
{
  if (e->type == EDGE_NFET) {
    if (_fold_n_width != 0) {
      return EDGE_WIDTH (e,idx,_fold_n_width,_min_width);
    }
    else {
      return e->w;
    }
  }
  else {
    if (_fold_p_width != 0) {
      return EDGE_WIDTH (e,idx,_fold_p_width,_min_width);
    }
    else {
      return e->w;
    }
  }
}


static void update_bbox (BBox *cur, int type, int x, int y, int rx, int ry)
{
  if (type == EDGE_PFET) {				
    if (cur->p.llx >= cur->p.urx || cur->p.lly >= cur->p.ury) {	
      cur->p.llx = MIN(x,rx);				
      cur->p.lly = MIN(y,ry);				
      cur->p.urx = MAX(x,rx);				
      cur->p.ury = MAX(y,ry);				
    }							
    else {						
      cur->p.llx = MIN(cur->p.llx,x);			
      cur->p.llx = MIN(cur->p.llx,rx);			
      cur->p.lly = MIN(cur->p.lly,y);			
      cur->p.lly = MIN(cur->p.lly,ry);			
      cur->p.urx = MAX(cur->p.urx,x);			
      cur->p.urx = MAX(cur->p.urx,rx);			
      cur->p.ury = MAX(cur->p.ury,y);			
      cur->p.ury = MAX(cur->p.ury,ry);			
    }							
  } else {						
    if (cur->n.llx >= cur->n.urx || cur->n.lly >= cur->n.ury) {	
      cur->n.llx = MIN(x,rx);				
      cur->n.lly = MIN(y,ry);				
      cur->n.urx = MAX(x,rx);				
      cur->n.ury = MAX(y,ry);				
    }							
    else {						
      cur->n.llx = MIN(cur->n.llx,x);			
      cur->n.llx = MIN(cur->n.llx,rx);			
      cur->n.lly = MIN(cur->n.lly,y);			
      cur->n.lly = MIN(cur->n.lly,ry);			
      cur->n.urx = MAX(cur->n.urx,x);			
      cur->n.urx = MAX(cur->n.urx,rx);			
      cur->n.ury = MAX(cur->n.ury,y);			
      cur->n.ury = MAX(cur->n.ury,ry);			
    }							
  }							
}


/*
  emits rectangles needed upto the FET.
  If it is right edge, also emits the final diffusion of the right edge.
*/
static int locate_fetedge (Layout *L, int dx,
			   unsigned int flags,
			   edge_t *prev, int previdx,
			   node_t *left, edge_t *e, int eidx)
{
  DiffMat *d;
  FetMat *f;
  PolyMat *p;
  int rect;
  int fet_type; /* -1 = downward notch, +1 = upward notch, 0 = same
		   width */

  int e_w = getwidth (0, e);

  /* XXX: THIS CODE IS COPIED FROM emit_rectangle!!!!! */

  d = L->getDiff (e->type, e->flavor);
  f = L->getFet (e->type, e->flavor);
  p = L->getPoly ();

  rect = 0;
  if (flags & EDGE_FLAGS_LEFT) {
    fet_type = 0;
    /* actual overhang rule */
    rect = d->effOverhang (e_w, left->contact);
  }
  else {
    Assert (prev, "Hmm");
    int prev_w = getwidth (previdx, prev);

    if (prev_w == e_w) {
      fet_type = 0;
      rect = f->getSpacing (e_w);
      if (left->contact) {
	rect = MAX (rect, d->viaSpaceMid());
      }
    }
    else if (prev_w < e_w) {
      /* upward notch */
      fet_type = 1;
      rect = d->getNotchSpacing();
      if (left->contact &&
	  (rect + d->effOverhang (e_w) < d->viaSpaceMid())) {
	rect = d->viaSpaceMid() - d->effOverhang (e_w);
      }
    }
    else {
      fet_type = -1;
      rect = d->effOverhang (e_w);
    }
  }

  Assert (rect > 0, "FIX FOR FINFETS!");

  dx += rect;

  if (fet_type != 0) {
    if (fet_type < 0) {
      /* down notch */
      rect = d->getNotchSpacing();
      if (left->contact &&
	  (rect + d->effOverhang (e_w) < d->viaSpaceMid())) {
	rect = d->viaSpaceMid() - d->effOverhang (e_w);
      }
    }
    else {
      /* up notch */
      rect = d->effOverhang (e_w);
    } 
    dx += rect;
  }

  return dx;
}


/*
  emits rectangles needed upto the FET.
  If it is right edge, also emits the final diffusion of the right edge.
*/
static int emit_rectangle (Layout *L,
			   int pad,
			   int dx, int dy,
			   unsigned int flags,
			   edge_t *prev, int previdx,
			   node_t *left, edge_t *e, int eidx, int yup,
			   BBox *ret)
{
  DiffMat *d;
  FetMat *f;
  PolyMat *p;
  int rect;
  int fet_type; /* -1 = downward notch, +1 = upward notch, 0 = same
		   width */

  BBox b;

  int e_w = getwidth (eidx, e);
  
  if (ret) {
    b = *ret;
  }
  else {
    b.p.llx = 0;
    b.p.lly = 0;
    b.p.urx = 0;
    b.p.ury = 0;
    b.n = b.p;
  }

#define RECT_UPDATE(type,x,y,rx,ry)	update_bbox(&b,type,x,y,rx,ry)

  /* XXX: THIS CODE GETS COPIED TO locate_fetedge!!!! */
  
  d = L->getDiff (e->type, e->flavor);
  f = L->getFet (e->type, e->flavor);
  p = L->getPoly ();
  b.flavor = e->flavor;

  int prev_w = 0;

  rect = 0;
  if (flags & EDGE_FLAGS_LEFT) {
    fet_type = 0;
    /* actual overhang rule */
    rect = d->effOverhang (e_w, left->contact);
  }
  else {
    Assert (prev, "Hmm");
    prev_w = getwidth (previdx, prev);
    
    if (prev_w == e_w) {
      fet_type = 0;
      rect = f->getSpacing (e_w);
      if (left->contact) {
	rect = MAX (rect, d->viaSpaceMid());
      }
    }
    else if (prev_w < e_w) {
      /* upward notch */
      fet_type = 1;
      rect = d->getNotchSpacing();
      if (left->contact &&
	  (rect + d->effOverhang (e_w) < d->viaSpaceMid())) {
	rect = d->viaSpaceMid() - d->effOverhang (e_w);
      }
    }
    else {
      fet_type = -1;
      rect = d->effOverhang (e_w);
    }
  }

  Assert (rect > 0, "FIX FOR FINFETS!");

  if (fet_type != -1) {
    rect += pad;
    pad = 0;
  }

  if (fet_type == 0) {
    if (yup < 0) {
      L->DrawDiff (e->flavor, e->type, dx, dy + yup*e_w, rect, -yup*e_w,
		   left->contact ? left : NULL);
    }
    else {
      L->DrawDiff (e->flavor, e->type, dx, dy, rect, yup*e_w,
		   left->contact ? left : NULL);
    }
    RECT_UPDATE(e->type, dx, dy, dx+rect, dy + yup*e_w);
  }
  else {
    if (yup < 0) {
      L->DrawDiff (e->flavor, e->type, dx, dy + yup*prev_w, rect, -yup*prev_w,
		   left->contact ? left : NULL);
    }
    else {
      L->DrawDiff (e->flavor, e->type, dx, dy, rect, yup*prev_w,
		   left->contact ? left : NULL);
    }
    RECT_UPDATE(e->type, dx,dy,dx+rect, dy + yup*prev_w);
  }
  dx += rect;

  if (fet_type != 0) {
    if (fet_type < 0) {
      /* down notch */
      rect = d->getNotchSpacing();
      if (left->contact &&
	  (rect + d->effOverhang (e_w) < d->viaSpaceMid())) {
	rect = d->viaSpaceMid() - d->effOverhang (e_w);
      }
    }
    else {
      /* up notch */
      rect = d->effOverhang (e_w);
    }
    rect += pad;
    pad = 0;
    if (yup < 0) {
      L->DrawDiff (e->flavor, e->type, dx, dy + yup*e_w, rect, -yup*e_w,
		   NULL);
    }
    else {
      L->DrawDiff (e->flavor, e->type, dx, dy, rect, yup*e_w, NULL);
    }
    RECT_UPDATE (e->type, dx,dy,dx+rect,dy+yup*e_w);
    dx += rect;
  }

  /* now print fet */
  if (yup < 0) {
    L->DrawFet (e->flavor, e->type, dx, dy + yup*e_w, e->l, -yup*e_w, NULL);
  }
  else {
    L->DrawFet (e->flavor, e->type, dx, dy, e->l, yup*e_w, NULL);
  }

  int poverhang = p->getOverhang (e->l);
  int uoverhang = poverhang;

  if (fet_type != 0) {
    uoverhang = MAX (uoverhang, p->getNotchOverhang (e->l));
  }

  /* now print poly edges */
  if (yup < 0) {
    L->DrawPoly (dx, dy, e->l, -yup*poverhang, e->g);
    L->DrawPoly (dx, dy + yup*(e_w+uoverhang), e->l, -yup*uoverhang, NULL);
  }
  else {
    L->DrawPoly (dx, dy - yup*poverhang, e->l, yup*poverhang, e->g);
    L->DrawPoly (dx, dy + yup*e_w, e->l, yup*uoverhang, NULL);
  }
  

  dx += e->l;
  
  if (flags & EDGE_FLAGS_RIGHT) {
    node_t *right;

    if (left == e->a) {
      right = e->b;
    }
    else {
      right = e->a;
    }
    rect = d->effOverhang (e_w, right->contact);

    if (yup < 0) {
      L->DrawDiff (e->flavor, e->type, dx, dy + yup*e_w, rect, -yup*e_w, right);
    }
    else {
      L->DrawDiff (e->flavor, e->type, dx, dy, rect, yup*e_w, right);
    }
    RECT_UPDATE (e->type, dx,dy,dx+rect,dy+yup*e_w);
    dx += rect;
  }

  if (ret) {
    *ret = b;
  }

  return dx;
}

static BBox print_dualstack (Layout *L, struct gate_pairs *gp)
{
  int flavor;
  int xpos, xpos_p;
  int diffspace;
  BBox b;
  int dx = 0;
  
  if (gp->basepair) {
    flavor = gp->u.e.n->flavor;
  }
  else {
    struct gate_pairs *tmp;
    tmp = (struct gate_pairs *) list_value (list_first (gp->u.gp));
    if (tmp->u.e.n) {
      flavor = tmp->u.e.n->flavor;
    }
    else {
      Assert (tmp->u.e.p, "Hmm");
      flavor = tmp->u.e.p->flavor;
    }
  }

  DiffMat *ndiff = Technology::T->diff[EDGE_NFET][flavor];
  DiffMat *pdiff = Technology::T->diff[EDGE_PFET][flavor];

  diffspace = ndiff->getOppDiffSpacing (flavor);
  Assert (diffspace == pdiff->getOppDiffSpacing (flavor), "Hmm?!");

  FetMat *nfet = Technology::T->fet[EDGE_NFET][flavor];
  FetMat *pfet = Technology::T->fet[EDGE_PFET][flavor];

  PolyMat *poly = Technology::T->poly;

  /* ok, now we can draw! */
  Assert (nfet && pfet && poly && ndiff && pdiff, "What?");

  xpos = dx;
  xpos_p = dx;

  b.p.llx = dx;
  b.p.lly = 0;
  b.p.urx = dx;
  b.p.ury = 0;
  b.n = b.p;

  int padn, padp;
  int fposn, fposp;

  int yp = +diffspace/2;
  int yn = yp - diffspace;
  
  if (gp->basepair) {
    fposn = locate_fetedge (L, xpos, EDGE_FLAGS_LEFT|EDGE_FLAGS_RIGHT,
			    NULL, 0, gp->l.n, gp->u.e.n, gp->n_start);
    fposp = locate_fetedge (L, xpos, EDGE_FLAGS_LEFT|EDGE_FLAGS_RIGHT,
			    NULL, 0, gp->l.p, gp->u.e.p, gp->p_start);
    
    if (fposn > fposp) {
      padp = fposn - fposp;
      padn = 0;
    }
    else {
      padn = fposp - fposn;
      padp = 0;
    }

    xpos = emit_rectangle (L, padn, xpos, yn,
			   EDGE_FLAGS_LEFT|EDGE_FLAGS_RIGHT,
			   NULL, 0,
			   gp->l.n, gp->u.e.n, gp->n_start, -1, &b);
    
    xpos_p = emit_rectangle (L, padp, xpos_p, yp,
			     EDGE_FLAGS_LEFT|EDGE_FLAGS_RIGHT,
			     NULL, 0,
			     gp->l.p, gp->u.e.p, gp->p_start, 1, &b);
  }
  else {
    listitem_t *li;
    int firstp = 1, firstn = 1;
    edge_t *prevp = NULL, *prevn = NULL;
    int prevpidx = 0, prevnidx = 0;
    node_t *leftp, *leftn;

    leftp = NULL;
    leftn = NULL;

    for (li = list_first (gp->u.gp); li; li = list_next (li)) {
      struct gate_pairs *tmp;
      unsigned int flagsp = 0, flagsn = 0;
      tmp = (struct gate_pairs *) list_value (li);

      Assert (tmp->basepair, "Hmm");
      
      if (firstp && tmp->u.e.p) {
	flagsp |= EDGE_FLAGS_LEFT;
	firstp = 0;
      }
      if (firstn && tmp->u.e.n) {
	firstn = 0;
	flagsn |= EDGE_FLAGS_LEFT;
      }
      if (!list_next (li)) {
	flagsp |= EDGE_FLAGS_RIGHT;
	flagsn |= EDGE_FLAGS_RIGHT;
      }
      else {
	struct gate_pairs *tnext;
	tnext = (struct gate_pairs *) list_value (list_next (li));
	if (!tnext->u.e.p) {
	  flagsp |= EDGE_FLAGS_RIGHT;
	}
	if (!tnext->u.e.n) {
	  flagsn |= EDGE_FLAGS_RIGHT;
	}
      }

      if (tmp->u.e.n) {
	if (!leftn) {
	  leftn = gp->l.n;
	}
	else {
	  Assert (prevn, "Hmm");
	  if (prevn->a == leftn) {
	    leftn = prevn->b;
	  }
	  else {
	    Assert (prevn->b == leftn, "Hmm");
	    leftn = prevn->a;
	  }
	}
      }
      if (tmp->u.e.p) {
	if (!leftp) {
	  leftp  = gp->l.p;
	}
	else {
	  Assert (prevp, "Hmm");
	  if (prevp->a == leftp) {
	    leftp = prevp->b;
	  }
	  else {
	    Assert (prevp->b == leftp, "Hmm");
	    leftp = prevp->a;
	  }
	}
      }

      /* compute padding */
      padn = 0;
      padp = 0;
      if (tmp->u.e.n && tmp->u.e.p) {
	fposn = locate_fetedge (L, xpos, flagsn,
				prevn, prevnidx, leftn, tmp->u.e.n,
				tmp->n_start);
	fposp = locate_fetedge (L, xpos_p, flagsp,
				prevp, prevpidx, leftp, tmp->u.e.p,
				tmp->p_start);
	if (fposn > fposp) {
	  padp = padp + fposn - fposp;
	}
	else {
	  padn = padn + fposp - fposn;
	}
      }
      
      if (tmp->u.e.n) {
	xpos = emit_rectangle (L, padn, xpos, yn, flagsn,
			       prevn, prevnidx, leftn, tmp->u.e.n,
			       tmp->n_start, -1, &b);
	prevn = tmp->u.e.n;
	prevnidx = tmp->n_start;
	if (!tmp->u.e.p) {
	  xpos_p = xpos;
	}
      }
      
      if (tmp->u.e.p) {
	xpos_p = emit_rectangle (L, padp, xpos_p, yp, flagsp,
				 prevp, prevpidx, leftp, tmp->u.e.p,
				 tmp->p_start, 1, &b);

	prevp = tmp->u.e.p;
	prevpidx = tmp->p_start;
	if (!tmp->u.e.n) {
	  xpos = xpos_p;
	}
      }
      
    }
  }
  return b;
}


static BBox print_singlestack (Layout *L, list_t *l)
{
  int flavor;
  int type;
  node_t *n;
  edge_t *e;
  edge_t *prev;
  int xpos = 0;
  int ypos = 0;
  BBox b;
  int idx = 0;
  int previdx = 0;

  b.p.llx = 0;
  b.p.lly = 0;
  b.p.urx = 0;
  b.p.ury = 0;
  b.n = b.p;

  if (list_length (l) < 4) return b;

  n = (node_t *) list_value (list_first (l));
  e = (edge_t *) list_value (list_next (list_first (l)));
  idx = (long) list_value (list_next (list_next (list_first (l))));
  
  flavor = e->flavor;
  type = e->type;
  
  DiffMat *diff = Technology::T->diff[type][flavor];
  FetMat *fet = Technology::T->fet[type][flavor];
  PolyMat *poly = Technology::T->poly;

  /* ok, now we can draw! */
  Assert (fet && diff && poly, "What?");

  /* lets draw rectangles */
  listitem_t *li;

  prev = NULL;
  previdx = 0;
  li = list_first (l);
  while (li && list_next (li) && list_next (list_next (li))) {
    unsigned int flags = 0;
    node_t *m;

    n = (node_t *) list_value (li);
    e = (edge_t *) list_value (list_next (li));
    idx = (long) list_value (list_next (list_next (li)));
    m = (node_t *) list_value (list_next (list_next (list_next (li))));

    if (li == list_first (l)) {
      flags |= EDGE_FLAGS_LEFT;
    }
    if (!list_next (list_next (list_next (li)))) {
      flags |= EDGE_FLAGS_RIGHT;
    }

    xpos = emit_rectangle (L, 0, xpos, ypos, flags, prev, previdx, 
			   n, e, idx, 1, &b);
    prev = e;
    previdx = idx;

    li = list_next (list_next (list_next (li)));
  }
  Assert (li && !list_next (li), "Eh?");
  n = (node_t *) list_value (li);
  return b;
}




void ActStackLayoutPass::_createlocallayout (Process *p)
{
  list_t *stks;
  BBox b;

  Assert (stk, "What?");

  stks = stk->getStacks (p);
  if (!stks || list_length (stks) == 0) {
    (*layoutmap)[p] = NULL;
    return;
  }

  listitem_t *li;

  li = list_first (stks);
  list_t *stklist = (list_t *) list_value (li);

  _min_width = min_width;
  _fold_p_width = fold_p_width;
  _fold_n_width = fold_n_width;

  LayoutBlob *BLOB = new LayoutBlob (BLOB_HORIZ);

  if (list_length (stklist) > 0) {
    /* dual stacks */
    listitem_t *si;

    for (si = list_first (stklist); si; si = list_next (si)) {
      struct gate_pairs *gp;
      Layout *l = new Layout(stk->getNL (p));
      gp = (struct gate_pairs *) list_value (si);

      /*--- process gp ---*/
      b = print_dualstack (l, gp);

      l->DrawDiffBBox (b.flavor, EDGE_PFET,
		       b.p.llx, b.p.lly, b.p.urx-b.p.llx, b.p.ury-b.p.lly);
      l->DrawDiffBBox (b.flavor, EDGE_NFET,
		       b.n.llx, b.n.lly, b.n.urx-b.n.llx, b.n.ury-b.n.lly);

      BLOB->appendBlob (new LayoutBlob (BLOB_BASE, l));
    }
  }

  li = list_next (li);
  stklist = (list_t *) list_value (li);

  if (stklist && (list_length (stklist) > 0)) {
    /* n stacks */
    listitem_t *si;

    for (si = list_first (stklist); si; si = list_next (si)) {
      list_t *sl = (list_t *) list_value (si);
      Layout *l = new Layout (stk->getNL (p));

      b = print_singlestack (l, sl);
      
      l->DrawDiffBBox (b.flavor, EDGE_NFET, b.n.llx, b.n.lly,
		       b.n.urx - b.n.llx, b.n.ury - b.n.lly);

      BLOB->appendBlob (new LayoutBlob (BLOB_BASE, l)); 
    }
  }

  li = list_next (li);
  stklist = (list_t *) list_value (li);
  if (stklist && (list_length (stklist) > 0)) {
    /* p stacks */
    listitem_t *si;

    for (si = list_first (stklist); si; si = list_next (si)) {
      list_t *sl = (list_t *) list_value (si);
      Layout *l = new Layout (stk->getNL (p));

      b = print_singlestack (l, sl);
      
      l->DrawDiffBBox (b.flavor, EDGE_PFET, b.n.llx, b.n.lly,
		       b.n.urx - b.n.llx, b.n.ury - b.n.lly);

      BLOB->appendBlob (new LayoutBlob (BLOB_BASE, l)); 
    }
  }

  /* --- add pins --- */
  netlist_t *n = stk->getNL (p);

  long bllx, blly, burx, bury;
  BLOB->getBBox (&bllx, &blly, &burx, &bury);

  if (n && (bllx <= burx && blly <= bury)) {
    /* we have a netlist + layout */
    int p_in = 0;
    int p_out = 0;
    int s_in = 1;
    int s_out = 1;

    RoutingMat *m1 = Technology::T->metal[0];
    RoutingMat *m2 = Technology::T->metal[1];

    int redge = (burx - bllx + 1);
    int tedge = (bury - blly + 1);

    redge = snap_to (redge, m2->getPitch());
    tedge = snap_to (tedge, m1->getPitch());
    

    for (int i=0; i < A_LEN (n->bN->ports); i++) {
      if (n->bN->ports[i].omit) continue;
      if (n->bN->ports[i].input) {
	p_in++;
      }
      else {
	p_out++;
      }
    }

    if ((p_in * m2->getPitch() > redge) ||(p_out * m2->getPitch() > redge)) {
      warning ("Can't fit ports!");
    }
    
    if (p_in > 0) {
      while ((m2->getPitch() + p_in * s_in * m2->getPitch()) <= redge) {
	s_in++;
      }
      s_in--;
    }

    if (p_out > 0) {
      while ((m2->getPitch() + p_out * s_out * m2->getPitch()) <= redge) {
	s_out++;
      }
      s_out--;
    }

#if 0    
    if (s_in < 2 || s_out < 2) {
      warning ("Tight ports!");
      fprintf (stderr, "Process: ");
      a->mfprintfproc (stderr, p);
      fprintf (stderr, "\n");
    }
#endif    

    p_in = m2->getPitch();
    p_out = m2->getPitch();

    char tmp[1024];

    Layout *pins = new Layout(n);
    
    for (int i=0; i < A_LEN (n->bN->ports); i++) {
      if (n->bN->ports[i].omit) continue;

      ihash_bucket_t *b;
      b = ihash_lookup (n->bN->cH, (long)n->bN->ports[i].c);
      Assert (b, "Hmm:");
      act_booleanized_var_t *v;
      struct act_nl_varinfo *av;
      v = (act_booleanized_var_t *) b->v;
      av = (struct act_nl_varinfo *)v->extra;
      Assert (av, "Problem..");

      if (n->bN->ports[i].input) {
	int w = m2->minWidth ();
	pins->DrawMetalPin (1, bllx + p_in, blly + tedge - w, w, w, av->n);
	p_in += m2->getPitch()*s_in;
      }
      else {
	int w = m2->minWidth ();
	pins->DrawMetalPin (1, bllx + p_out, blly + m1->getPitch(), w, w, av->n);
	p_out += m2->getPitch()*s_out;
      }
    }
    LayoutBlob *bl = new LayoutBlob (BLOB_MERGE);
    bl->appendBlob (BLOB);
    bl->appendBlob (new LayoutBlob (BLOB_BASE, pins));
    BLOB = bl;
  }
  
  BLOB->PrintRect (stdout);
  printf ("---\n");

  (*layoutmap)[p] = BLOB;
  
}


void ActStackLayoutPass::_createlayout (Process *p)
{
  if (layoutmap->find (p) != layoutmap->end()) {
    return;
  }

  ActInstiter i(p->CurScope ());
  for (i = i.begin(); i != i.end(); i++) {
    ValueIdx *vx = (*i);
    if (TypeFactory::isProcessType (vx->t)) {
      Process *x = dynamic_cast<Process *> (vx->t->BaseType());
      if (x->isExpanded()) {
	_createlayout (x);
      }
    }
  }
  _createlocallayout (p);
}


int ActStackLayoutPass::run (Process *p)
{
  init ();

  if (!rundeps (p)) {
    return 0;
  }

  if (!p) {
    ActNamespace *g = ActNamespace::Global();
    ActInstiter i(g->CurScope());

    for (i = i.begin(); i != i.end(); i++) {
      ValueIdx *vx = (*i);
      if (TypeFactory::isProcessType (vx->t)) {
	Process *x = dynamic_cast<Process *>(vx->t->BaseType());
	if (x->isExpanded()) {
	  _createlayout (x);
	}
      }
    }
  }
  else {
    _createlayout (p);
  }

  _finished = 2;
  return 1;
}

int ActStackLayoutPass::init ()
{
  cleanup();

  layoutmap = new std::map<Process *, LayoutBlob *>();

  min_width = config_get_int ("net.min_width");
  fold_n_width = config_get_int ("net.fold_nfet_width");
  fold_p_width = config_get_int ("net.fold_pfet_width");

  _finished = 1;
  return 1;
}


int ActStackLayoutPass::emitLEF (FILE *fp, Process *p)
{
  int padx = 0;
  int pady = 0;
  netlist_t *n;
  if (!completed ()) {
    return 0;
  }

  if (!p) {
    return 0;
  }

  LayoutBlob *blob = (*layoutmap)[p];
  if (!blob) {
    return 0;
  }

  n = stk->getNL (p);
  if (!n) {
    return 0;
  }

  long bllx, blly, burx, bury;
  blob->getBBox (&bllx, &blly, &burx, &bury);

  if (bllx > burx || blly > bury) {
    /* no layout */
    return 0;
  }

  fprintf (fp, "MACRO ");
  a->mfprintfproc (fp, p);
  fprintf (fp, "\n");

  fprintf (fp, "    CLASS CORE ;\n");
  fprintf (fp, "    FOREIGN ");
  a->mfprintfproc (fp, p);
  fprintf (fp, "%.6f %.6f ;\n", 0.0, 0.0);
  fprintf (fp, "    ORIGIN %.6f %.6f ;\n", 0.0, 0.0);

  int redge = (burx - bllx + 1 + 10);
  int tedge = (bury - blly + 1 + 10);

  Assert (Technology::T->nmetals >= 3, "What?");
  
  RoutingMat *m1 = Technology::T->metal[0];
  RoutingMat *m2 = Technology::T->metal[1];
  RoutingMat *m3 = Technology::T->metal[2];


  /* add space on all sides if there aren't many metal layers */
  if (Technology::T->nmetals < 5) {
    padx = 2*m2->getPitch();
    pady = 2*m3->getPitch();
    pady = snap_to (pady, m1->getPitch());
  }

  redge = snap_to (redge, m2->getPitch());
  tedge = snap_to (tedge, m1->getPitch());

  double scale = Technology::T->scale/1000.0;

  fprintf (fp, "    SIZE %.6f BY %.6f ;\n", (redge + 2*padx)*scale,
	   (tedge + 2*pady)*scale);
  fprintf (fp, "    SYMMETRY X Y ;\n");
  fprintf (fp, "    SITE CoreSite ;\n");

  for (int i=0; i < A_LEN (n->bN->ports); i++) {
    if (n->bN->ports[i].omit) continue;

    char tmp[1024];
    ActId *id = n->bN->ports[i].c->toid();
    fprintf (fp, "    PIN ");
    id->sPrint (tmp, 1024);
    a->mfprintf (fp, "%s\n", tmp);
    delete id;

    fprintf (fp, "        DIRECTION %s ;\n",
	     n->bN->ports[i].input ? "INPUT" : "OUTPUT");
    fprintf (fp, "        USE ");

    ihash_bucket_t *b;
    const char *sigtype;
    sigtype = "SIGNAL";
    b = ihash_lookup (n->bN->cH, (long)n->bN->ports[i].c);
    Assert (b, "What on earth");
    act_booleanized_var_t *v;
    struct act_nl_varinfo *av;
    v = (act_booleanized_var_t *) b->v;
    av = (struct act_nl_varinfo *)v->extra;
    Assert (av, "Huh");
    if (av->n == n->Vdd) {
      sigtype = "POWER";
    }
    else if (av->n == n->GND) {
      sigtype = "GROUND";
    }
    fprintf (fp, "%s ;\n", sigtype);

    fprintf (fp, "        PORT\n");
    fprintf (fp, "        LAYER %s ;\n", m2->getName());
    /* -- find this rectangle, and print it out! -- */
    /* ZZZ XXX HERE */
    
    fprintf (fp, "        END\n");
    fprintf (fp, "    END ");
    a->mfprintf (fp, "%s", tmp);
    fprintf (fp, "\n");
  }

  /* XXX: add obstructions for metal layers */
  if (tedge > 6*m1->getPitch()) {
    fprintf (fp, "    OBS\n");
    fprintf (fp, "      LAYER %s ;\n", m1->getName());
    fprintf (fp, "         RECT %.6f %.6f %.6f %.6f ;\n",
	     scale*(padx + m2->getPitch()), scale*(pady + 3*m1->getPitch()),
	     scale*(padx + redge - m2->getPitch()),
	     scale*(pady + tedge - 3*m1->getPitch()));
    fprintf (fp, "    END\n");
  }

  return 1;
}

#ifdef INTEGRATED_PLACER
int ActStackLayoutPass::createBlocks (circuit_t *ckt, Process *p)
{
  int padx = 0;
  int pady = 0;
  netlist_t *n;
  if (!completed ()) {
    return 0;
  }

  if (!p) {
    return 0;
  }

  LayoutBlob *blob = (*layoutmap)[p];
  if (!blob) {
    return 0;
  }

  n = stk->getNL (p);
  if (!n) {
    return 0;
  }

  char tmp[1024];
  a->msnprintfproc (tmp, 1024, p);
  
  long bllx, blly, burx, bury;
  blob->getBBox (&bllx, &blly, &burx, &bury);

  int redge = (burx - bllx + 1 + 10); // XXX: 2*well_pad
  int tedge = (bury - blly + 1 + 10);

  RoutingMat *m1 = Technology::T->metal[0];
  RoutingMat *m2 = Technology::T->metal[1];
  RoutingMat *m3 = Technology::T->metal[2];


  /* add space on all sides if there aren't many metal layers */
  if (Technology::T->nmetals < 5) {
    padx = 2*m2->getPitch();
    pady = 2*m3->getPitch();
    pady = snap_to (pady, m1->getPitch());
  }

  redge = snap_to (redge, m2->getPitch());
  tedge = snap_to (tedge, m1->getPitch());

  std::string cktstr(tmp);
  ckt->add_block_type (cktstr, (redge + 2*padx)/m2->getPitch(),
		       (tedge + 2*pady)/m1->getPitch());
  
  /* pins */
  int p_in = 0;
  int p_out = 0;

  for (int i=0; i < A_LEN (n->bN->ports); i++) {
    if (n->bN->ports[i].omit) continue;
    if (n->bN->ports[i].input) {
      p_in++;
    }
    else {
      p_out++;
    }
  }

  if ((p_in * m2->getPitch() > redge) ||(p_out * m2->getPitch() > redge)) {
    warning ("Can't fit ports!");
  }
  
  int s_in = 1;
  int s_out = 1;

  if (p_in > 0) {
    while ((m2->getPitch() + p_in * s_in * m2->getPitch()) <= redge) {
      s_in++;
    }
    s_in--;
  }

  if (p_out > 0) {
    while ((m2->getPitch() + p_out * s_out * m2->getPitch()) <= redge) {
      s_out++;
    }
    s_out--;
  }
  
  if (s_in < 2 || s_out < 2) {
    warning ("Tight ports!");
  }

  p_in = m2->getPitch() + padx;
  p_out = m2->getPitch() + padx;

  for (int i=0; i < A_LEN (n->bN->ports); i++) {
    if (n->bN->ports[i].omit) continue;

    ActId *id = n->bN->ports[i].c->toid();
    id->sPrint (tmp, 1024);
    char tmp2[1024];
    a->msnprintf (tmp2, 1024, "%s", tmp);
    delete id;

    std::string pinname(tmp2);

    if (n->bN->ports[i].input) {
      ckt->add_pin_to_block (cktstr, pinname, p_in/m2->getPitch(),
			     (tedge + pady)/m1->getPitch());
      p_in += m2->getPitch()*s_in;
    }
    else {
      ckt->add_pin_to_block (cktstr, pinname, p_out/m2->getPitch(),
			     (pady + 1)/m1->getPitch());
      p_out += m2->getPitch()*s_out;
    }
  }
  return 1;
}
#endif