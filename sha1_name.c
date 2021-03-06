#include "cache.h"
#include "tag.h"
#include "commit.h"
#include "tree.h"
#include "blob.h"
#include "tree-walk.h"
#include "refs.h"

static int find_short_object_filename(int len, const char *name, unsigned char *sha1)
{
	struct alternate_object_database *alt;
	char hex[40];
	int found = 0;
	static struct alternate_object_database *fakeent;

	if (!fakeent) {
		const char *objdir = get_object_directory();
		int objdir_len = strlen(objdir);
		int entlen = objdir_len + 43;
		fakeent = xmalloc(sizeof(*fakeent) + entlen);
		memcpy(fakeent->base, objdir, objdir_len);
		fakeent->name = fakeent->base + objdir_len + 1;
		fakeent->name[-1] = '/';
	}
	fakeent->next = alt_odb_list;

	sprintf(hex, "%.2s", name);
	for (alt = fakeent; alt && found < 2; alt = alt->next) {
		struct dirent *de;
		DIR *dir;
		sprintf(alt->name, "%.2s/", name);
		dir = opendir(alt->base);
		if (!dir)
			continue;
		while ((de = readdir(dir)) != NULL) {
			if (strlen(de->d_name) != 38)
				continue;
			if (memcmp(de->d_name, name + 2, len - 2))
				continue;
			if (!found) {
				memcpy(hex + 2, de->d_name, 38);
				found++;
			}
			else if (memcmp(hex + 2, de->d_name, 38)) {
				found = 2;
				break;
			}
		}
		closedir(dir);
	}
	if (found == 1)
		return get_sha1_hex(hex, sha1) == 0;
	return found;
}

static int match_sha(unsigned len, const unsigned char *a, const unsigned char *b)
{
	do {
		if (*a != *b)
			return 0;
		a++;
		b++;
		len -= 2;
	} while (len > 1);
	if (len)
		if ((*a ^ *b) & 0xf0)
			return 0;
	return 1;
}

static int find_short_packed_object(int len, const unsigned char *match, unsigned char *sha1)
{
	struct packed_git *p;
	const unsigned char *found_sha1 = NULL;
	int found = 0;

	prepare_packed_git();
	for (p = packed_git; p && found < 2; p = p->next) {
		uint32_t num, last;
		uint32_t first = 0;
		open_pack_index(p);
		num = p->num_objects;
		last = num;
		while (first < last) {
			uint32_t mid = (first + last) / 2;
			const unsigned char *now;
			int cmp;

			now = nth_packed_object_sha1(p, mid);
			cmp = hashcmp(match, now);
			if (!cmp) {
				first = mid;
				break;
			}
			if (cmp > 0) {
				first = mid+1;
				continue;
			}
			last = mid;
		}
		if (first < num) {
			const unsigned char *now, *next;
		       now = nth_packed_object_sha1(p, first);
			if (match_sha(len, match, now)) {
				next = nth_packed_object_sha1(p, first+1);
			       if (!next|| !match_sha(len, match, next)) {
					/* unique within this pack */
					if (!found) {
						found_sha1 = now;
						found++;
					}
					else if (hashcmp(found_sha1, now)) {
						found = 2;
						break;
					}
				}
				else {
					/* not even unique within this pack */
					found = 2;
					break;
				}
			}
		}
	}
	if (found == 1)
		hashcpy(sha1, found_sha1);
	return found;
}

#define SHORT_NAME_NOT_FOUND (-1)
#define SHORT_NAME_AMBIGUOUS (-2)

static int find_unique_short_object(int len, char *canonical,
				    unsigned char *res, unsigned char *sha1)
{
	int has_unpacked, has_packed;
	unsigned char unpacked_sha1[20], packed_sha1[20];

	prepare_alt_odb();
	has_unpacked = find_short_object_filename(len, canonical, unpacked_sha1);
	has_packed = find_short_packed_object(len, res, packed_sha1);
	if (!has_unpacked && !has_packed)
		return SHORT_NAME_NOT_FOUND;
	if (1 < has_unpacked || 1 < has_packed)
		return SHORT_NAME_AMBIGUOUS;
	if (has_unpacked != has_packed) {
		hashcpy(sha1, (has_packed ? packed_sha1 : unpacked_sha1));
		return 0;
	}
	/* Both have unique ones -- do they match? */
	if (hashcmp(packed_sha1, unpacked_sha1))
		return SHORT_NAME_AMBIGUOUS;
	hashcpy(sha1, packed_sha1);
	return 0;
}

