#include "lero/lero_extension.h"
#include "optimizer/cost.h"
#include "optimizer/planner.h"
#include "utils/lsyscache.h"

#define CARD_MAX_NUM 25000

bool enable_bao = false;
bool enable_lero = false;
double lero_swing_factor = 0.1;
int lero_subquery_table_num = 2;

bool record_original_card_phase;
int join_card_num;
int cur_card_idx;
int max_num_tables;

double original_card_list[CARD_MAX_NUM];
double lero_card_list[CARD_MAX_NUM];
int join_input_table_nums[CARD_MAX_NUM];


static void
add_join_input_tables(PlannerInfo *root, Path *path, int* num)
{
	JoinPath* join_path;
	MaterialPath* material_path;
	SortPath* sort_path;
	AggPath* agg_path;
	MemoizePath* memoize_path;
	GatherPath* gather_path;
	GatherMergePath* gather_merge_path;
	Index table_relid;
	char* table_name;

	switch (path->pathtype)
	{
		case T_SeqScan:
			table_relid = path->parent->relid;
			table_name = get_rel_name(root->simple_rte_array[table_relid]->relid);
			// elog(WARNING, "Seq Scan %s", table_name);
			*num += 1;
            break;
		case T_IndexScan:
			table_relid = path->parent->relid;
			table_name = get_rel_name(root->simple_rte_array[table_relid]->relid);
			// elog(WARNING, "Index Scan %s", table_name);
			*num += 1;
            break;
		case T_IndexOnlyScan:
			table_relid = path->parent->relid;
			table_name = get_rel_name(root->simple_rte_array[table_relid]->relid);
			// elog(WARNING, "Index Only Scan %s", table_name);
			*num += 1;
            break;
		case T_BitmapHeapScan:
			table_relid = path->parent->relid;
			table_name = get_rel_name(root->simple_rte_array[table_relid]->relid);
			// elog(WARNING, "Bitmap Heap Scan %s", table_name);
			*num += 1;
            break;
		case T_HashJoin:
			join_path = (JoinPath *) path;
			// elog(WARNING, "Hash Join (");
			add_join_input_tables(root, join_path->innerjoinpath, num);
			add_join_input_tables(root, join_path->outerjoinpath, num);
			// elog(WARNING, ")");
			break;
		case T_MergeJoin:
			join_path = (JoinPath *) path;
			// elog(WARNING, "Merge Join (");
			add_join_input_tables(root, join_path->innerjoinpath, num);
			add_join_input_tables(root, join_path->outerjoinpath, num);
			// elog(WARNING, ")");
			break;
		case T_NestLoop:
			join_path = (JoinPath *) path;
			// elog(WARNING, "NestLoop Join (");
			add_join_input_tables(root, join_path->innerjoinpath, num);
			add_join_input_tables(root, join_path->outerjoinpath, num);
			// elog(WARNING, ")");
			break;
		case T_Material:;
			// elog(WARNING, "Material (");
			material_path = (MaterialPath *) path;
			add_join_input_tables(root, material_path->subpath, num);
			// elog(WARNING, ")");
			break;
		case T_Sort:;
			// elog(WARNING, "Sort (");
			sort_path = (SortPath *) path;
			add_join_input_tables(root, sort_path->subpath, num);
			// elog(WARNING, ")");
			break;
		case T_Agg:;
			// elog(WARNING, "Agg (");
			agg_path = (AggPath *) path;
			add_join_input_tables(root, agg_path->subpath, num);
			// elog(WARNING, ")");
			break;
		case T_Memoize:;
			// elog(WARNING, "Memoize (");
			memoize_path = (MemoizePath *) path;
			add_join_input_tables(root, memoize_path->subpath, num);
			// elog(WARNING, ")");
			break;
		case T_Gather:
			// elog(WARNING, "Gather (");
			gather_path = (GatherPath *) path;
			add_join_input_tables(root, gather_path->subpath, num);
			// elog(WARNING, ")");
			break;
		case T_GatherMerge:
			// elog(WARNING, "Gather Merge (");
			gather_merge_path = (GatherMergePath *) path;
			add_join_input_tables(root, gather_merge_path->subpath, num);
			// elog(WARNING, ")");
			break;
		case T_SampleScan:
		case T_TidScan:
		case T_SubqueryScan:
		case T_FunctionScan:
		case T_TableFuncScan:
		case T_ValuesScan:
		case T_CteScan:
		case T_WorkTableScan:
		case T_NamedTuplestoreScan:
		case T_ForeignScan:
		case T_CustomScan:
		case T_Append:
		case T_MergeAppend:
		case T_Result:
		case T_ProjectSet:
		case T_Unique:
		case T_IncrementalSort:
		case T_Group:
		case T_WindowAgg:
		case T_RecursiveUnion:
		case T_LockRows:
		case T_ModifyTable:
		case T_Limit:
			elog(ERROR, "unrecognized node type: %d",
				 (int) path->pathtype);
			break;
		default:
			elog(ERROR, "unrecognized node type: %d",
				 (int) path->pathtype);
			break;
	}
}

