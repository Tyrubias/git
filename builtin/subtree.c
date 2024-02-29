#include "builtin.h"
#include "cache-tree.h"
#include "commit.h"
#include "config.h"
#include "environment.h"
#include "gettext.h"
#include "parse-options.h"

#define BUILTIN_SUBTREE_ADD_USAGE \
	N_("git subtree add --prefix=<prefix> <commit>")

#define BUILTIN_SUBTREE_ADD_ALT_USAGE \
	N_("git subtree add   --prefix=<prefix> <repository> <ref>")

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

static int add(int argc, const char **argv, const char *prefix)
{
	printf(_("git subtree add\n"));
	return 0;
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