static int get_short_sha1(const char *name, int len, unsigned char *sha1,
			  int quietly)
{
	int i, status;
	char canonical[40];
	unsigned char res[20];

	if (len < MINIMUM_ABBREV || len > 40)
		return -1;
	hashclr(res);
	memset(canonical, 'x', 40);
	for (i = 0; i < len ;i++) {
		unsigned char c = name[i];
		unsigned char val;
		if (c >= '0' && c <= '9')
			val = c - '0';
		else if (c >= 'a' && c <= 'f')
			val = c - 'a' + 10;
		else if (c >= 'A' && c <='F') {
			val = c - 'A' + 10;
			c -= 'A' - 'a';
		}
		else
			return -1;
		canonical[i] = c;
		if (!(i & 1))
			val <<= 4;
		res[i >> 1] |= val;
	}

	status = find_unique_short_object(i, canonical, res, sha1);
	if (!quietly && (status == SHORT_NAME_AMBIGUOUS))
		return error("short SHA1 %.*s is ambiguous.", len, canonical);
	return status;
}

const char *find_unique_abbrev(const unsigned char *sha1, int len)
{
	int status, exists;
	static char hex[41];

	exists = has_sha1_file(sha1);
	memcpy(hex, sha1_to_hex(sha1), 40);
	if (len == 40 || !len)
		return hex;
	while (len < 40) {
		unsigned char sha1_ret[20];
		status = get_short_sha1(hex, len, sha1_ret, 1);
		if (exists
		    ? !status
		    : status == SHORT_NAME_NOT_FOUND) {
			hex[len] = 0;
			return hex;
		}
		len++;
	}
	return hex;
}

static int ambiguous_path(const char *path, int len)
{
	int slash = 1;
	int cnt;

	for (cnt = 0; cnt < len; cnt++) {
		switch (*path++) {
		case '\0':
			break;
		case '/':
			if (slash)
				break;
			slash = 1;
			continue;
		case '.':
			continue;
		default:
			slash = 0;
			continue;
		}
		break;
	}
	return slash;
}

int dwim_ref(const char *str, int len, unsigned char *sha1, char **ref)
{
	const char **p, *r;
	int refs_found = 0;

	*ref = NULL;
	for (p = ref_rev_parse_rules; *p; p++) {
		unsigned char sha1_from_ref[20];
		unsigned char *this_result;

		this_result = refs_found ? sha1_from_ref : sha1;
		r = resolve_ref(mkpath(*p, len, str), this_result, 1, NULL);
		if (r) {
			if (!refs_found++)
				*ref = xstrdup(r);
			if (!warn_ambiguous_refs)
				break;
		}
	}
	return refs_found;
}

int dwim_log(const char *str, int len, unsigned char *sha1, char **log)
{
	const char **p;
	int logs_found = 0;

	*log = NULL;
	for (p = ref_rev_parse_rules; *p; p++) {
		struct stat st;
		unsigned char hash[20];
		char path[PATH_MAX];
		const char *ref, *it;

		strcpy(path, mkpath(*p, len, str));
		ref = resolve_ref(path, hash, 1, NULL);
		if (!ref)
			continue;
		if (!stat(git_path("logs/%s", path), &st) &&
		    S_ISREG(st.st_mode))
			it = path;
		else if (strcmp(ref, path) &&
			 !stat(git_path("logs/%s", ref), &st) &&
			 S_ISREG(st.st_mode))
			it = ref;
		else
			continue;
		if (!logs_found++) {
			*log = xstrdup(it);
			hashcpy(sha1, hash);
		}
		if (!warn_ambiguous_refs)
			break;
	}
	return logs_found;
}

