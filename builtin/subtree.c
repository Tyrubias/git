#include "builtin.h"
#include "cache-tree.h"
#include "commit.h"
#include "config.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "lockfile.h"
#include "object-name.h"
#include "object.h"
#include "parse-options.h"
#include "refs.h"
#include "repository.h"
#include "reset.h"
#include "resolve-undo.h"
#include "revision.h"
#include "run-command.h"
#include "strbuf.h"
#include "trailer.h"
#include "tree-walk.h"
#include "tree.h"
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

#define GIT_SUBTREE_DIR_TRAILER "git-subtree-dir"
#define GIT_SUBTREE_SPLIT_TRAILER "git-subtree-split"
#define GIT_SUBTREE_MAIN_TRAILER "git-subtree-mainline"

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

	if (repo_read_index_unmerged(the_repository))
		die(_("You need to resolve your current index first"));

	resolve_undo_clear_index(the_repository->index);

	cache_tree_free(&the_repository->index->cache_tree);

	if (parse_tree(tree))
		return error(_("couldn't parse tree for commit %s"),
			     oid_to_hex(&commit->object.oid));

	init_tree_desc(&tree_desc, &tree->object.oid, tree->buffer, tree->size);

	memset(&opts, 0, sizeof(opts));
	opts.merge = 1;
	opts.prefix = prefix;
	opts.fn = bind_merge;
	opts.head_idx = 1;
	opts.dst_index = the_repository->index;
	opts.src_index = the_repository->index;

	if (unpack_trees(1, &tree_desc, &opts))
		return error(_("couldn't unpack tree for commit %s"),
			     oid_to_hex(&commit->object.oid));

	if (write_locked_index(the_repository->index, &lock_file, COMMIT_LOCK))
		die(_("unable to write new index file"));

	return 0;
}

static int checkout_subtree_dir(const char *subtree_dir)
{
	struct child_process cp = CHILD_PROCESS_INIT;

	cp.git_cmd = 1;

	strvec_push(&cp.args, "checkout");
	strvec_push(&cp.args, "--");
	strvec_push(&cp.args, subtree_dir);

	return run_command(&cp);
}

static int init_squash_message(struct strbuf *msg, struct commit *old_commit,
			       struct commit *new_commit,
			       const char *subtree_dir)
{
	struct rev_info revs;
	struct commit *commit;
	struct pretty_print_context ctx = { 0 };
	LIST_HEAD(raw_trailers);
	LIST_HEAD(trailers);
	struct list_head *pos, *tmp;
	struct new_trailer_item *item;
	struct process_trailer_options opts = PROCESS_TRAILER_OPTIONS_INIT;

	if (old_commit) {
		strbuf_addf(msg, "Squashed '%s/' changes from ", subtree_dir);
		strbuf_add_unique_abbrev(msg, &old_commit->object.oid,
					 DEFAULT_ABBREV);
		strbuf_addf(msg, "..");
		strbuf_add_unique_abbrev(msg, &new_commit->object.oid,
					 DEFAULT_ABBREV);
		strbuf_addchars(msg, '\n', 2);

		repo_init_revisions(the_repository, &revs, NULL);
		add_pending_object(&revs, &old_commit->object, NULL);
		add_pending_object(&revs, &new_commit->object, NULL);

		if (prepare_revision_walk(&revs))
			return error(_(
				"Failed to prepare revision walk while squashing"));

		while ((commit = get_revision(&revs))) {
			repo_format_commit_message(the_repository, commit,
						   "%h %s", msg, &ctx);
		}

		reset_revision_walk();
		release_revisions(&revs);

		repo_init_revisions(the_repository, &revs, NULL);
		add_pending_object(&revs, &old_commit->object, NULL);
		add_pending_object(&revs, &new_commit->object, NULL);
		revs.reverse ^= 1;

		if (prepare_revision_walk(&revs))
			return error(_(
				"Failed to prepare revision walk while squashing"));

		while ((commit = get_revision(&revs))) {
			repo_format_commit_message(the_repository, commit,
						   "REVERT: %h %s", msg, &ctx);
		}

		reset_revision_walk();
		release_revisions(&revs);
	} else {
		strbuf_addf(msg, "Squashed '%s/' content from commit ",
			    subtree_dir);
		strbuf_add_unique_abbrev(msg, &new_commit->object.oid,
					 DEFAULT_ABBREV);
		strbuf_addch(msg, '\n');
	}

	strbuf_addch(msg, '\n');

	trailer_config_init();

	item = xmalloc(sizeof(*item));
	item->text = xstrfmt("%s: %s", GIT_SUBTREE_DIR_TRAILER, subtree_dir);
	item->where = WHERE_END;
	list_add_tail(&item->list, &raw_trailers);

	item = xmalloc(sizeof(*item));
	item->text = xstrfmt("%s: %s", GIT_SUBTREE_SPLIT_TRAILER,
			     oid_to_hex(&new_commit->object.oid));
	item->where = WHERE_END;
	list_add_tail(&item->list, &raw_trailers);

	parse_trailers_from_command_line_args(&trailers, &raw_trailers);

	format_trailers(&opts, &trailers, msg);

	free_trailers(&trailers);

	list_for_each_safe (pos, tmp, &raw_trailers) {
		item = list_entry(pos, struct new_trailer_item, list);
		free((char *)item->text);
		free(item);
	}

	return 0;
}

