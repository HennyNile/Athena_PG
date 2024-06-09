#include "lero/lero_extension.h"
#include "optimizer/planner.h"
#include "utils/lsyscache.h"

#define CARD_MAX_NUM 25000

bool enable_lero = false;
double lero_swing_factor = 0.1;
int lero_subquery_table_num = 2;

bool record_original_card_phase;
int join_card_num;
int cur_card_idx;

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

			// elog(WARNING, "estimate card %d: rows %f, num_table %d", join_card_num, rows, num_tables);
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

PlannedStmt *lero_pgsysml_hook_planner(Query *parse, const char *queryString,
									  int cursorOptions,
									  ParamListInfo boundParams)
{
	PlannedStmt *original_plan;
    PlannedStmt *plan;
	record_original_card_phase = true;
	join_card_num = 0;
	cur_card_idx = 0;
    original_plan = standard_planner(copyObject(parse), queryString, cursorOptions, boundParams);
	pfree(original_plan);
	record_original_card_phase = false;
	for (int i = 0; i < join_card_num; i++) {
		if (join_input_table_nums[i] == lero_subquery_table_num) {
			// elog(WARNING, "change card %d: from %f to %f", i, original_card_list[i], original_card_list[i] * lero_swing_factor);
			lero_card_list[i] = original_card_list[i] * lero_swing_factor;
		} else {
			lero_card_list[i] = original_card_list[i];
		}
	}

	plan = standard_planner(parse, queryString, cursorOptions, boundParams);
	return plan;
}