/*---------------------------------------------------------------*/
/*--- begin                                          ir_cfg.c ---*/
/*---------------------------------------------------------------*/


/*
   This file is part of Valgrind, a dynamic binary instrumentation
   framework.

   Copyright (C) 2026-2026  Ivo Raisr   (ivosh@ivosh.net)

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 3 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.

   The GNU General Public License is contained in the file COPYING.
*/

#include "libvex_basictypes.h"
#include "libvex_ir.h"
#include "libvex.h"
#include "main_util.h"
#include "ir_cfg.h"

/* ------------------------------------------------------------------ */
/* Constructors                                                       */
/* ------------------------------------------------------------------ */

IRCFG *emptyIRCFG(IRTypeEnv *tyenv, IRExpr *final_next, IRJumpKind final_jumpkind, Int offsIP) {
    IRCFG *cfg = LibVEX_Alloc_inline(sizeof(IRCFG));
    cfg->tyenv = tyenv;
    cfg->blocks_size = 8;
    cfg->n_blocks = 0;
    cfg->blocks = LibVEX_Alloc_inline(8 * sizeof(IRBlock *));
    cfg->final_next = final_next;
    cfg->final_jumpkind = final_jumpkind;
    cfg->offsIP = offsIP;
    cfg->has_back_edge = False;
    return cfg;
}

IRBlock *newIRBlock(IRCFG *cfg) {
    IRBlock *b = LibVEX_Alloc_inline(sizeof(IRBlock));
    b->phis = LibVEX_Alloc_inline(4 * sizeof(IRPhiNode *));
    b->n_phis = 0;
    b->phis_size = 4;
    b->stmts = LibVEX_Alloc_inline(16 * sizeof(IRStmt *));
    b->stmts_used = 0;
    b->stmts_size = 16;
    b->exit_stmt = NULL;
    b->n_succs = 0;
    b->succs[0] = b->succs[1] = -1;
    b->preds = LibVEX_Alloc_inline(4 * sizeof(Int));
    b->n_preds = 0;
    b->preds_size = 4;

    if (cfg->n_blocks == cfg->blocks_size) {
        IRBlock **old = cfg->blocks;
        Int newsz = cfg->blocks_size * 2;
        cfg->blocks = LibVEX_Alloc_inline(newsz * sizeof(IRBlock *));
        for (Int i = 0; i < cfg->n_blocks; i++) {
            cfg->blocks[i] = old[i];
        }
        cfg->blocks_size = newsz;
    }
    cfg->blocks[cfg->n_blocks++] = b;
    return b;
}

IRPhiNode *newIRPhiNode(IRTypeEnv *tyenv, IRType ty, Int guest_off, Int n_preds) {
    IRPhiNode *phi = LibVEX_Alloc_inline(sizeof(IRPhiNode));
    phi->dst = newIRTemp(tyenv, ty);
    phi->guest_off = guest_off;
    phi->n_preds = n_preds;
    phi->vals = LibVEX_Alloc_inline(n_preds * sizeof(IRTemp));
    for (Int i = 0; i < n_preds; i++) {
        phi->vals[i] = IRTemp_INVALID;
    }
    return phi;
}

void addPhiToBlock(IRBlock *b, IRPhiNode *phi) {
    if (b->n_phis == b->phis_size) {
        IRPhiNode **old = b->phis;
        Int newsz = b->phis_size * 2;
        b->phis = LibVEX_Alloc_inline(newsz * sizeof(IRPhiNode *));
        for (Int i = 0; i < b->n_phis; i++) {
            b->phis[i] = old[i];
        }
        b->phis_size = newsz;
    }
    b->phis[b->n_phis++] = phi;
}

void addStmtToBlock(IRBlock *b, IRStmt *st) {
    if (b->stmts_used == b->stmts_size) {
        IRStmt **old = b->stmts;
        Int newsz = b->stmts_size * 2;
        b->stmts = LibVEX_Alloc_inline(newsz * sizeof(IRStmt *));
        for (Int i = 0; i < b->stmts_used; i++) {
            b->stmts[i] = old[i];
        }
        b->stmts_size = newsz;
    }
    b->stmts[b->stmts_used++] = st;
}

