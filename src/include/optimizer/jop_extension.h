#include "postgres.h"
#include "fmgr.h"
#include "nodes/plannodes.h"
#include "nodes/pathnodes.h"

#ifndef JOP_EXTENSION_H
#define JOP_EXTENSION_H

extern char *catch_join_order(PlannerInfo *root, Path *path);

extern void save_join_order_plans(PlannerInfo *root, List *pathlis);

typedef struct string_builder{
	char* data;
	size_t len;
	size_t cap;
} string_builder;

extern void 
string_builder_init(string_builder* self);

extern void
string_builder_destroy(string_builder* self);

extern void 
string_builder_append(string_builder* self, const char* append);

#endif