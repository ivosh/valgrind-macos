/*---------------------------------------------------------------*/
/*--- begin                                          ir_cfg.h ---*/
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

/* Internal call-flow-graph (CFG) phi-node representation.
   Never exposed through pub/libvex_ir.h. Tools never see these types. */
#ifndef __VEX_IR_CFG_H
#define __VEX_IR_CFG_H

#include "libvex_basictypes.h"
#include "libvex_ir.h"
#include "libvex.h"

/* ------------------------------------------------------------------ */
/* Core types                                                         */
/* ------------------------------------------------------------------ */

typedef struct {
    IRTemp dst;
    Int guest_off;
    Int n_preds;
    IRTemp *vals; /* vals[i] = temp from predecessor i */
} IRPhiNode;

typedef struct {
    IRPhiNode **phis;
    Int n_phis;
    Int phis_size;
    IRStmt **stmts;
    Int stmts_used;
    Int stmts_size;
    IRStmt *exit_stmt; /* Ist_Exit, or NULL for the final block */
    Int succs[2];
    Int n_succs;
    Int *preds;
    Int n_preds;
    Int preds_size;
} IRBlock;

typedef struct {
    IRTypeEnv *tyenv;
    IRBlock **blocks;
    Int n_blocks;
    Int blocks_size;
    IRExpr *final_next;
    IRJumpKind final_jumpkind;
    Int offsIP;
    Bool has_back_edge; /* True after cfg_loop_unroll adds a back-edge */
} IRCFG;

/* ---- Constructors ---- */
extern IRCFG *emptyIRCFG(IRTypeEnv *tyenv, IRExpr *final_next, IRJumpKind final_jumpkind, Int offsIP);

extern IRBlock *newIRBlock(IRCFG *cfg);

extern IRPhiNode *newIRPhiNode(IRTypeEnv *tyenv, IRType ty, Int guest_off, Int n_preds);

extern void addPhiToBlock(IRBlock *b, IRPhiNode *phi);

extern void addStmtToBlock(IRBlock *b, IRStmt *st);

extern void addEdge(IRCFG *cfg, Int from, Int to);

/* ---- Lift / Lower ---- */
extern IRCFG *irsb_to_ircfg(const IRSB *bb);

extern IRSB *ircfg_to_irsb(const IRCFG *cfg);

/* ---- Debug / Sanity ---- */
extern void ppIRCFG(const IRCFG *cfg);

extern void sanityCheckIRCFG(const IRCFG *cfg, const HChar *caller);

#endif /* __VEX_IR_CFG_H */

/*---------------------------------------------------------------*/
/*--- end                                            ir_cfg.h ---*/
/*---------------------------------------------------------------*/