void addEdge(IRCFG *cfg, Int from, Int to) {
    vassert(from >= 0 && from < cfg->n_blocks);
    vassert(to >= 0 && to < cfg->n_blocks);

    IRBlock *bf = cfg->blocks[from];
    vassert(bf->n_succs < 2);
    bf->succs[bf->n_succs++] = to;

    IRBlock *bt = cfg->blocks[to];
    if (bt->n_preds == bt->preds_size) {
        Int *old = bt->preds;
        Int newsz = bt->preds_size * 2;
        bt->preds = LibVEX_Alloc_inline(newsz * sizeof(Int));
        for (Int i = 0; i < bt->n_preds; i++) {
            bt->preds[i] = old[i];
        }
        bt->preds_size = newsz;
    }
    bt->preds[bt->n_preds++] = from;
}

/* ------------------------------------------------------------------ */
/* Lift/Lowers                                                        */
/* ------------------------------------------------------------------ */

/*
 * Split a flat IRSB at every Ist_Exit.  Each Ist_Exit becomes a block terminal; the fall-through is the only
 * intra-CFG edge (the taken target of Ist_Exit leaves the IRCFG and is recorded in exit_stmt).
 * K exits → K+1 blocks; block i → block i+1 (fall-through only).
 */
IRCFG *irsb_to_ircfg(const IRSB *bb) {
    vassert(bb != NULL);
    IRCFG *cfg = emptyIRCFG(deepCopyIRTypeEnv(bb->tyenv),
                            deepCopyIRExpr(bb->next),
                            bb->jumpkind, bb->offsIP);
    IRBlock *curr = newIRBlock(cfg);

    for (Int i = 0; i < bb->stmts_used; i++) {
        IRStmt *st = bb->stmts[i];
        if (st->tag == Ist_Exit) {
            curr->exit_stmt = deepCopyIRStmt(st);
            Int curr_idx = cfg->n_blocks - 1;
            IRBlock *next = newIRBlock(cfg);
            Int next_idx = cfg->n_blocks - 1;
            addEdge(cfg, curr_idx, next_idx);
            curr = next;
        } else {
            addStmtToBlock(curr, deepCopyIRStmt(st));
        }
    }
    /* Last block: exit_stmt=NULL, n_succs=0 (set by newIRBlock defaults) */
    sanityCheckIRCFG(cfg, "irsb_to_ircfg");
    return cfg;
}

/*
 * Linearize IRCFG back to a flat IRSB.  Blocks emitted in index order.
 * Requires n_phis == 0 in every block (call lower_phi_nodes first).
 */
IRSB *ircfg_to_irsb(const IRCFG *cfg) {
    vassert(cfg != NULL);

    IRSB *bb = emptyIRSB();
    bb->tyenv = deepCopyIRTypeEnv(cfg->tyenv);
    bb->next = deepCopyIRExpr(cfg->final_next);
    bb->jumpkind = cfg->final_jumpkind;
    bb->offsIP = cfg->offsIP;

    for (Int b = 0; b < cfg->n_blocks; b++) {
        const IRBlock *blk = cfg->blocks[b];
        vassert(blk->n_phis == 0);

        for (Int i = 0; i < blk->stmts_used; i++) {
            addStmtToIRSB(bb, deepCopyIRStmt(blk->stmts[i]));
        }
        if (blk->exit_stmt) {
            addStmtToIRSB(bb, deepCopyIRStmt(blk->exit_stmt));
        }
    }
    return bb;
}

/* ------------------------------------------------------------------ */
/* Dominance                                                          */
/* ------------------------------------------------------------------ */

/* Iterative algorithm for computing immediate dominator.
   Returns idom[0..n-1]; idom[0]=0 (entry dominates itself).
   Requires blocks in reverse-postorder (irsb_to_ircfg guarantees this). */