static int get_sha1_basic(const char *str, int len, unsigned char *sha1)
{
	static const char *warning = "warning: refname '%.*s' is ambiguous.\n";
	char *real_ref = NULL;
	int refs_found = 0;
	int at, reflog_len;

	if (len == 40 && !get_sha1_hex(str, sha1))
		return 0;

	/* basic@{time or number} format to query ref-log */
	reflog_len = at = 0;
	if (str[len-1] == '}') {
		for (at = 0; at < len - 1; at++) {
			if (str[at] == '@' && str[at+1] == '{') {
				reflog_len = (len-1) - (at+2);
				len = at;
				break;
			}
		}
	}

	/* Accept only unambiguous ref paths. */
	if (len && ambiguous_path(str, len))
		return -1;

	if (!len && reflog_len) {
		/* allow "@{...}" to mean the current branch reflog */
		refs_found = dwim_ref("HEAD", 4, sha1, &real_ref);
	} else if (reflog_len)
		refs_found = dwim_log(str, len, sha1, &real_ref);
	else
		refs_found = dwim_ref(str, len, sha1, &real_ref);

	if (!refs_found)
		return -1;

	if (warn_ambiguous_refs && refs_found > 1)
		fprintf(stderr, warning, len, str);

	if (reflog_len) {
		int nth, i;
		unsigned long at_time;
		unsigned long co_time;
		int co_tz, co_cnt;

		/* Is it asking for N-th entry, or approxidate? */
		for (i = nth = 0; 0 <= nth && i < reflog_len; i++) {
			char ch = str[at+2+i];
			if ('0' <= ch && ch <= '9')
				nth = nth * 10 + ch - '0';
			else
				nth = -1;
		}
		if (0 <= nth)
			at_time = 0;
		else {
			char *tmp = xstrndup(str + at + 2, reflog_len);
			at_time = approxidate(tmp);
			free(tmp);
		}
		if (read_ref_at(real_ref, at_time, nth, sha1, NULL,
				&co_time, &co_tz, &co_cnt)) {
			if (at_time)
				fprintf(stderr,
					"warning: Log for '%.*s' only goes "
					"back to %s.\n", len, str,
					show_date(co_time, co_tz, DATE_RFC2822));
			else
				fprintf(stderr,
					"warning: Log for '%.*s' only has "
					"%d entries.\n", len, str, co_cnt);
		}
	}

	free(real_ref);
	return 0;
}

static int get_sha1_1(const char *name, int len, unsigned char *sha1);

static int get_parent(const char *name, int len,
		      unsigned char *result, int idx)
{
	unsigned char sha1[20];
	int ret = get_sha1_1(name, len, sha1);
	struct commit *commit;
	struct commit_list *p;

	if (ret)
		return ret;
	commit = lookup_commit_reference(sha1);
	if (!commit)
		return -1;
	if (parse_commit(commit))
		return -1;
	if (!idx) {
		hashcpy(result, commit->object.sha1);
		return 0;
	}
	p = commit->parents;
	while (p) {
		if (!--idx) {
			hashcpy(result, p->item->object.sha1);
			return 0;
		}
		p = p->next;
	}
	return -1;
}

static int get_nth_ancestor(const char *name, int len,
			    unsigned char *result, int generation)
{
	unsigned char sha1[20];
	struct commit *commit;
	int ret;

	ret = get_sha1_1(name, len, sha1);
	if (ret)
		return ret;
	commit = lookup_commit_reference(sha1);
	if (!commit)
		return -1;

	while (generation--) {
		if (parse_commit(commit) || !commit->parents)
			return -1;
		commit = commit->parents->item;
	}
	hashcpy(result, commit->object.sha1);
	return 0;
}

struct object *peel_to_type(const char *name, int namelen,
			    struct object *o, enum object_type expected_type)
{
	if (name && !namelen)
		namelen = strlen(name);
	if (!o) {
		unsigned char sha1[20];
		if (get_sha1_1(name, namelen, sha1))
			return NULL;
		o = parse_object(sha1);
	}
	while (1) {
		if (!o || (!o->parsed && !parse_object(o->sha1)))
			return NULL;
		if (o->type == expected_type)
			return o;
		if (o->type == OBJ_TAG)
			o = ((struct tag*) o)->tagged;
		else if (o->type == OBJ_COMMIT)
			o = &(((struct commit *) o)->tree->object);
		else {
			if (name)
				error("%.*s: expected %s type, but the object "
				      "dereferences to %s type",
				      namelen, name, typename(expected_type),
				      typename(o->type));
			return NULL;
		}
	}
}

