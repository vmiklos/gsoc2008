/*
 * "git rm" builtin command
 *
 * Copyright (C) Linus Torvalds 2006
 */
#include "cache.h"
#include "builtin.h"
#include "dir.h"
#include "cache-tree.h"
#include "tree-walk.h"
#include "parse-options.h"

static const char * const builtin_rm_usage[] = {
	"git rm [options] [--] <file>...",
	NULL
};

static struct {
	int nr, alloc;
	const char **name;
} list;

static void add_list(const char *name)
{
	if (list.nr >= list.alloc) {
		list.alloc = alloc_nr(list.alloc);
		list.name = xrealloc(list.name, list.alloc * sizeof(const char *));
	}
	list.name[list.nr++] = name;
}

static int remove_file(const char *name)
{
	int ret;
	char *slash;

	ret = unlink(name);
	if (ret && errno == ENOENT)
		/* The user has removed it from the filesystem by hand */
		ret = errno = 0;

	if (!ret && (slash = strrchr(name, '/'))) {
		char *n = xstrdup(name);
		do {
			n[slash - name] = 0;
			name = n;
		} while (!rmdir(name) && (slash = strrchr(name, '/')));
	}
	return ret;
}

static int check_local_mod(unsigned char *head, int index_only)
{
	/* items in list are already sorted in the cache order,
	 * so we could do this a lot more efficiently by using
	 * tree_desc based traversal if we wanted to, but I am
	 * lazy, and who cares if removal of files is a tad
	 * slower than the theoretical maximum speed?
	 */
	int i, no_head;
	int errs = 0;

	no_head = is_null_sha1(head);
	for (i = 0; i < list.nr; i++) {
		struct stat st;
		int pos;
		struct cache_entry *ce;
		const char *name = list.name[i];
		unsigned char sha1[20];
		unsigned mode;
		int local_changes = 0;
		int staged_changes = 0;

		pos = cache_name_pos(name, strlen(name));
		if (pos < 0)
			continue; /* removing unmerged entry */
		ce = active_cache[pos];

		if (lstat(ce->name, &st) < 0) {
			if (errno != ENOENT)
				fprintf(stderr, "warning: '%s': %s",
					ce->name, strerror(errno));
			/* It already vanished from the working tree */
			continue;
		}
		else if (S_ISDIR(st.st_mode)) {
			/* if a file was removed and it is now a
			 * directory, that is the same as ENOENT as
			 * far as git is concerned; we do not track
			 * directories.
			 */
			continue;
		}
		if (ce_match_stat(ce, &st, 0))
			local_changes = 1;
		if (no_head
		     || get_tree_entry(head, name, sha1, &mode)
		     || ce->ce_mode != create_ce_mode(mode)
		     || hashcmp(ce->sha1, sha1))
			staged_changes = 1;

		if (local_changes && staged_changes)
			errs = error("'%s' has staged content different "
				     "from both the file and the HEAD\n"
				     "(use -f to force removal)", name);
		else if (!index_only) {
			/* It's not dangerous to git-rm --cached a
			 * file if the index matches the file or the
			 * HEAD, since it means the deleted content is
			 * still available somewhere.
			 */
			if (staged_changes)
				errs = error("'%s' has changes staged in the index\n"
					     "(use --cached to keep the file, "
					     "or -f to force removal)", name);
			if (local_changes)
				errs = error("'%s' has local modifications\n"
					     "(use --cached to keep the file, "
					     "or -f to force removal)", name);
		}
	}
	return errs;
}

static struct lock_file lock_file;

static int show_only = 0, force = 0, index_only = 0, recursive = 0, quiet = 0;
static int ignore_unmatch = 0;

static struct option builtin_rm_options[] = {
	OPT__DRY_RUN(&show_only),
	OPT__QUIET(&quiet),
	OPT_BOOLEAN( 0 , "cached",         &index_only, "only remove from the index"),
	OPT_BOOLEAN('f', "force",          &force,      "override the up-to-date check"),
	OPT_BOOLEAN('r', NULL,             &recursive,  "allow recursive removal"),
	OPT_BOOLEAN( 0 , "ignore-unmatch", &ignore_unmatch,
				"exit with a zero status even if nothing matched"),
	OPT_END(),
};

int cmd_rm(int argc, const char **argv, const char *prefix)
{
	int i, newfd;
	const char **pathspec;
	char *seen;

	git_config(git_default_config, NULL);

	argc = parse_options(argc, argv, builtin_rm_options, builtin_rm_usage, 0);
	if (!argc)
		usage_with_options(builtin_rm_usage, builtin_rm_options);

	if (!index_only)
		setup_work_tree();

	newfd = hold_locked_index(&lock_file, 1);

	if (read_cache() < 0)
		die("index file corrupt");

	pathspec = get_pathspec(prefix, argv);
	seen = NULL;
	for (i = 0; pathspec[i] ; i++)
		/* nothing */;
	seen = xcalloc(i, 1);

	for (i = 0; i < active_nr; i++) {
		struct cache_entry *ce = active_cache[i];
		if (!match_pathspec(pathspec, ce->name, ce_namelen(ce), 0, seen))
			continue;
		add_list(ce->name);
	}

	if (pathspec) {
		const char *match;
		int seen_any = 0;
		for (i = 0; (match = pathspec[i]) != NULL ; i++) {
			if (!seen[i]) {
				if (!ignore_unmatch) {
					die("pathspec '%s' did not match any files",
					    match);
				}
			}
			else {
				seen_any = 1;
			}
			if (!recursive && seen[i] == MATCHED_RECURSIVELY)
				die("not removing '%s' recursively without -r",
				    *match ? match : ".");
		}

		if (! seen_any)
			exit(0);
	}

	/*
	 * If not forced, the file, the index and the HEAD (if exists)
	 * must match; but the file can already been removed, since
	 * this sequence is a natural "novice" way:
	 *
	 *	rm F; git rm F
	 *
	 * Further, if HEAD commit exists, "diff-index --cached" must
	 * report no changes unless forced.
	 */
	if (!force) {
		unsigned char sha1[20];
		if (get_sha1("HEAD", sha1))
			hashclr(sha1);
		if (check_local_mod(sha1, index_only))
			exit(1);
	}

	/*
	 * First remove the names from the index: we won't commit
	 * the index unless all of them succeed.
	 */
	for (i = 0; i < list.nr; i++) {
		const char *path = list.name[i];
		if (!quiet)
			printf("rm '%s'\n", path);

		if (remove_file_from_cache(path))
			die("git-rm: unable to remove %s", path);
	}

	if (show_only)
		return 0;

	/*
	 * Then, unless we used "--cached", remove the filenames from
	 * the workspace. If we fail to remove the first one, we
	 * abort the "git rm" (but once we've successfully removed
	 * any file at all, we'll go ahead and commit to it all:
	 * by then we've already committed ourselves and can't fail
	 * in the middle)
	 */
	if (!index_only) {
		int removed = 0;
		for (i = 0; i < list.nr; i++) {
			const char *path = list.name[i];
			if (!remove_file(path)) {
				removed = 1;
				continue;
			}
			if (!removed)
				die("git-rm: %s: %s", path, strerror(errno));
		}
	}

	if (active_cache_changed) {
		if (write_cache(newfd, active_cache, active_nr) ||
		    commit_locked_index(&lock_file))
			die("Unable to write new index file");
	}

	return 0;
}