void lero_pgsysml_set_joinrel_size_estimates(PlannerInfo *root, RelOptInfo *rel,
											RelOptInfo *outer_rel,
											RelOptInfo *inner_rel,
											SpecialJoinInfo *sjinfo,
											List *restrictlist)
{
	static double rows;
	int num_tables = 0;
	if (record_original_card_phase)
	{
		if (join_card_num < CARD_MAX_NUM) {
			rows = rel->rows;
			original_card_list[join_card_num] = rows;

			num_tables = 0;
			add_join_input_tables(root, outer_rel->cheapest_total_path, &num_tables);
			add_join_input_tables(root, inner_rel->cheapest_total_path, &num_tables);
			join_input_table_nums[join_card_num] = num_tables;
			if (num_tables > max_num_tables) {
				max_num_tables = num_tables;
			}

			// elog(WARNING, "estimate card %d: rows %f, num_tables %d", join_card_num, rows, num_tables);
			join_card_num += 1;
		}
	}
	else
	{
		if (cur_card_idx < join_card_num) {
			// elog(WARNING, "set card %d: %f", cur_card_idx, lero_card_list[cur_card_idx]);
			rows = lero_card_list[cur_card_idx];
			cur_card_idx += 1;
		} else {
			rows = rel->rows;
		}
	}
	rel->rows = rows;
}

static double swing_factors[4] = {0.1, 100., 10., 0.01};

PlannedStmt *lero_pgsysml_hook_planner(Query *parse, const char *queryString,
									  int cursorOptions,
									  ParamListInfo boundParams)
{
	PlannedStmt *original_plan;
    PlannedStmt *plan;
	record_original_card_phase = true;
	join_card_num = 0;
	cur_card_idx = 0;
	max_num_tables = 0;
    original_plan = standard_planner(copyObject(parse), queryString, cursorOptions, boundParams);
	record_original_card_phase = false;
	for (int current_subquery_table_num = max_num_tables; current_subquery_table_num >= 2; --current_subquery_table_num) {
		for (int current_swing_factor_idx = 0; current_swing_factor_idx < 4; ++current_swing_factor_idx) {
			for (int i = 0; i < join_card_num; i++) {
				if (join_input_table_nums[i] == current_subquery_table_num) {
					// elog(WARNING, "change card %d: from %f to %f", i, original_card_list[i], original_card_list[i] * lero_swing_factor);
					lero_card_list[i] = original_card_list[i] * swing_factors[current_swing_factor_idx];
				} else {
					lero_card_list[i] = original_card_list[i];
				}
			}

			plan = standard_planner(copyObject(parse), queryString, cursorOptions, boundParams);
			pfree(plan);
		}
	}
	return original_plan;
}

#define SET2(x1, x2)     \
do {                    \
	enable_##x1 = true; \
	enable_##x2 = true; \
} while(0)

#define SET3(x1, x2, x3) \
do {                     \
	enable_##x1 = true;  \
	enable_##x2 = true;  \
	enable_##x3 = true;  \
} while(0)

#define SET4(x1, x2, x3, x4) \
do {                         \
	enable_##x1 = true;      \
	enable_##x2 = true;      \
	enable_##x3 = true;      \
	enable_##x4 = true;      \
} while(0)

#define SET5(x1, x2, x3, x4, x5) \
do {                             \
	enable_##x1 = true;          \
	enable_##x2 = true;          \
	enable_##x3 = true;          \
	enable_##x4 = true;          \
	enable_##x5 = true;          \
} while(0)

