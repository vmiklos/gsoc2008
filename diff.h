/*
 * Copyright (C) 2005 Junio C Hamano
 */
#ifndef DIFF_H
#define DIFF_H

#include "tree-walk.h"

struct rev_info;
struct diff_options;
struct diff_queue_struct;

typedef void (*change_fn_t)(struct diff_options *options,
		 unsigned old_mode, unsigned new_mode,
		 const unsigned char *old_sha1,
		 const unsigned char *new_sha1,
		 const char *fullpath);

typedef void (*add_remove_fn_t)(struct diff_options *options,
		    int addremove, unsigned mode,
		    const unsigned char *sha1,
		    const char *fullpath);

typedef void (*diff_format_fn_t)(struct diff_queue_struct *q,
		struct diff_options *options, void *data);

#define DIFF_FORMAT_RAW		0x0001
#define DIFF_FORMAT_DIFFSTAT	0x0002
#define DIFF_FORMAT_NUMSTAT	0x0004
#define DIFF_FORMAT_SUMMARY	0x0008
#define DIFF_FORMAT_PATCH	0x0010
#define DIFF_FORMAT_SHORTSTAT	0x0020
#define DIFF_FORMAT_DIRSTAT	0x0040
#define DIFF_FORMAT_CUMULATIVE	0x0080

/* These override all above */
#define DIFF_FORMAT_NAME	0x0100
#define DIFF_FORMAT_NAME_STATUS	0x0200
#define DIFF_FORMAT_CHECKDIFF	0x0400

/* Same as output_format = 0 but we know that -s flag was given
 * and we should not give default value to output_format.
 */
#define DIFF_FORMAT_NO_OUTPUT	0x0800

#define DIFF_FORMAT_CALLBACK	0x1000

#define DIFF_OPT_RECURSIVE           (1 <<  0)
#define DIFF_OPT_TREE_IN_RECURSIVE   (1 <<  1)
#define DIFF_OPT_BINARY              (1 <<  2)
#define DIFF_OPT_TEXT                (1 <<  3)
#define DIFF_OPT_FULL_INDEX          (1 <<  4)
#define DIFF_OPT_SILENT_ON_REMOVE    (1 <<  5)
#define DIFF_OPT_FIND_COPIES_HARDER  (1 <<  6)
#define DIFF_OPT_FOLLOW_RENAMES      (1 <<  7)
#define DIFF_OPT_COLOR_DIFF          (1 <<  8)
#define DIFF_OPT_COLOR_DIFF_WORDS    (1 <<  9)
#define DIFF_OPT_HAS_CHANGES         (1 << 10)
#define DIFF_OPT_QUIET               (1 << 11)
#define DIFF_OPT_NO_INDEX            (1 << 12)
#define DIFF_OPT_ALLOW_EXTERNAL      (1 << 13)
#define DIFF_OPT_EXIT_WITH_STATUS    (1 << 14)
#define DIFF_OPT_REVERSE_DIFF        (1 << 15)
#define DIFF_OPT_CHECK_FAILED        (1 << 16)
#define DIFF_OPT_RELATIVE_NAME       (1 << 17)
#define DIFF_OPT_IGNORE_SUBMODULES   (1 << 18)
#define DIFF_OPT_TST(opts, flag)    ((opts)->flags & DIFF_OPT_##flag)
#define DIFF_OPT_SET(opts, flag)    ((opts)->flags |= DIFF_OPT_##flag)
#define DIFF_OPT_CLR(opts, flag)    ((opts)->flags &= ~DIFF_OPT_##flag)

struct diff_options {
	const char *filter;
	const char *orderfile;
	const char *pickaxe;
	const char *single_follow;
	const char *a_prefix, *b_prefix;
	unsigned flags;
	int context;
	int break_opt;
	int detect_rename;
	int skip_stat_unmatch;
	int line_termination;
	int output_format;
	int pickaxe_opts;
	int rename_score;
	int rename_limit;
	int warn_on_too_large_rename;
	int dirstat_percent;
	int setup;
	int abbrev;
	const char *prefix;
	int prefix_length;
	const char *stat_sep;
	long xdl_opts;

	int stat_width;
	int stat_name_width;

	/* this is set by diffcore for DIFF_FORMAT_PATCH */
	int found_changes;

	FILE *file;
	int close_file;

	int nr_paths;
	const char **paths;
	int *pathlens;
	change_fn_t change;
	add_remove_fn_t add_remove;
	diff_format_fn_t format_callback;
	void *format_callback_data;
};

enum color_diff {
	DIFF_RESET = 0,
	DIFF_PLAIN = 1,
	DIFF_METAINFO = 2,
	DIFF_FRAGINFO = 3,
	DIFF_FILE_OLD = 4,
	DIFF_FILE_NEW = 5,
	DIFF_COMMIT = 6,
	DIFF_WHITESPACE = 7,
};
const char *diff_get_color(int diff_use_color, enum color_diff ix);
#define diff_get_color_opt(o, ix) \
	diff_get_color(DIFF_OPT_TST((o), COLOR_DIFF), ix)


extern const char mime_boundary_leader[];

extern void diff_tree_setup_paths(const char **paths, struct diff_options *);
extern void diff_tree_release_paths(struct diff_options *);
extern int diff_tree(struct tree_desc *t1, struct tree_desc *t2,
		     const char *base, struct diff_options *opt);
