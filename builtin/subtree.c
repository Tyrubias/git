#include "builtin.h"
#include "cache-tree.h"
#include "commit.h"
#include "config.h"
#include "environment.h"
#include "commit.h"
#include "gettext.h"
#include "parse-options.h"
#include "refs.h"
#include "repository.h"
#include "strbuf.h"
#include "wt-status.h"

#define BUILTIN_SUBTREE_ADD_USAGE \
	N_("git subtree add --prefix=<prefix> <commit>")

#define BUILTIN_SUBTREE_ADD_ALT_USAGE \
	N_("git subtree add --prefix=<prefix> <repository> <ref>")

#define BUILTIN_SUBTREE_MERGE_USAGE \
	N_("git subtree merge --prefix=<prefix> <commit>")

#define BUILTIN_SUBTREE_SPLIT_USAGE \
	N_("git subtree split --prefix=<prefix> [<commit>]")

#define BUILTIN_SUBTREE_PULL_USAGE \
	N_("git subtree pull  --prefix=<prefix> <repository> <ref>")

#define BUILTIN_SUBTREE_PUSH_USAGE \
	N_("git subtree push  --prefix=<prefix> <repository> <refspec>")

static const char *const git_subtree_usage[] = { BUILTIN_SUBTREE_ADD_USAGE,
						 BUILTIN_SUBTREE_ADD_ALT_USAGE,
						 BUILTIN_SUBTREE_MERGE_USAGE,
						 BUILTIN_SUBTREE_SPLIT_USAGE,
						 BUILTIN_SUBTREE_PULL_USAGE,
						 BUILTIN_SUBTREE_PUSH_USAGE,
						 NULL };

static const char *const git_subtree_add_usage[] = {
	BUILTIN_SUBTREE_ADD_USAGE, BUILTIN_SUBTREE_ADD_ALT_USAGE, NULL
};

static int path_exists(const char *path)
{
	struct stat sb;
	return !stat(path, &sb);
}

static int add_commit(struct commit *commit, int rejoin, int squash)
{
	return 0;
}

static int add_repository(const char *repository, const char *ref)
{
	return 0;
}

static int add(int argc, const char **argv, const char *prefix)
{
	const char *subtree_prefix = NULL;
	const char *commit_message = NULL;
	char *ref = NULL;
	struct commit *commit = NULL;
	int squash = 0;
	struct option options[] = {
		OPT_STRING(0, "prefix", &subtree_prefix, N_("prefix"),
			   N_("the name of the subdir to split out")),
		OPT_BOOL(0, "squash", &squash,
			 N_("merge subtree changes as a single commit")),
		OPT_STRING_F(
			'm', "message", &commit_message, N_("message"),
			N_("use the given message as the commit message for the merge commit"),
			PARSE_OPT_NONEG),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options, git_subtree_add_usage,
			     0);
	if (!subtree_prefix)
		die(_("parameter '%s' is required"), "--prefix");
	if (path_exists(subtree_prefix))
		die(_("prefix '%s' already exists"), subtree_prefix);

	require_clean_work_tree(the_repository, N_("subtree add"),
				_("Please commit or stash them."), 0, 0);

	if (argc == 1) {
		commit = lookup_commit_reference_by_name(argv[0]);
		if (!commit)
			die(_("fatal: '%s' does not refer to a commit"),
			    argv[0]);

		return add_commit(commit, 0, squash);
	} else if (argc == 2) {
		ref = xstrfmt("refs/heads/%s", argv[1]);
		if (!check_refname_format(ref, 0)) {
			free(ref);
			die(_("fatal: '%s' does not look like a ref"), argv[1]);
		}
		free(ref);

		return add_repository(argv[0], argv[1]);
	} else
		usage_with_options(git_subtree_add_usage, options);
}

static int merge(int argc, const char **argv, const char *prefix)
{
	printf(_("git subtree merge\n"));
	return 0;
}

static int split(int argc, const char **argv, const char *prefix)
{
	printf(_("git subtree split\n"));
	return 0;
}

static int pull(int argc, const char **argv, const char *prefix)
{
	printf(_("git subtree pull\n"));
	return 0;
}

static int push(int argc, const char **argv, const char *prefix)
{
	printf(_("git subtree push\n"));
	return 0;
}

int cmd_subtree(int argc, const char **argv, const char *prefix)
{
	parse_opt_subcommand_fn *fn = NULL;
	struct option options[] = { OPT_SUBCOMMAND("add", &fn, add),
				    OPT_SUBCOMMAND("merge", &fn, merge),
				    OPT_SUBCOMMAND("split", &fn, split),
				    OPT_SUBCOMMAND("pull", &fn, pull),
				    OPT_SUBCOMMAND("push", &fn, push),
				    OPT_END() };

	git_config(git_default_config, NULL);

	if (!prefix)
		prefix = "";

	argc = parse_options(argc, argv, prefix, options, git_subtree_usage, 0);

	prepare_repo_settings(the_repository);
	the_repository->settings.command_requires_full_index = 0;

	return fn(argc, argv, prefix);
}
