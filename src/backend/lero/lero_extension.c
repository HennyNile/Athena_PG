#include "lero/lero_extension.h"
#include "optimizer/planner.h"

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
	
	switch (path->pathtype)
	{
		case T_SeqScan:
		case T_IndexScan:
		case T_IndexOnlyScan:
		case T_BitmapHeapScan:;
			num += 1;
            break;
		case T_HashJoin:
		case T_MergeJoin:
		case T_NestLoop:;
			join_path = (JoinPath *) path;
			add_join_input_tables(root, join_path->innerjoinpath, num);
			add_join_input_tables(root, join_path->outerjoinpath, num);
			break;
		case T_Material:;
			material_path = (MaterialPath *) path;
			add_join_input_tables(root, material_path->subpath, num);
			break;
		case T_Sort:;
			sort_path = (SortPath *) path;
			add_join_input_tables(root, sort_path->subpath, num);
			break;
		case T_Agg:;
			agg_path = (AggPath *) path;
			add_join_input_tables(root, agg_path->subpath, num);
			break;
		case T_Memoize:;
			memoize_path = (MemoizePath *) path;
			add_join_input_tables(root, memoize_path->subpath, num);
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
		case T_Gather:
		case T_IncrementalSort:
		case T_Group:
		case T_WindowAgg:
		case T_RecursiveUnion:
		case T_LockRows:
		case T_ModifyTable:
		case T_Limit:
		case T_GatherMerge:
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
		rows = rel->rows;
		original_card_list[join_card_num] = rows;

		num_tables = 0;
		add_join_input_tables(root, outer_rel->cheapest_total_path, &num_tables);
		add_join_input_tables(root, inner_rel->cheapest_total_path, &num_tables);
		join_input_table_nums[join_card_num] = num_tables;
		join_card_num += 1;
	}
	else
	{
		if (cur_card_idx <= join_card_num) {
			rows = lero_card_list[cur_card_idx];
		} else {
			rows = rel->rows;
		}
		cur_card_idx += 1;
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
			lero_card_list[i] = original_card_list[i] * lero_swing_factor;
		} else {
			lero_card_list[i] = original_card_list[i];
		}
	}

	plan = standard_planner(parse, queryString, cursorOptions, boundParams);
	return plan;
}