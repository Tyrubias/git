#include "builtin.h"
#include "cache-tree.h"
#include "commit.h"
#include "config.h"
#include "environment.h"
#include "commit.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "lockfile.h"
#include "object.h"
#include "object-name.h"
#include "parse-options.h"
#include "refs.h"
#include "repository.h"
#include "run-command.h"
#include "strbuf.h"
#include "tree.h"
#include "tree-walk.h"
#include "unpack-trees.h"
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

static int read_tree_prefix(struct commit *commit, const char *prefix)
{
	struct tree *tree;
	struct lock_file lock_file = LOCK_INIT;
	struct tree_desc tree_desc;
	struct unpack_trees_options opts;

	tree = repo_get_commit_tree(the_repository, commit);

	if (!tree)
		return error(_("couldn't get tree for commit %s"),
			     oid_to_hex(&commit->object.oid));

	repo_hold_locked_index(the_repository, &lock_file, LOCK_DIE_ON_ERROR);

	if (parse_tree(tree))
		return error(_("couldn't parse tree for commit %s"),
			     oid_to_hex(&commit->object.oid));

	init_tree_desc(&tree_desc, &tree->object.oid, tree->buffer, tree->size);

	memset(&opts, 0, sizeof(opts));
	opts.prefix = prefix;
	opts.head_idx = -1;
	opts.src_index = the_repository->index;
	opts.dst_index = the_repository->index;
	opts.fn = bind_merge;

	if (unpack_trees(1, &tree_desc, &opts))
		return error(_("couldn't unpack tree for commit %s"),
			     oid_to_hex(&commit->object.oid));

	if (write_locked_index(the_repository->index, &lock_file, COMMIT_LOCK))
		die(_("unable to write new index file"));

	return 0;
}

static int checkout_prefix(const char *prefix)
{
	struct child_process cp = CHILD_PROCESS_INIT;

	cp.git_cmd = 1;

	strvec_push(&cp.args, "checkout");
	strvec_push(&cp.args, "--");
	strvec_push(&cp.args, prefix);

	return run_command(&cp);
}

static int add_commit(struct commit *commit, int rejoin, int squash,
		      const char *prefix)
{
	struct object_id tree_oid, curr_head_oid;
	int head_is_parent;

	if (!rejoin) {
		if (read_tree_prefix(commit, prefix))
			return error(
				_("couldn't read tree into index for commit %s"),
				oid_to_hex(&commit->object.oid));
	}

	if (checkout_prefix(prefix))
		return error(_("couldn't checkout working tree at %s"), prefix);

	if (write_index_as_tree(&tree_oid, the_repository->index,
				get_index_file(), 0, NULL))
		return error(_("couldn't write index into new tree"));

	if (repo_get_oid_committish(the_repository, "HEAD", &curr_head_oid))
		return error(_("couldn't get commit associated with HEAD"));

	head_is_parent = oideq(&commit->object.oid, &curr_head_oid);

	return 0;
}

static int fetch_repo_ref(const char *repository, const char *ref)
{
	struct child_process cp = CHILD_PROCESS_INIT;

	cp.git_cmd = 1;

	strvec_push(&cp.args, "fetch");
	strvec_push(&cp.args, repository);
	strvec_push(&cp.args, ref);

	return run_command(&cp);
}

static int add_repository(const char *repository, const char *ref,
			  const char *prefix)
{
	struct object_id oid;
	int fetch_ret;

	fetch_ret = fetch_repo_ref(repository, ref);

	if (fetch_ret)
		return error(_("couldn't fetch ref %s from repository %s"), ref,
			     repository);

	if (refs_read_ref(get_main_ref_store(the_repository), "FETCH_HEAD",
			  &oid))
		return error(_("couldn't read FETCH_HEAD after fetching %s"),
			     repository);

	return add_commit(lookup_commit(the_repository, &oid), 0, 0, prefix);
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

		return add_commit(commit, 0, squash, subtree_prefix);
	} else if (argc == 2) {
		ref = xstrfmt("refs/heads/%s", argv[1]);
		if (check_refname_format(ref, 0)) {
			free(ref);
			die(_("fatal: '%s' does not look like a ref"), argv[1]);
		}
		free(ref);

		return add_repository(argv[0], argv[1], subtree_prefix);
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