static Int *computeIDom(const IRCFG *cfg) {
    Int n = cfg->n_blocks;
    Int *idom = LibVEX_Alloc_inline(n * sizeof(Int));
    for (Int i = 0; i < n; i++) idom[i] = -1;
    idom[0] = 0;

    Bool changed = True;
    while (changed) {
        changed = False;
        for (Int b = 1; b < n; b++) {
            const IRBlock *blk = cfg->blocks[b];
            Int new_idom = -1;
            for (Int pi = 0; pi < blk->n_preds; pi++) {
                Int p = blk->preds[pi];
                if (idom[p] == -1) {
                    continue;
                }
                if (new_idom == -1) {
                    new_idom = p;
                    continue;
                }
                Int a = p, bb2 = new_idom;
                while (a != bb2) {
                    while (a > bb2) a = idom[a];
                    while (bb2 > a) bb2 = idom[bb2];
                }
                new_idom = a;
            }
            if (new_idom != -1 && idom[b] != new_idom) {
                idom[b] = new_idom;
                changed = True;
            }
        }
    }
    return idom;
}

typedef struct {
    Int **sets;
    Int *counts;
    Int *caps;
    Int n;
} DFSets;

static void addDominanceFrontier(DFSets *df, Int b, Int j) {
    Bool _found = False;
    for (Int _k = 0; _k < df->counts[b]; _k++) {
        if (df->sets[b][_k] == j) {
            _found = True;
            break;
        }
    }
    if (!_found) {
        if (df->counts[b] == df->caps[b]) {
            Int *old = df->sets[b];
            Int new_size = df->caps[b] * 2;
            df->sets[b] = LibVEX_Alloc_inline(new_size * sizeof(Int));
            for (Int i = 0; i < df->counts[b]; i++) {
                df->sets[b][i] = old[i];
            }
            df->caps[b] = new_size;
        }
        df->sets[b][df->counts[b]++] = (j);
    }
}

static DFSets computeDominanceFrontier(const IRCFG *cfg, const Int *idom) {
    Int n = cfg->n_blocks;
    DFSets df;
    df.n = n;
    df.sets = LibVEX_Alloc_inline(n * sizeof(Int *));
    df.counts = LibVEX_Alloc_inline(n * sizeof(Int));
    df.caps = LibVEX_Alloc_inline(n * sizeof(Int));
    for (Int i = 0; i < n; i++) {
        df.caps[i] = 4;
        df.counts[i] = 0;
        df.sets[i] = LibVEX_Alloc_inline(df.caps[i] * sizeof(Int));
    }

    /* For each join block j, walk up the dominator tree from each of its predecessors towards
     * j's immediate dominator. */
    for (Int j = 0; j < n; j++) {
        if (cfg->blocks[j]->n_preds < 2) continue;
        for (Int pi = 0; pi < cfg->blocks[j]->n_preds; pi++) {
            Int runner = cfg->blocks[j]->preds[pi];
            while (runner != idom[j]) {
                addDominanceFrontier(&df, runner, j);
                runner = idom[runner];
            }
        }
    }
    return df;
}

