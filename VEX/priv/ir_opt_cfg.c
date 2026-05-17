/*---------------------------------------------------------------*/
/*--- begin                                      ir_opt_cfg.c ---*/
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

#include "ir_cfg.h"

/* ------------------------------------------------------------------ */
/* Call-flow-graph (CFG) optimization pipeline                        */
/* ------------------------------------------------------------------ */

IRSB *run_cfg_pipeline(IRSB *bb,
                       IRExpr * (*specHelper)(const HChar *function_name, IRExpr **args, IRStmt **precedingStmts,
                                              Int n_precedingStmts),
                       Bool( *preciseMemExnsFn)(Int minoff, Int maxoff, VexRegisterUpdates pxControl),
                       VexRegisterUpdates pxControl,
                       Addr guest_addr,
                       VexArch guest_arch) {
    IRCFG *cfg = irsb_to_ircfg(bb);
    sanityCheckIRCFG(cfg, "run_cfg_pipeline:after_lift");
    return ircfg_to_irsb(cfg);
}

/*---------------------------------------------------------------*/
/*--- end                                        ir_opt_cfg.c ---*/
/*---------------------------------------------------------------*/