static int peel_onion(const char *name, int len, unsigned char *sha1)
{
	unsigned char outer[20];
	const char *sp;
	unsigned int expected_type = 0;
	struct object *o;

	/*
	 * "ref^{type}" dereferences ref repeatedly until you cannot
	 * dereference anymore, or you get an object of given type,
	 * whichever comes first.  "ref^{}" means just dereference
	 * tags until you get a non-tag.  "ref^0" is a shorthand for
	 * "ref^{commit}".  "commit^{tree}" could be used to find the
	 * top-level tree of the given commit.
	 */
	if (len < 4 || name[len-1] != '}')
		return -1;

	for (sp = name + len - 1; name <= sp; sp--) {
		int ch = *sp;
		if (ch == '{' && name < sp && sp[-1] == '^')
			break;
	}
	if (sp <= name)
		return -1;

	sp++; /* beginning of type name, or closing brace for empty */
	if (!strncmp(commit_type, sp, 6) && sp[6] == '}')
		expected_type = OBJ_COMMIT;
	else if (!strncmp(tree_type, sp, 4) && sp[4] == '}')
		expected_type = OBJ_TREE;
	else if (!strncmp(blob_type, sp, 4) && sp[4] == '}')
		expected_type = OBJ_BLOB;
	else if (sp[0] == '}')
		expected_type = OBJ_NONE;
	else
		return -1;

	if (get_sha1_1(name, sp - name - 2, outer))
		return -1;

	o = parse_object(outer);
	if (!o)
		return -1;
	if (!expected_type) {
		o = deref_tag(o, name, sp - name - 2);
		if (!o || (!o->parsed && !parse_object(o->sha1)))
			return -1;
		hashcpy(sha1, o->sha1);
	}
	else {
		/*
		 * At this point, the syntax look correct, so
		 * if we do not get the needed object, we should
		 * barf.
		 */
		o = peel_to_type(name, len, o, expected_type);
		if (o) {
			hashcpy(sha1, o->sha1);
			return 0;
		}
		return -1;
	}
	return 0;
}

static int get_describe_name(const char *name, int len, unsigned char *sha1)
{
	const char *cp;

	for (cp = name + len - 1; name + 2 <= cp; cp--) {
		char ch = *cp;
		if (hexval(ch) & ~0377) {
			/* We must be looking at g in "SOMETHING-g"
			 * for it to be describe output.
			 */
			if (ch == 'g' && cp[-1] == '-') {
				cp++;
				len -= cp - name;
				return get_short_sha1(cp, len, sha1, 1);
			}
		}
	}
	return -1;
}

static int get_sha1_1(const char *name, int len, unsigned char *sha1)
{
	int ret, has_suffix;
	const char *cp;

	/*
	 * "name~3" is "name^^^", "name~" is "name~1", and "name^" is "name^1".
	 */
	has_suffix = 0;
	for (cp = name + len - 1; name <= cp; cp--) {
		int ch = *cp;
		if ('0' <= ch && ch <= '9')
			continue;
		if (ch == '~' || ch == '^')
			has_suffix = ch;
		break;
	}

	if (has_suffix) {
		int num = 0;
		int len1 = cp - name;
		cp++;
		while (cp < name + len)
			num = num * 10 + *cp++ - '0';
		if (!num && len1 == len - 1)
			num = 1;
		if (has_suffix == '^')
			return get_parent(name, len1, sha1, num);
		/* else if (has_suffix == '~') -- goes without saying */
		return get_nth_ancestor(name, len1, sha1, num);
	}

	ret = peel_onion(name, len, sha1);
	if (!ret)
		return 0;

	ret = get_sha1_basic(name, len, sha1);
	if (!ret)
		return 0;

	/* It could be describe output that is "SOMETHING-gXXXX" */
	ret = get_describe_name(name, len, sha1);
	if (!ret)
		return 0;

	return get_short_sha1(name, len, sha1, 0);
}

static int handle_one_ref(const char *path,
		const unsigned char *sha1, int flag, void *cb_data)
{
	struct commit_list **list = cb_data;
	struct object *object = parse_object(sha1);
	if (!object)
		return 0;
	if (object->type == OBJ_TAG) {
		object = deref_tag(object, path, strlen(path));
		if (!object)
			return 0;
	}
	if (object->type != OBJ_COMMIT)
		return 0;
	insert_by_date((struct commit *)object, list);
	return 0;
}