static int new_squash_commit(struct object_id *new_squashed_commit,
			     struct commit *old_squashed_commit,
			     struct commit *old_commit,
			     struct commit *new_commit, const char *subtree_dir)
{
	struct strbuf commit_message = STRBUF_INIT;
	struct commit_list *parents = NULL;

	if (old_squashed_commit) {
		init_squash_message(&commit_message, old_commit, new_commit,
				    subtree_dir);
		commit_list_insert(old_squashed_commit, &parents);
	} else {
		init_squash_message(&commit_message, NULL, new_commit,
				    subtree_dir);
	}

	return commit_tree(commit_message.buf, commit_message.len,
			   get_commit_tree_oid(new_commit), parents,
			   new_squashed_commit, NULL, NULL);
}

static inline struct strbuf *new_squashed_msg(const struct commit *commit,
					      const char *subtree_dir,
					      const char *commit_msg)
{
	struct strbuf *msg = xmalloc(sizeof(struct strbuf));
	if (commit_msg)
		strbuf_addstr(msg, commit_msg);
	else
		strbuf_addf(msg, "Merge commit '%s' as '%s'",
			    oid_to_hex(&commit->object.oid), subtree_dir);

	return msg;
}

static int add_commit(struct commit *commit, int rejoin, int squash,
		      const char *subtree_dir, const char *commit_message)
{
	struct object_id tree_oid, curr_head_oid, new_squash_oid,
		new_commit_oid;
	struct commit_list *parents = NULL;
	struct strbuf *new_squash_msg;
	struct reset_head_opts reset_opts = { 0 };

	if (!rejoin) {
		if (read_tree_prefix(commit, subtree_dir))
			return error(
				_("couldn't read tree into index for commit %s"),
				oid_to_hex(&commit->object.oid));
	}

	if (checkout_subtree_dir(subtree_dir))
		return error(_("couldn't checkout working tree at %s"),
			     subtree_dir);

	if (write_index_as_tree(&tree_oid, the_repository->index,
				get_index_file(), 0, NULL))
		return error(_("couldn't write index into new tree"));

	if (repo_get_oid_committish(the_repository, "HEAD", &curr_head_oid))
		return error(_("couldn't get commit associated with HEAD"));

	if (!oideq(&commit->object.oid, &curr_head_oid))
		commit_list_insert(lookup_commit(the_repository,
						 &curr_head_oid),
				   &parents);

	if (squash) {
		if (new_squash_commit(&new_squash_oid, NULL, NULL, commit,
				      subtree_dir))
			return error(
				_("couldn't create new squash commit from %s"),
				oid_to_hex(&commit->object.oid));

		new_squash_msg = new_squashed_msg(
			lookup_commit(the_repository, &new_squash_oid),
			subtree_dir, commit_message);

		commit_list_insert(lookup_commit(the_repository,
						 &new_squash_oid),
				   &parents);
	} else {
		new_squash_msg =
			new_squashed_msg(commit, subtree_dir, commit_message);
		commit_list_insert(commit, &parents);
	}

	if (commit_tree(new_squash_msg->buf, new_squash_msg->len, &tree_oid,
			parents, &new_commit_oid, NULL, NULL))
		return error(_("couldn't create new commit"));

	reset_opts.oid = &new_commit_oid;
	reset_opts.head_msg = "reset: checkout subtree commit";

	return reset_head(the_repository, &reset_opts);
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
			  const char *subtree_dir, const char *commit_message,
			  int squash)
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

	return add_commit(lookup_commit_or_die(&oid, "FETCH_HEAD"), 0, squash,
			  subtree_dir, commit_message);
}

