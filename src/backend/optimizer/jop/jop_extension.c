#include "optimizer/jop_extension.h"
#include "parser/parsetree.h"
#include "nodes/nodes.h"

void string_builder_init(string_builder* self) {
	self->data = (char*)palloc(10);
	self->len = 0;
	self->cap = 10;
	self->data[0] = '\0';
}

void string_builder_destroy(string_builder* self) {
	pfree(self->data);
}

void string_builder_append(string_builder* self, const char* append) {
	size_t append_len = strlen(append);
	size_t new_len = self->len + append_len;
	size_t new_cap = self->cap;
	while (new_cap < new_len + 1) {
		new_cap *= 2;
	}
	if (new_cap != self->cap) {
		char* new_data = (char*) palloc(new_cap);
		memcpy(new_data, self->data, self->len);
		pfree(self->data);
		self->data = new_data;
		self->cap = new_cap;
	}
	memcpy(self->data + self->len, append, append_len + 1);
	self->len = new_len;
}

char* catch_join_order(PlannerInfo *root, Path *path)
{
    string_builder strb;
	char* alias_name;
	char* inner;
	char* outer;
	char* return_str;
	Index seqscan_idx;
	Index indexscan_idx;
	Index idxonlyscan_idx;
	Index bit_heapscan_idx;
	
	string_builder_init(&strb);
	alias_name = "";
	inner = "";
	outer = "";
    switch (path->pathtype)
	{
		case T_SeqScan:
			seqscan_idx = path->parent->relid;
			alias_name = rt_fetch(seqscan_idx, root->parse->rtable)->eref->aliasname;
            string_builder_append(&strb, alias_name);
            break;
		case T_IndexScan:
			indexscan_idx = ((IndexPath *)path)->path.parent->relid;
            alias_name = rt_fetch(indexscan_idx, root->parse->rtable)->eref->aliasname;
            string_builder_append(&strb, alias_name);
			break;
		case T_IndexOnlyScan:
			idxonlyscan_idx = ((IndexPath *)path)->path.parent->relid;
            alias_name = rt_fetch(idxonlyscan_idx, root->parse->rtable)->eref->aliasname;
            string_builder_append(&strb, alias_name);
			break;
		case T_BitmapHeapScan:
			bit_heapscan_idx = ((BitmapHeapPath *)path)->path.parent->relid;
            alias_name = rt_fetch(bit_heapscan_idx, root->parse->rtable)->eref->aliasname;
            string_builder_append(&strb, alias_name);
			break;
		case T_HashJoin:
			inner = catch_join_order(root, ((HashPath* )path)->jpath.innerjoinpath);
			outer = catch_join_order(root, ((HashPath* )path)->jpath.outerjoinpath);
            string_builder_append(&strb, "(");
            string_builder_append(&strb, outer);
            string_builder_append(&strb, " ");
            string_builder_append(&strb, inner);
            string_builder_append(&strb, ")");
            pfree(inner);
            pfree(outer);
            break;
		case T_MergeJoin:
			inner = catch_join_order(root, ((MergePath* )path)->jpath.innerjoinpath);
			outer = catch_join_order(root, ((MergePath* )path)->jpath.outerjoinpath);
            string_builder_append(&strb, "(");
            string_builder_append(&strb, outer);
            string_builder_append(&strb, " ");
            string_builder_append(&strb, inner);
            string_builder_append(&strb, ")");
            pfree(inner);
            pfree(outer);
            break;
		case T_NestLoop:;
			inner = catch_join_order(root, ((NestPath* )path)->jpath.innerjoinpath);
			outer = catch_join_order(root, ((NestPath* )path)->jpath.outerjoinpath);
            string_builder_append(&strb, "(");
            string_builder_append(&strb, outer);
            string_builder_append(&strb, " ");
            string_builder_append(&strb, inner);
            string_builder_append(&strb, ")");
            pfree(inner);
            pfree(outer);            
			break;
		case T_Material:
			outer = catch_join_order(root, ((MaterialPath* )path)->subpath);
            string_builder_append(&strb, outer);
            pfree(outer);
			break;
		case T_Sort:
			outer = catch_join_order(root, ((SortPath* )path)->subpath);
            string_builder_append(&strb, outer);
            pfree(outer);
            break;
		case T_Agg:
			if (IsA(path, GroupingSetsPath))
			{
				outer = catch_join_order(root, ((GroupingSetsPath* )path)->subpath);
                string_builder_append(&strb, outer);
                pfree(outer);
			}
			else
			{
				Assert(IsA(path, AggPath));
				outer = catch_join_order(root, ((AggPath* )path)->subpath);
                string_builder_append(&strb, outer);
                pfree(outer);
			}
			break;
		case T_Gather:
			outer = catch_join_order(root, ((GatherPath *)path)->subpath);
            string_builder_append(&strb, outer);
            pfree(outer);
			break;
		case T_GatherMerge:
			outer = catch_join_order(root, ((GatherMergePath *)path)->subpath);
            string_builder_append(&strb, outer);
            pfree(outer);
			break;
		case T_Memoize:
			outer = catch_join_order(root, ((MemoizePath* )path)->subpath);
            string_builder_append(&strb, outer);
            pfree(outer);           
			break;
		case T_Result:
			if (IsA(path, ProjectionPath))
			{
				outer = catch_join_order(root, ((ProjectionPath* )path)->subpath);
				string_builder_append(&strb, outer);
				pfree(outer);
				break;
			}
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
		case T_ProjectSet:
		case T_Unique:
		case T_Group:
		case T_WindowAgg:
		case T_RecursiveUnion:
		case T_LockRows:
		case T_ModifyTable:
			elog(WARNING, "unrecognized node type: %d",
				 (int) path->type);
			break;
		default:
			elog(ERROR, "unrecognized node type: %d",
				 (int) path->type);
			break;
	}
    return_str = (char*) palloc(strb.len+1);
	memcpy(return_str, strb.data, strb.len);
	return_str[strb.len] = '\0';
	string_builder_destroy(&strb);

    return return_str;
}

void save_join_order_plans(PlannerInfo *root, List *pathlis)
{
    // ListCell *lc;
    // FILE *fp;
	// fp = fopen("/tmp/JOP_join_order_plans.txt", "w");
    // foreach(lc, pathlis)
    // {
    //     Path *path = (Path *) lfirst(lc);
    //     char *join_order = catch_join_order(root, path);
    //     fprintf(fp, "%s\n", join_order);
    //     pfree(join_order);
    // }
	// fclose(fp);
}