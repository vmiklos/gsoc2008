#include "builtin.h"
#include "cache.h"
#include "string-list.h"
#include "rerere.h"
#include "xdiff/xdiff.h"
#include "xdiff-interface.h"

static const char git_rerere_usage[] =
"git rerere [clear | status | diff | gc]";

/* these values are days */
static int cutoff_noresolve = 15;
static int cutoff_resolve = 60;

static const char *rr_path(const char *name, const char *file)
{
	return git_path("rr-cache/%s/%s", name, file);
}

static time_t rerere_created_at(const char *name)
{
	struct stat st;
	return stat(rr_path(name, "preimage"), &st) ? (time_t) 0 : st.st_mtime;
}

static int has_resolution(const char *name)
{
	struct stat st;
	return !stat(rr_path(name, "postimage"), &st);
}

static void unlink_rr_item(const char *name)
{
	unlink(rr_path(name, "thisimage"));
	unlink(rr_path(name, "preimage"));
	unlink(rr_path(name, "postimage"));
	rmdir(git_path("rr-cache/%s", name));
}

static int git_rerere_gc_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "gc.rerereresolved"))
		cutoff_resolve = git_config_int(var, value);
	else if (!strcmp(var, "gc.rerereunresolved"))
		cutoff_noresolve = git_config_int(var, value);
	else
		return git_default_config(var, value, cb);
	return 0;
}

static void garbage_collect(struct string_list *rr)
{
	struct string_list to_remove = { NULL, 0, 0, 1 };
	DIR *dir;
	struct dirent *e;
	int i, cutoff;
	time_t now = time(NULL), then;

	git_config(git_rerere_gc_config, NULL);
	dir = opendir(git_path("rr-cache"));
	while ((e = readdir(dir))) {
		const char *name = e->d_name;
		if (name[0] == '.' &&
		    (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
			continue;
		then = rerere_created_at(name);
		if (!then)
			continue;
		cutoff = (has_resolution(name)
			  ? cutoff_resolve : cutoff_noresolve);
		if (then < now - cutoff * 86400)
			string_list_append(name, &to_remove);
	}
	for (i = 0; i < to_remove.nr; i++)
		unlink_rr_item(to_remove.items[i].string);
	string_list_clear(&to_remove, 0);
}

static int outf(void *dummy, mmbuffer_t *ptr, int nbuf)
{
	int i;
	for (i = 0; i < nbuf; i++)
		if (write_in_full(1, ptr[i].ptr, ptr[i].size) != ptr[i].size)
			return -1;
	return 0;
}

static int diff_two(const char *file1, const char *label1,
		const char *file2, const char *label2)
{
	xpparam_t xpp;
	xdemitconf_t xecfg;
	xdemitcb_t ecb;
	mmfile_t minus, plus;

	if (read_mmfile(&minus, file1) || read_mmfile(&plus, file2))
		return 1;

	printf("--- a/%s\n+++ b/%s\n", label1, label2);
	fflush(stdout);
	xpp.flags = XDF_NEED_MINIMAL;
	memset(&xecfg, 0, sizeof(xecfg));
	xecfg.ctxlen = 3;
	ecb.outf = outf;
	xdi_diff(&minus, &plus, &xpp, &xecfg, &ecb);

	free(minus.ptr);
	free(plus.ptr);
	return 0;
}

int cmd_rerere(int argc, const char **argv, const char *prefix)
{
	struct string_list merge_rr = { NULL, 0, 0, 1 };
	int i, fd;

	if (argc < 2)
		return rerere();

	fd = setup_rerere(&merge_rr);
	if (fd < 0)
		return 0;

	if (!strcmp(argv[1], "clear")) {
		for (i = 0; i < merge_rr.nr; i++) {
			const char *name = (const char *)merge_rr.items[i].util;
			if (!has_resolution(name))
				unlink_rr_item(name);
		}
		unlink(git_path("rr-cache/MERGE_RR"));
	} else if (!strcmp(argv[1], "gc"))
		garbage_collect(&merge_rr);
	else if (!strcmp(argv[1], "status"))
		for (i = 0; i < merge_rr.nr; i++)
			printf("%s\n", merge_rr.items[i].string);
	else if (!strcmp(argv[1], "diff"))
		for (i = 0; i < merge_rr.nr; i++) {
			const char *path = merge_rr.items[i].string;
			const char *name = (const char *)merge_rr.items[i].util;
			diff_two(rr_path(name, "preimage"), path, path, path);
		}
	else
		usage(git_rerere_usage);

	string_list_clear(&merge_rr, 1);
	return 0;
}
