#include "attr.h"
static struct ll_diff_driver {
	const char *name;
	struct ll_diff_driver *next;
	char *cmd;
} *user_diff, **user_diff_tail;

/*
 * Currently there is only "diff.<drivername>.command" variable;
 * because there are "diff.color.<slot>" variables, we are parsing
 * this in a bit convoluted way to allow low level diff driver
 * called "color".
 */
static int parse_lldiff_command(const char *var, const char *ep, const char *value)
{
	const char *name;
	int namelen;
	struct ll_diff_driver *drv;

	name = var + 5;
	namelen = ep - name;
	for (drv = user_diff; drv; drv = drv->next)
		if (!strncmp(drv->name, name, namelen) && !drv->name[namelen])
			break;
	if (!drv) {
		char *namebuf;
		drv = xcalloc(1, sizeof(struct ll_diff_driver));
		namebuf = xmalloc(namelen + 1);
		memcpy(namebuf, name, namelen);
		namebuf[namelen] = 0;
		drv->name = namebuf;
		drv->next = NULL;
		if (!user_diff_tail)
			user_diff_tail = &user_diff;
		*user_diff_tail = drv;
		user_diff_tail = &(drv->next);
	}

	if (!value)
		return error("%s: lacks value", var);
	drv->cmd = strdup(value);
	return 0;
}

	if (!prefixcmp(var, "diff.")) {
		const char *ep = strrchr(var, '.');

		if (ep != var + 4 && !strcmp(ep, ".command"))
			return parse_lldiff_command(var, ep, value);
	}

static void setup_diff_attr_check(struct git_attr_check *check)
{
	static struct git_attr *attr_diff;

	if (!attr_diff)
		attr_diff = git_attr("diff", 4);
	check->attr = attr_diff;
}

static int file_is_binary(struct diff_filespec *one)
	unsigned long sz;
	struct git_attr_check attr_diff_check;

	setup_diff_attr_check(&attr_diff_check);
	if (!git_checkattr(one->path, 1, &attr_diff_check)) {
		const char *value = attr_diff_check.value;
		if (ATTR_TRUE(value))
			return 0;
		else if (ATTR_FALSE(value))
			return 1;
	}

	if (!one->data) {
		if (!DIFF_FILE_VALID(one))
			return 0;
		diff_populate_filespec(one, 0);
	}
	sz = one->size;
	return !!memchr(one->data, 0, sz);
	if (!o->text && (file_is_binary(one) || file_is_binary(two))) {
	if (file_is_binary(one) || file_is_binary(two)) {
	if (file_is_binary(two))
static int diff_populate_gitlink(struct diff_filespec *s, int size_only)
{
	int len;
	char *data = xmalloc(100);
	len = snprintf(data, 100,
		"Subproject commit %s\n", sha1_to_hex(s->sha1));
	s->data = data;
	s->size = len;
	s->should_free = 1;
	if (size_only) {
		s->data = NULL;
		free(data);
	}
	return 0;
}


	if (S_ISDIRLNK(s->mode))
		return diff_populate_gitlink(s, size_only);

		buf = convert_to_git(s->path, s->data, &size);
		if (buf) {
static const char *external_diff_attr(const char *name)
{
	struct git_attr_check attr_diff_check;

	setup_diff_attr_check(&attr_diff_check);
	if (!git_checkattr(name, 1, &attr_diff_check)) {
		const char *value = attr_diff_check.value;
		if (!ATTR_TRUE(value) &&
		    !ATTR_FALSE(value) &&
		    !ATTR_UNSET(value)) {
			struct ll_diff_driver *drv;

			if (!user_diff_tail) {
				user_diff_tail = &user_diff;
				git_config(git_diff_ui_config);
			}
			for (drv = user_diff; drv; drv = drv->next)
				if (!strcmp(drv->name, value))
					return drv->cmd;
		}
	}
	return NULL;
}

	if (!o->allow_external)
		pgm = NULL;
	else {
		const char *cmd = external_diff_attr(name);
		if (cmd)
			pgm = cmd;
	}

			if ((!fill_mmfile(&mf, one) && file_is_binary(one)) ||
			    (!fill_mmfile(&mf, two) && file_is_binary(two)))
		if (file_is_binary(p->two))