static void placePhiNodes(IRCFG *cfg, const DFSets *df) {
    Int n = cfg->n_blocks;

    /* Collect unique (offset, type) pairs appearing in any PUT statement, independent of aliased registers. */
    Int max_pairs = 0;
    for (Int b = 0; b < n; b++) {
        const IRBlock *blk = cfg->blocks[b];
        for (Int i = 0; i < blk->stmts_used; i++) {
            if (blk->stmts[i]->tag == Ist_Put) max_pairs++;
        }
    }
    if (max_pairs == 0) return;

    Int *pair_off = LibVEX_Alloc_inline(max_pairs * sizeof(Int));
    IRType *pair_ty = LibVEX_Alloc_inline(max_pairs * sizeof(IRType));
    Int n_pairs = 0;
    for (Int b = 0; b < n; b++) {
        const IRBlock *blk = cfg->blocks[b];
        for (Int i = 0; i < blk->stmts_used; i++) {
            const IRStmt *st = blk->stmts[i];
            if (st->tag != Ist_Put) continue;

            Int off = st->Ist.Put.offset;
            IRType ty = typeOfIRExpr(cfg->tyenv, st->Ist.Put.data);
            Bool seen = False;
            for (Int k = 0; k < n_pairs; k++) {
                if (pair_off[k] == off && pair_ty[k] == ty) {
                    seen = True;
                    break;
                }
            }
            if (!seen) {
                pair_off[n_pairs] = off;
                pair_ty[n_pairs] = ty;
                n_pairs++;
            }
        }
    }

    /* For each definition site of (off, ty), place phis at its dominance frontier;
       propagate frontier blocks as new definition sites and repeat. */
    for (Int pi = 0; pi < n_pairs; pi++) {
        Int off = pair_off[pi];
        IRType ty = pair_ty[pi];

        Bool *in_worklist = LibVEX_Alloc_inline(n * sizeof(Bool));
        Bool *has_phi = LibVEX_Alloc_inline(n * sizeof(Bool));
        for (Int b = 0; b < n; b++) {
            in_worklist[b] = has_phi[b] = False;
        }

        Int *worklist = LibVEX_Alloc_inline(n * sizeof(Int));
        Int worklist_length = 0;
        for (Int b = 0; b < n; b++) {
            const IRBlock *blk = cfg->blocks[b];
            for (Int i = 0; i < blk->stmts_used; i++) {
                const IRStmt *st = blk->stmts[i];
                if (st->tag == Ist_Put &&
                    st->Ist.Put.offset == off &&
                    typeOfIRExpr(cfg->tyenv, st->Ist.Put.data) == ty) {
                    if (!in_worklist[b]) {
                        worklist[worklist_length++] = b;
                        in_worklist[b] = True;
                    }
                    break;
                }
            }
        }

        for (Int w_idx = 0; w_idx < worklist_length; w_idx++) {
            Int block = worklist[w_idx]; /* currently processed block */
            for (Int k = 0; k < df->counts[block]; k++) {
                Int d = df->sets[block][k]; /* join block in DF[block] - candidate phi site */
                if (!has_phi[d] && cfg->blocks[d]->n_preds >= 2) {
                    IRPhiNode *phi = newIRPhiNode(cfg->tyenv, ty, off, cfg->blocks[d]->n_preds);
                    addPhiToBlock(cfg->blocks[d], phi);
                    has_phi[d] = True;
                    if (!in_worklist[d]) {
                        worklist[worklist_length++] = d;
                        in_worklist[d] = True;
                    }
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Stack helpers                                                      */
/* ------------------------------------------------------------------ */

#define MAX_GUEST_REG_STACKS 512
#define MAX_STACK_DEPTH 256

/* Guest registers can be accessed at the same offset with different widths. The key is (offset, type) tuple. */
typedef struct {
    Int off;
    IRType ty;
    IRTemp vals[MAX_STACK_DEPTH];
    Int top;
} GuestRegStack;

/* Thread-safety note: g_stacks is a module-level mutable state, unlike the arena-allocated working state used by
 * the rest of VEX. This is safe because Valgrind serializes all LibVEX_Translate calls through its scheduler - only
 * one translation runs at a time. */
static GuestRegStack g_stacks[MAX_GUEST_REG_STACKS];
static Int g_n_stacks = 0;

static void guestRegStacksReset(void) {
    for (Int i = 0; i < g_n_stacks; i++) {
        g_stacks[i].off = 0;
        g_stacks[i].ty = Ity_INVALID;
        g_stacks[i].top = 0;
    }
    g_n_stacks = 0;
}

static IRTemp guestRegStackTop(Int off, IRType ty) {
    for (Int i = 0; i < g_n_stacks; i++) {
        if (g_stacks[i].off == off && g_stacks[i].ty == ty && g_stacks[i].top > 0) {
            return g_stacks[i].vals[g_stacks[i].top - 1];
        }
    }
    return IRTemp_INVALID;
}

static void guestRegStackPush(Int off, IRType ty, IRTemp t) {
    for (Int i = 0; i < g_n_stacks; i++) {
        if (g_stacks[i].off == off && g_stacks[i].ty == ty) {
            vassert(g_stacks[i].top < MAX_STACK_DEPTH);
            g_stacks[i].vals[g_stacks[i].top++] = t;
            return;
        }
    }
    vassert(g_n_stacks < MAX_GUEST_REG_STACKS);

    g_stacks[g_n_stacks].off = off;
    g_stacks[g_n_stacks].ty = ty;
    g_stacks[g_n_stacks].top = 0;
    g_stacks[g_n_stacks].vals[g_stacks[g_n_stacks].top++] = t;
    g_n_stacks++;
}

static void guestRegStackPop(Int off, IRType ty) {
    for (Int i = 0; i < g_n_stacks; i++)
        if (g_stacks[i].off == off && g_stacks[i].ty == ty) {
            vassert(g_stacks[i].top > 0);
            g_stacks[i].top--;
            return;
        }
    vpanic("guestRegStackPop: guest register (offset, type) not found");
}

/* ------------------------------------------------------------------ */
/* SSA                                                                */
/* ------------------------------------------------------------------ */

/* A depth-first walk of the dominator tree. Pushes definitions when entering a block, fills successor phi slots
 * while definitions are live, recurses into children, pops on exit. */
static void renameBlock(IRCFG *cfg, Int b, const Int *idom) {
#define MAX_RENAMES 16384
    Int *pushed_offs = LibVEX_Alloc_inline(MAX_RENAMES * sizeof(Int));
    IRType *pushed_tys = LibVEX_Alloc_inline(MAX_RENAMES * sizeof(IRType));
    Int n_pushed = 0;

    IRBlock *blk = cfg->blocks[b];

    /* Step 1: phi destinations - push onto stacks. */
    for (Int i = 0; i < blk->n_phis; i++) {
        IRPhiNode *phi = blk->phis[i];
        IRType phi_ty = typeOfIRTemp(cfg->tyenv, phi->dst);
        guestRegStackPush(phi->guest_off, phi_ty, phi->dst);
        pushed_offs[n_pushed] = phi->guest_off;
        pushed_tys[n_pushed] = phi_ty;
        n_pushed++;
    }

    /* Step 2: rename statements. */
    for (Int i = 0; i < blk->stmts_used; i++) {
        IRStmt *st = blk->stmts[i];
        if (st->tag == Ist_Put) {
            /* PUT: keep in place; update stacks so subsequent renaming reflects the new guest-state contents. */
            IRExpr *data = st->Ist.Put.data;
            Int put_off = st->Ist.Put.offset;
            IRType put_ty = typeOfIRExpr(cfg->tyenv, data);
            Int put_sz = sizeofIRType(put_ty);
            Int put_end = put_off + put_sz;
            IRTemp to_push = (data->tag == Iex_RdTmp) ? data->Iex.RdTmp.tmp : IRTemp_INVALID;
            guestRegStackPush(put_off, put_ty, to_push);
            pushed_offs[n_pushed] = put_off;
            pushed_tys[n_pushed] = put_ty;
            n_pushed++;
            /* Mark aliased registers as invalid. */
            Int n_iter = g_n_stacks;
            for (Int k = 0; k < n_iter; k++) {
                if (g_stacks[k].off == put_off && g_stacks[k].ty == put_ty) {
                    continue;
                }
                if (g_stacks[k].top == 0) {
                    continue;
                }
                /* Already invalidated by an earlier overlapping write in this block. */
                if (g_stacks[k].vals[g_stacks[k].top - 1] == IRTemp_INVALID) {
                    continue;
                }
                Int k_off = g_stacks[k].off;
                Int k_end = k_off + sizeofIRType(g_stacks[k].ty);
                if (k_off < put_end && put_off < k_end) {
                    guestRegStackPush(k_off, g_stacks[k].ty, IRTemp_INVALID);
                    pushed_offs[n_pushed] = k_off;
                    pushed_tys[n_pushed] = g_stacks[k].ty;
                    n_pushed++;
                }
            }
        } else if (st->tag == Ist_PutI || st->tag == Ist_Dirty) {
            /* PutI/Dirty: can write any guest-state offset; invalidate every currently-live stack. */
            Int n_iter = g_n_stacks;
            for (Int k = 0; k < n_iter; k++) {
                if (g_stacks[k].top > 0 && g_stacks[k].vals[g_stacks[k].top - 1] != IRTemp_INVALID) {
                    Int off = g_stacks[k].off;
                    IRType ty = g_stacks[k].ty;
                    guestRegStackPush(off, ty, IRTemp_INVALID);
                    pushed_offs[n_pushed] = off;
                    pushed_tys[n_pushed] = ty;
                    n_pushed++;
                }
            }
        } else if (st->tag == Ist_WrTmp && st->Ist.WrTmp.data->tag == Iex_Get) {
            /* WrTmp(t, GET(off)): replace GET with RdTmp only when the (offset, GET.ty) stack has a live definition. */
            Int off = st->Ist.WrTmp.data->Iex.Get.offset;
            IRType ty = st->Ist.WrTmp.data->Iex.Get.ty;
            IRTemp top = guestRegStackTop(off, ty);
            if (top != IRTemp_INVALID) {
                st->Ist.WrTmp.data = IRExpr_RdTmp(top);
            }
        }
    }

    /* Step 3: fill phi operands in fall-through successors. */
    for (Int succ_i = 0; succ_i < blk->n_succs; succ_i++) {
        Int succ = blk->succs[succ_i];
        IRBlock *succ_blk = cfg->blocks[succ];
        Int pred_idx = -1;
        for (Int pred_i = 0; pred_i < succ_blk->n_preds; pred_i++) {
            if (succ_blk->preds[pred_i] == b) {
                pred_idx = pred_i;
                break;
            }
        }
        vassert(pred_idx >= 0);

        for (Int i = 0; i < succ_blk->n_phis; i++) {
            IRPhiNode *phi = succ_blk->phis[i];
            IRType phi_ty = typeOfIRTemp(cfg->tyenv, phi->dst);
            IRTemp top = guestRegStackTop(phi->guest_off, phi_ty);
            phi->vals[pred_idx] = (top != IRTemp_INVALID) ? top : IRTemp_INVALID;
        }
    }

    /* Step 4: recurse on dominator-tree children */
    for (Int c = 1; c < cfg->n_blocks; c++)
        if (idom[c] == b && c != b) {
            renameBlock(cfg, c, idom);
            /* child pops its own pushes */
        }

    /* Step 5: pop all pushes made in this block (in reverse order) */
    for (Int i = n_pushed - 1; i >= 0; i--) {
        guestRegStackPop(pushed_offs[i], pushed_tys[i]);
    }
}

void buildSSA(IRCFG *cfg) {
    Int *idom = computeIDom(cfg);
    DFSets df = computeDominanceFrontier(cfg, idom);
    placePhiNodes(cfg, &df);
    guestRegStacksReset();
    renameBlock(cfg, 0, idom);
    sanityCheckIRCFG(cfg, "buildSSA:after_rename");
}

/* Convert phi-nodes to parallel copies in predecessor blocks. */
void lowerPhiNodes(IRCFG *cfg) {
    for (Int b = 0; b < cfg->n_blocks; b++) {
        IRBlock *blk = cfg->blocks[b];
        if (blk->n_phis == 0) continue;
        for (Int pi = 0; pi < blk->n_preds; pi++) {
            IRBlock *pred = cfg->blocks[blk->preds[pi]];
            for (Int i = 0; i < blk->n_phis; i++) {
                const IRPhiNode *phi = blk->phis[i];
                IRTemp src = phi->vals[pi];
                if (src == IRTemp_INVALID) continue;
                addStmtToBlock(pred, IRStmt_WrTmp(phi->dst, IRExpr_RdTmp(src)));
            }
        }
        blk->n_phis = 0;
    }
}

/* ------------------------------------------------------------------ */
/* ppIRCFG                                                            */
/* ------------------------------------------------------------------ */

void ppIRCFG(const IRCFG *cfg) {
    vex_printf("IRCFG (%d blocks):\n", cfg->n_blocks);
    for (Int b = 0; b < cfg->n_blocks; b++) {
        const IRBlock *blk = cfg->blocks[b];
        vex_printf("  Block %d  preds=[", b);
        for (Int i = 0; i < blk->n_preds; i++) {
            vex_printf("%d%s", blk->preds[i], i + 1 < blk->n_preds ? "," : "");
        }
        vex_printf("]  succs=[");
        for (Int i = 0; i < blk->n_succs; i++) {
            vex_printf("%d%s", blk->succs[i], i + 1 < blk->n_succs ? "," : "");
        }
        vex_printf("]\n");

        for (Int i = 0; i < blk->n_phis; i++) {
            const IRPhiNode *phi = blk->phis[i];
            vex_printf("    t%u = phi(off=%d", phi->dst, phi->guest_off);
            for (Int j = 0; j < phi->n_preds; j++) {
                phi->vals[j] == IRTemp_INVALID
                    ? vex_printf(", pred%d:?", j)
                    : vex_printf(", pred%d:t%u", j, phi->vals[j]);
            }
            vex_printf(")\n");
        }

        for (Int i = 0; i < blk->stmts_used; i++) {
            vex_printf("    ");
            ppIRStmt(blk->stmts[i]);
            vex_printf("\n");
        }
        if (blk->exit_stmt) {
            vex_printf("    [exit] ");
            ppIRStmt(blk->exit_stmt);
            vex_printf("\n");
        } else {
            vex_printf("    [final] ");
            ppIRExpr(cfg->final_next);
            vex_printf("\n");
        }
    }
}

/* ------------------------------------------------------------------ */
/* sanityCheckIRCFG                                                   */
/* ------------------------------------------------------------------ */

void sanityCheckIRCFG(const IRCFG *cfg, const HChar *caller) {
    vassert(cfg && cfg->n_blocks > 0 && cfg->tyenv);
    vassert(cfg->final_next != NULL);

    /* Entry block has no predecessors in a linear CFG. */
    if (!cfg->has_back_edge) {
        vassert(cfg->blocks[0]->n_preds == 0);
    }

    for (Int b = 0; b < cfg->n_blocks; b++) {
        const IRBlock *blk = cfg->blocks[b];
        vassert(blk->n_succs >= 0 && blk->n_succs <= 2);

        /* A block with Ist_Exit has the taken target outside the IRCFG. */
        if (blk->exit_stmt) {
            vassert(blk->exit_stmt->tag == Ist_Exit);
            vassert(blk->n_succs >= 1);
        }

        /* Forward edge consistency: every succ lists b as a pred. */
        for (Int s = 0; s < blk->n_succs; s++) {
            Int c = blk->succs[s];
            vassert(c >= 0 && c < cfg->n_blocks);

            const IRBlock *cblk = cfg->blocks[c];
            Bool found = False;
            for (Int p = 0; p < cblk->n_preds; p++) {
                if (cblk->preds[p] == b) {
                    found = True;
                    break;
                }
            }
            if (!found) {
                vex_printf("sanityCheckIRCFG (%s): block %d→%d edge inconsistency\n", caller, b, c);
                vpanic("sanityCheckIRCFG: edge inconsistency");
            }
        }

        /* Reverse edge consistency: every pred lists b as a successor. */
        for (Int p = 0; p < blk->n_preds; p++) {
            Int q = blk->preds[p];
            vassert(q >= 0 && q < cfg->n_blocks);

            const IRBlock *qblk = cfg->blocks[q];
            Bool found = False;
            for (Int s = 0; s < qblk->n_succs; s++) {
                if (qblk->succs[s] == b) {
                    found = True;
                    break;
                }
            }
            if (!found) {
                vex_printf("sanityCheckIRCFG (%s): block %d<-%d pred inconsistency\n", caller, b, q);
                vpanic("sanityCheckIRCFG: pred inconsistency");
            }
        }

        for (Int i = 0; i < blk->n_phis; i++) {
            const IRPhiNode *phi = blk->phis[i];
            if (phi->n_preds != blk->n_preds) {
                vex_printf("sanityCheckIRCFG (%s): block %d phi n_preds mismatch\n", caller, b);
                vpanic("sanityCheckIRCFG: phi/block pred count mismatch");
            }
            /* Phi only meaningful at a join, and its destination must have a real type. */
            vassert(blk->n_preds >= 2);
            vassert(typeOfIRTemp(cfg->tyenv, phi->dst) != Ity_INVALID);
        }
    }
    /* The last block must have no successors in a linear CFG. */
    if (!cfg->has_back_edge) {
        vassert(cfg->blocks[cfg->n_blocks - 1]->n_succs == 0);
    }
}

/*---------------------------------------------------------------*/
/*--- end                                            ir_cfg.c ---*/
/*---------------------------------------------------------------*/