/*
 * This interprets names like ':/Initial revision of "git"' by searching
 * through history and returning the first commit whose message starts
 * with the given string.
 *
 * For future extension, ':/!' is reserved. If you want to match a message
 * beginning with a '!', you have to repeat the exclamation mark.
 */

#define ONELINE_SEEN (1u<<20)
static int get_sha1_oneline(const char *prefix, unsigned char *sha1)
{
	struct commit_list *list = NULL, *backup = NULL, *l;
	int retval = -1;
	char *temp_commit_buffer = NULL;

	if (prefix[0] == '!') {
		if (prefix[1] != '!')
			die ("Invalid search pattern: %s", prefix);
		prefix++;
	}
	for_each_ref(handle_one_ref, &list);
	for (l = list; l; l = l->next)
		commit_list_insert(l->item, &backup);
	while (list) {
		char *p;
		struct commit *commit;
		enum object_type type;
		unsigned long size;

		commit = pop_most_recent_commit(&list, ONELINE_SEEN);
		if (!parse_object(commit->object.sha1))
			continue;
		free(temp_commit_buffer);
		if (commit->buffer)
			p = commit->buffer;
		else {
			p = read_sha1_file(commit->object.sha1, &type, &size);
			if (!p)
				continue;
			temp_commit_buffer = p;
		}
		if (!(p = strstr(p, "\n\n")))
			continue;
		if (!prefixcmp(p + 2, prefix)) {
			hashcpy(sha1, commit->object.sha1);
			retval = 0;
			break;
		}
	}
	free(temp_commit_buffer);
	free_commit_list(list);
	for (l = backup; l; l = l->next)
		clear_commit_marks(l->item, ONELINE_SEEN);
	return retval;
}

/*
 * This is like "get_sha1_basic()", except it allows "sha1 expressions",
 * notably "xyz^" for "parent of xyz"
 */
int get_sha1(const char *name, unsigned char *sha1)
{
	unsigned unused;
	return get_sha1_with_mode(name, sha1, &unused);
}

int get_sha1_with_mode(const char *name, unsigned char *sha1, unsigned *mode)
{
	int ret, bracket_depth;
	int namelen = strlen(name);
	const char *cp;

	*mode = S_IFINVALID;
	ret = get_sha1_1(name, namelen, sha1);
	if (!ret)
		return ret;
	/* sha1:path --> object name of path in ent sha1
	 * :path -> object name of path in index
	 * :[0-3]:path -> object name of path in index at stage
	 */
	if (name[0] == ':') {
		int stage = 0;
		struct cache_entry *ce;
		int pos;
		if (namelen > 2 && name[1] == '/')
			return get_sha1_oneline(name + 2, sha1);
		if (namelen < 3 ||
		    name[2] != ':' ||
		    name[1] < '0' || '3' < name[1])
			cp = name + 1;
		else {
			stage = name[1] - '0';
			cp = name + 3;
		}
		namelen = namelen - (cp - name);
		if (!active_cache)
			read_cache();
		pos = cache_name_pos(cp, namelen);
		if (pos < 0)
			pos = -pos - 1;
		while (pos < active_nr) {
			ce = active_cache[pos];
			if (ce_namelen(ce) != namelen ||
			    memcmp(ce->name, cp, namelen))
				break;
			if (ce_stage(ce) == stage) {
				hashcpy(sha1, ce->sha1);
				*mode = ce->ce_mode;
				return 0;
			}
			pos++;
		}
		return -1;
	}
	for (cp = name, bracket_depth = 0; *cp; cp++) {
		if (*cp == '{')
			bracket_depth++;
		else if (bracket_depth && *cp == '}')
			bracket_depth--;
		else if (!bracket_depth && *cp == ':')
			break;
	}
	if (*cp == ':') {
		unsigned char tree_sha1[20];
		if (!get_sha1_1(name, cp-name, tree_sha1))
			return get_tree_entry(tree_sha1, cp+1, sha1,
					      mode);
	}
	return ret;
}