static int add(int argc, const char **argv, const char *prefix)
{
	const char *subtree_dir = NULL;
	const char *commit_message = NULL;
	char *ref = NULL;
	struct commit *commit = NULL;
	int squash = 0;
	struct option options[] = {
		OPT_STRING(0, "prefix", &subtree_dir, N_("prefix"),
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
	if (!subtree_dir)
		die(_("parameter '%s' is required"), "--prefix");
	if (path_exists(subtree_dir))
		die(_("prefix '%s' already exists"), subtree_dir);

	require_clean_work_tree(the_repository, N_("subtree add"),
				_("Please commit or stash them."), 0, 0);

	if (argc == 1) {
		commit = lookup_commit_reference_by_name(argv[0]);
		if (!commit)
			die(_("'%s' does not refer to a commit"), argv[0]);

		return add_commit(commit, 0, squash, subtree_dir,
				  commit_message);
	} else if (argc == 2) {
		ref = xstrfmt("refs/heads/%s", argv[1]);
		if (check_refname_format(ref, 0)) {
			free(ref);
			die(_("'%s' does not look like a ref"), argv[1]);
		}
		free(ref);

		return add_repository(argv[0], argv[1], subtree_dir,
				      commit_message, squash);
	} else {
		usage_with_options(git_subtree_add_usage, options);
	}
}

static int process_subtree_split(struct commit *main_commit,
				 const char *split_hash, const char *repository,
				 struct object_id *split_oid)
{
	if (repo_get_oid_commit(the_repository, split_hash, split_oid)) {
		if (repository) {
			if (fetch_repo_ref(repository, split_hash))
				return error(
					_("couldn't fetch ref %s from repository %s"),
					split_hash, repository);
			if (repo_get_oid_commit(the_repository, split_hash,
						split_oid))
				return error(
					_("could not rev-parse split hash %s from commit %s"),
					split_hash,
					oid_to_hex(&main_commit->object.oid));
		} else {
			return error(
				_("could not rev-parse split hash %s from commit %s"),
				split_hash,
				oid_to_hex(&main_commit->object.oid));
		}
	}

	return error(_("could not rev-parse split hash %s from commit %s"),
		     split_hash, oid_to_hex(&main_commit->object.oid));
}

static int find_latest_squash(const char *subtree_dir, const char *repository,
			      struct object_id *commit_oid,
			      struct object_id *split_oid)
{
	struct rev_info revs;
	struct commit *commit;
	struct strbuf msg = STRBUF_INIT, main = STRBUF_INIT,
		      split = STRBUF_INIT;
	struct trailer_iterator iter;
	int success = -1;

	repo_init_revisions(the_repository, &revs, NULL);
	add_head_to_pending(&revs);

	if (prepare_revision_walk(&revs))
		return error(_("Failed to prepare revision walk"));

	while ((commit = get_revision(&revs))) {
		pp_commit_easy(CMIT_FMT_RAW, commit, &msg);
		trailer_iterator_init(&iter, msg.buf);
		while (trailer_iterator_advance(&iter)) {
			if (!strcmp(iter.key.buf, GIT_SUBTREE_DIR_TRAILER) &&
			    strcmp(iter.val.buf, subtree_dir))
				goto continue_outer;
			if (!strcmp(iter.key.buf, GIT_SUBTREE_MAIN_TRAILER))
				strbuf_addbuf(&main, &iter.val);
			else if (!strcmp(iter.key.buf,
					 GIT_SUBTREE_SPLIT_TRAILER))
				strbuf_addbuf(&split, &iter.val);
		}
		if (split.len) {
			if (main.len) {
				if (commit_list_count(commit->parents) > 2)
					*commit_oid =
						commit->parents->next->item
							->object.oid;
				else
					goto finish;
			} else {
				*commit_oid = commit->object.oid;
			}
			if (process_subtree_split(commit, split.buf, repository,
						  split_oid))
				goto finish;

			success = 0;
			break;
		}
	continue_outer:
		trailer_iterator_release(&iter);
		strbuf_reset(&msg);
	}

finish:
	reset_revision_walk();
	release_revisions(&revs);
	strbuf_release(&msg);
	strbuf_release(&main);
	strbuf_release(&split);

	return success;
}

static int do_subtree_merge(const char *subtree_dir, const char *msg,
			    struct commit *commit)
{
	struct child_process cp = CHILD_PROCESS_INIT;

	cp.git_cmd = 1;

	strvec_push(&cp.args, "merge");
	strvec_push(&cp.args, "--no-ff");
	strvec_push(&cp.args, "-X");
	strvec_push(&cp.args, xstrfmt("subtree=%s", subtree_dir));
	if (msg) {
		strvec_push(&cp.args, "-m");
		strvec_push(&cp.args, msg);
	}
	strvec_push(&cp.args, oid_to_hex(&commit->object.oid));

	return run_command(&cp);
}

static int merge(int argc, const char **argv, const char *prefix)
{
	const char *subtree_dir = NULL;
	const char *commit_message = NULL;
	struct commit *commit = NULL;
	struct object_id last_squash_commit_oid, last_subtree_commit_oid,
		new_squash_commit_oid;
	int squash = 0;
	struct option options[] = {
		OPT_STRING(0, "prefix", &subtree_dir, N_("prefix"),
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

	if (argc < 1 || argc > 2)
		die(_("you must provide exactly one revision, and optionally a repository."));

	if (!subtree_dir)
		die(_("parameter '%s' is required"), "--prefix");
	if (!path_exists(subtree_dir))
		die(_("'%s' does not exist; use 'git subtree add'"),
		    subtree_dir);

	require_clean_work_tree(the_repository, N_("subtree add"),
				_("Please commit or stash them."), 0, 0);

	commit = lookup_commit_reference_by_name(argv[0]);

	if (!commit)
		die(_("'%s' does not refer to a commit"), argv[0]);

	if (squash) {
		if (find_latest_squash(subtree_dir, argc == 2 ? argv[1] : NULL,
				       &last_squash_commit_oid,
				       &last_subtree_commit_oid))
			return error(
				_("can't squash-merge: '%s' was never added."),
				subtree_dir);
		if (oideq(&last_subtree_commit_oid, &commit->object.oid)) {
			warning(_("Subtree is already at commit %s"),
				oid_to_hex(&commit->object.oid));
			return 0;
		}
		if (new_squash_commit(&new_squash_commit_oid,
				      lookup_commit(the_repository,
						    &last_squash_commit_oid),
				      lookup_commit(the_repository,
						    &last_subtree_commit_oid),
				      commit, subtree_dir))
			return error(_("couldn't create new squash commit"));
	}

	return do_subtree_merge(subtree_dir, commit_message, commit);
	;
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