extern int diff_tree_sha1(const unsigned char *old, const unsigned char *new,
			  const char *base, struct diff_options *opt);
extern int diff_root_tree_sha1(const unsigned char *new, const char *base,
                               struct diff_options *opt);

struct combine_diff_path {
	struct combine_diff_path *next;
	int len;
	char *path;
	unsigned int mode;
	unsigned char sha1[20];
	struct combine_diff_parent {
		char status;
		unsigned int mode;
		unsigned char sha1[20];
	} parent[FLEX_ARRAY];
};
#define combine_diff_path_size(n, l) \
	(sizeof(struct combine_diff_path) + \
	 sizeof(struct combine_diff_parent) * (n) + (l) + 1)

extern void show_combined_diff(struct combine_diff_path *elem, int num_parent,
			      int dense, struct rev_info *);

extern void diff_tree_combined(const unsigned char *sha1, const unsigned char parent[][20], int num_parent, int dense, struct rev_info *rev);

extern void diff_tree_combined_merge(const unsigned char *sha1, int, struct rev_info *);

extern void diff_addremove(struct diff_options *,
			   int addremove,
			   unsigned mode,
			   const unsigned char *sha1,
			   const char *fullpath);

extern void diff_change(struct diff_options *,
			unsigned mode1, unsigned mode2,
			const unsigned char *sha1,
			const unsigned char *sha2,
			const char *fullpath);

extern void diff_unmerge(struct diff_options *,
			 const char *path,
			 unsigned mode,
			 const unsigned char *sha1);

#define DIFF_SETUP_REVERSE      	1
#define DIFF_SETUP_USE_CACHE		2
#define DIFF_SETUP_USE_SIZE_CACHE	4

extern int git_diff_basic_config(const char *var, const char *value, void *cb);
extern int git_diff_ui_config(const char *var, const char *value, void *cb);
extern int diff_use_color_default;
extern void diff_setup(struct diff_options *);
extern int diff_opt_parse(struct diff_options *, const char **, int);
extern int diff_setup_done(struct diff_options *);

#define DIFF_DETECT_RENAME	1
#define DIFF_DETECT_COPY	2

#define DIFF_PICKAXE_ALL	1
#define DIFF_PICKAXE_REGEX	2

extern void diffcore_std(struct diff_options *);

#define COMMON_DIFF_OPTIONS_HELP \
"\ncommon diff options:\n" \
"  -z            output diff-raw with lines terminated with NUL.\n" \
"  -p            output patch format.\n" \
"  -u            synonym for -p.\n" \
"  --patch-with-raw\n" \
"                output both a patch and the diff-raw format.\n" \
"  --stat        show diffstat instead of patch.\n" \
"  --numstat     show numeric diffstat instead of patch.\n" \
"  --patch-with-stat\n" \
"                output a patch and prepend its diffstat.\n" \
"  --name-only   show only names of changed files.\n" \
"  --name-status show names and status of changed files.\n" \
"  --full-index  show full object name on index lines.\n" \
"  --abbrev=<n>  abbreviate object names in diff-tree header and diff-raw.\n" \
"  -R            swap input file pairs.\n" \
"  -B            detect complete rewrites.\n" \
"  -M            detect renames.\n" \
"  -C            detect copies.\n" \
"  --find-copies-harder\n" \
"                try unchanged files as candidate for copy detection.\n" \
"  -l<n>         limit rename attempts up to <n> paths.\n" \
"  -O<file>      reorder diffs according to the <file>.\n" \
"  -S<string>    find filepair whose only one side contains the string.\n" \
"  --pickaxe-all\n" \
"                show all files diff when -S is used and hit is found.\n" \
"  -a  --text    treat all files as text.\n"

extern int diff_queue_is_empty(void);
extern void diff_flush(struct diff_options*);

/* diff-raw status letters */
#define DIFF_STATUS_ADDED		'A'
#define DIFF_STATUS_COPIED		'C'
#define DIFF_STATUS_DELETED		'D'
#define DIFF_STATUS_MODIFIED		'M'
#define DIFF_STATUS_RENAMED		'R'
#define DIFF_STATUS_TYPE_CHANGED	'T'
#define DIFF_STATUS_UNKNOWN		'X'
#define DIFF_STATUS_UNMERGED		'U'

/* these are not diff-raw status letters proper, but used by
 * diffcore-filter insn to specify additional restrictions.
 */
#define DIFF_STATUS_FILTER_AON		'*'
#define DIFF_STATUS_FILTER_BROKEN	'B'

extern const char *diff_unique_abbrev(const unsigned char *, int);

/* do not report anything on removed paths */
#define DIFF_SILENT_ON_REMOVED 01
/* report racily-clean paths as modified */
#define DIFF_RACY_IS_MODIFIED 02
extern int run_diff_files(struct rev_info *revs, unsigned int option);
extern int run_diff_index(struct rev_info *revs, int cached);

extern int do_diff_cache(const unsigned char *, struct diff_options *);
extern int diff_flush_patch_id(struct diff_options *, unsigned char *);

extern int diff_result_code(struct diff_options *, int);

extern void diff_no_index(struct rev_info *, int, const char **, int, const char *);

#endif /* DIFF_H */
