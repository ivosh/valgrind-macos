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

    for (Int b = 0; b < cfg->n_blocks; b++) {
        const IRBlock *blk = cfg->blocks[b];
        vassert(blk->n_succs <= 2);

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
        for (Int i = 0; i < blk->n_phis; i++) {
            const IRPhiNode *phi = blk->phis[i];
            if (phi->n_preds != blk->n_preds) {
                vex_printf("sanityCheckIRCFG (%s): block %d phi n_preds mismatch\n", caller, b);
                vpanic("sanityCheckIRCFG: phi/block pred count mismatch");
            }
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