PlannedStmt *bao_pgsysml_hook_planner(Query *parse, const char *queryString,
									  int cursorOptions,
									  ParamListInfo boundParams)
{
	PlannedStmt *original_plan;
    PlannedStmt *plan;
	bool hj, mj,nl, is, ss, io;
	int arm;
    original_plan = standard_planner(copyObject(parse), queryString, cursorOptions, boundParams);
	hj = enable_hashjoin;
	mj = enable_mergejoin;
	nl = enable_nestloop;
	is = enable_indexscan;
	ss = enable_seqscan;
	io = enable_indexonlyscan;
	for (arm = 0; arm < 48; ++arm) {
		enable_hashjoin = false;
		enable_mergejoin = false;
		enable_nestloop = false;
		enable_indexscan = false;
		enable_seqscan = false;
		enable_indexonlyscan = false;
		switch (arm) {
		case 0:
			SET2(hashjoin, indexonlyscan);
			break;
		case 1:
			SET3(hashjoin, indexonlyscan, indexscan);
			break;
		case 2:
			SET4(hashjoin, indexonlyscan, indexscan, mergejoin);
			break;
		case 3:
			SET5(hashjoin, indexonlyscan, indexscan, mergejoin, nestloop);
			break;
		case 4:
			SET5(hashjoin, indexonlyscan, indexscan, mergejoin, seqscan);
			break;
		case 5:
			SET4(hashjoin, indexonlyscan, indexscan, nestloop);
			break;
		case 6:
			SET5(hashjoin, indexonlyscan, indexscan, nestloop, seqscan);
			break;
		case 7:
			SET4(hashjoin, indexonlyscan, indexscan, seqscan);
			break;
		case 8:
			SET3(hashjoin, indexonlyscan, mergejoin);
			break;
		case 9:
			SET4(hashjoin, indexonlyscan, mergejoin, nestloop);
			break;
		case 10:
			SET5(hashjoin, indexonlyscan, mergejoin, nestloop, seqscan);
			break;
		case 11:
			SET4(hashjoin, indexonlyscan, mergejoin, seqscan);
			break;
		case 12:
			SET3(hashjoin, indexonlyscan, nestloop);
			break;
		case 13:
			SET4(hashjoin, indexonlyscan, nestloop, seqscan);
			break;
		case 14:
			SET3(hashjoin, indexonlyscan, seqscan);
			break;
		case 15:
			SET2(hashjoin, indexscan);
			break;
		case 16:
			SET3(hashjoin, indexscan, mergejoin);
			break;
		case 17:
			SET4(hashjoin, indexscan, mergejoin, nestloop);
			break;
		case 18:
			SET5(hashjoin, indexscan, mergejoin, nestloop, seqscan);
			break;
		case 19:
			SET4(hashjoin, indexscan, mergejoin, seqscan);
			break;
		case 20:
			SET3(hashjoin, indexscan, nestloop);
			break;
		case 21:
			SET4(hashjoin, indexscan, nestloop, seqscan);
			break;
		case 22:
			SET3(hashjoin, indexscan, seqscan);
			break;
		case 23:
			SET4(hashjoin, mergejoin, nestloop, seqscan);
			break;
		case 24:
			SET3(hashjoin, mergejoin, seqscan);
			break;
		case 25:
			SET3(hashjoin, nestloop, seqscan);
			break;
		case 26:
			SET2(hashjoin, seqscan);
			break;
		case 27:
			SET3(indexonlyscan, indexscan, mergejoin);
			break;
		case 28:
			SET4(indexonlyscan, indexscan, mergejoin, nestloop);
			break;
		case 29:
			SET5(indexonlyscan, indexscan, mergejoin, nestloop, seqscan);
			break;
		case 30:
			SET4(indexonlyscan, indexscan, mergejoin, seqscan);
			break;
		case 31:
			SET3(indexonlyscan, indexscan, nestloop);
			break;
		case 32:
			SET4(indexonlyscan, indexscan, nestloop, seqscan);
			break;
		case 33:
			SET2(indexonlyscan, mergejoin);
			break;
		case 34:
			SET3(indexonlyscan, mergejoin, nestloop);
			break;
		case 35:
			SET4(indexonlyscan, mergejoin, nestloop, seqscan);
			break;
		case 36:
			SET3(indexonlyscan, mergejoin, seqscan);
			break;
		case 37:
			SET2(indexonlyscan, nestloop);
			break;
		case 38:
			SET3(indexonlyscan, nestloop, seqscan);
			break;
		case 39:
			SET2(indexscan, mergejoin);
			break;
		case 40:
			SET3(indexscan, mergejoin, nestloop);
			break;
		case 41:
			SET4(indexscan, mergejoin, nestloop, seqscan);
			break;
		case 42:
			SET3(indexscan, mergejoin, seqscan);
			break;
		case 43:
			SET2(indexscan, nestloop);
			break;
		case 44:
			SET3(indexscan, nestloop, seqscan);
			break;
		case 45:
			SET3(mergejoin, nestloop, seqscan);
			break;
		case 46:
			SET2(mergejoin, seqscan);
			break;
		case 47:
			SET2(nestloop, seqscan);
			break;
		}

		plan = standard_planner(copyObject(parse), queryString, cursorOptions, boundParams);
		pfree(plan);
	}
	enable_hashjoin = hj;
	enable_mergejoin = mj;
	enable_nestloop = nl;
	enable_seqscan = ss;
	enable_indexscan = is;
	enable_indexonlyscan = io;
	return original_plan;
}