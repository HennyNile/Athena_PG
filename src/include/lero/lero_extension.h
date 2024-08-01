#pragma once

#include "postgres.h"
#include "fmgr.h"
#include "optimizer/paths.h"
#include "nodes/plannodes.h"
#include "nodes/pg_list.h"
#include "utils/guc.h"

extern bool enable_bao;
extern bool enable_lero;
extern double lero_swing_factor;
extern int lero_subquery_table_num;

extern void lero_pgsysml_set_joinrel_size_estimates(PlannerInfo *root, RelOptInfo *rel,
						   RelOptInfo *outer_rel,
						   RelOptInfo *inner_rel,
						   SpecialJoinInfo *sjinfo,
						   List *restrictlist);

extern PlannedStmt* lero_pgsysml_hook_planner(Query *parse, const char *queryString,
                                int cursorOptions,
                                ParamListInfo boundParams);

extern PlannedStmt* bao_pgsysml_hook_planner(Query *parse, const char *queryString,
                                int cursorOptions,
                                ParamListInfo boundParams);