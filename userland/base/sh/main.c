/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Implements the zedBSD sh userland command.
 */

#include "userland/base/sh/alias.h"
#include "userland/base/sh/builtins.h"
#include "userland/base/sh/expand.h"
#include "userland/base/sh/glob.h"
#include "userland/base/sh/lexer.h"
#include "userland/base/sh/parser.h"
#include "userland/base/sh/vars.h"

#include <errno.h>
#include <fcntl.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define SHELL_LINE_MAX 256
#define ARG_MAX 64
#define SOURCE_MAX 8192
#define PIPELINE_MAX 16
#define SHELL_SIGNAL_MAX 32

/* Last completed command, retained across input lines and empty input. */
static int shell_status;

/*
 * What a command has asked the constructs around it to do.
 *
 * break and continue name how many enclosing loops they act on, and return
 * ends a function.  They are carried here rather than in the result of each
 * command, because they have to travel out through every list, pipeline and
 * branch between the command and the loop it named.
 */
static int loop_depth;
static int break_pending;
static int continue_pending;
static int return_pending;
static int function_depth;

/* How many files the shell is reading through a dot command or by name. */
static int source_depth;

/*
 * The options set by the set command.
 *
 * errexit ends the shell where a command fails, but only where the failure
 * is not itself the answer to a question: the condition of an if or a
 * while, the left of an && or an ||, and anything a ! turns round are asked
 * in order to be answered either way.  The count says how deep in such a
 * question the shell is.
 */
static int option_errexit;
static int option_trace;
static int option_unset_error;
static int condition_depth;

/* The status a return asked its function to end with. */
static int return_status;

/* Set when a parse stopped because the input had not finished. */
static int shell_incomplete;

/*
 * The tokens and the tree of one input, with a count of what holds them.
 *
 * A function's body is a part of the tree of the input that defined it, and
 * the leaves of that tree name tokens of the same input, so neither may be
 * released while a definition still points into them.  The count says how
 * many things do.
 */
struct shell_input {
	struct sh_token_list tokens;
	struct sh_node *tree;
	int references;
};

/* A function, named, with the body it was defined with. */
struct shell_function {
	char *name;
	struct sh_node *body;
	struct shell_input *input;
};

#define SHELL_FUNCTION_MAX 64

/* How deeply one function may call another, this one included. */
#define SHELL_CALL_DEPTH_MAX 64
static struct shell_function shell_functions[SHELL_FUNCTION_MAX];
static size_t shell_function_count;

/* Parse failures stop a script even though ordinary command failures do not. */
static int shell_syntax_error;

/* Exact status supplied by an execution helper; -1 means boolean fallback. */
static int execution_status = -1;

static int command_background;
static int command_subshell;
static pid_t last_job;
static pid_t last_job_processes[PIPELINE_MAX];
static int last_job_process_count;
static const char *shell_name = "/bin/sh";
static int shell_positional_count;
static char **shell_positional;

/*
 * The positional parameters when the shell itself owns them.
 *
 * Those the shell was started with belong to its own argument vector, and
 * are not freed; those a set command installs are made here, and the block
 * is released when another set replaces it.  A function keeps its caller's
 * block aside for the length of the call, so a set inside a function does
 * not disturb what the caller will see again.
 */
static char **positional_owned;
static char *trap_action[SHELL_SIGNAL_MAX];
static volatile int trap_pending[SHELL_SIGNAL_MAX];
static int getopts_offset = 1;
static long getopts_last_index = 1;

#define REDIRECT_MAX 16

/*
 * One redirection, as it was written.
 *
 * A redirection either puts a file on a descriptor, or makes one descriptor
 * a second name for another, or closes one.  Which of the three it is is
 * said by the source, because that is what the three forms differ in, while
 * the descriptor they act on is the same question in all of them.
 */
#define REDIRECT_FROM_PATH  (-1)
#define REDIRECT_CLOSE	    (-2)

/*
 * A here-document, whose text stands where a path would.  It is given to
 * the command through a pipe that a process of its own fills, because the
 * text may be longer than a pipe will hold at once and nothing would then
 * read the rest of it.
 */
#define REDIRECT_HEREDOC    (-3)

struct redirection {
	int descriptor;
	int source;
	int flags;
	char *path;
};

/* What a descriptor held before a redirection, so that it can be put back. */
struct redirect_save {
	int descriptor;
	int saved;
};

struct pipeline_command {
	char *argv[ARG_MAX + 1];
	int argc;
	struct redirection redirects[REDIRECT_MAX];
	int redirect_count;
};

static int command(char *text);
static int execute_node(const struct sh_node *node, struct shell_input *input,
			int background);
static int execute_simple(const struct sh_node *node,
			  struct shell_input *input, int background);
static int execute_pipeline_node(const struct sh_node *node,
				 struct shell_input *input);
static void trace_pipeline(struct pipeline_command *items, int count);
static void errexit_check(int result);
static void expand_context_fill(struct sh_expand_context *context);
static char *expand_one_word(struct shell_input *input, size_t index);
static struct shell_function *find_function(const char *name);
static int control_flow_pending(void);
static void positional_free(char **values);
static int positional_replace(int argc, char **argv, int first);
static struct shell_input *input_hold(struct shell_input *input);
static void input_release(struct shell_input *input);
static int redirect_apply(const struct sh_node *node,
			  struct shell_input *input,
			  struct redirect_save *saves, int *save_count);
static int call_function(struct shell_function *function, int argc,
			 char **argv);
static int command_argv_body(int argc, char **argv);
static int wait_status_result(int status);
static int run_pending_traps(void);
static int parse_pipeline(const struct sh_token_list *list, size_t *position, size_t limit, struct pipeline_command *items, int *item_count, enum sh_token_type *following, const struct sh_expand_context *context);
static int assignment_length(const char *text);
static void pipeline_free(struct pipeline_command *items, int count);
static int redirect_read_one(const struct sh_token_list *list, size_t *position, size_t limit, struct redirection *result, const struct sh_expand_context *context);
static int redirections_apply(const struct redirection *items, int count, struct redirect_save *saves, int *save_count);
static void redirections_undo(struct redirect_save *saves, int save_count);
static int heredoc_open(const char *text, int descriptor);
static int execute_pipeline(struct pipeline_command *items, int count, int background);
static int execute_parent_command(struct pipeline_command *item);
static int command_argv(int argc, char **argv);
static int special_builtin_name(const char *name);
static int apply_assignment(char *text);
static int temporary_assignment(char *text, struct sh_var_snapshot *snapshot);
static int command_dispatch(int argc, char **argv);
static int continue_foreground(pid_t pid, int *status);
static int shell_tcsetpgrp(int descriptor, pid_t pgrp);
static int shell_controls_terminal(void);
static void remember_job(pid_t group, const pid_t *processes, int count);
static void forget_job(void);
static int resolve_command(const char *name, char *candidate, size_t capacity);
static int search_path(const char *name, const char *suffix, char *candidate, size_t capacity);
static int path_candidate(const char *path, size_t *position, const char *name, const char *suffix, char *candidate, size_t capacity, int *last);
static int is_executable_file(const char *path);
static int shell_getopts_builtin(int argc, char **argv);
static int set_decimal_variable(const char *name, long value);
static int signal_number(const char *name);
static int set_trap(const char *action, int number);
static int source_file(const char *path);
static int source_file_mode(const char *path, int continue_on_error);
static int join_arguments(int argc, char **argv, int first, char **result);
static int read_line(char **result, int raw);
static int read_assign_fields(char *line, int argc, char **argv, int first);
static int shell_wait_builtin(int argc, char **argv);
static int shell_builtin_name(const char *name);
static int is_elf(const char *path);
static int run_external(char *const argv[]);
static int spawn_wait(char *const argv[]);
static int spawn_foreground_tty(char *const argv[], int *status);
static ssize_t shell_write_nosigpipe(int descriptor, const void *buffer, size_t length);
static void remember_single_job(pid_t process);
static const char *signal_message(int number);
static int wait_foreground(pid_t pid, int *status);
static int run_shell_script(int argc, char **argv, const char *path);
static int run_search_path(int argc, char **argv);
static int run_resolved(int argc, char **argv, const char *path);
static int pipeline_child(struct pipeline_command *item);
static const char *shell_lookup(void *context, const char *name);
static int shell_assign(void *context, const char *name, const char *value);
static void shell_signal_handler(int signal_number);
static int shell_command_substitute(void *context, const char *source, char **result);

/*
 * Runs the sh command.
 */
int
main(
	int argc,
	char **argv)
{
	int function_result;
	char cwd[256];
	char hostname[65];
	char prompt[sizeof(cwd) + sizeof(hostname) + 16U];
	char *pending;
	char *joined;
	char *line;

	/* Handles a failed sh var get operation. */
	if (sh_var_get("PATH") == NULL)
		(void)sh_var_set("PATH", "/bin:/sbin:/usr/bin", 1);

	/* Handles a failed sh var get operation. */
	if (sh_var_get("TERM") == NULL)
		(void)sh_var_set("TERM", "zed", 1);

	/* Handles the selected command-line operation. */
	if (argc >= 3 && strcmp(argv[1], "-c") == 0) {
		shell_name = argc >= 4 ? argv[3] : argv[0];
		shell_positional_count = argc >= 4 ? argc - 4 : 0;
		shell_positional = argc >= 4 ? argv + 4 : NULL;

		/* Computes the function result. */
		(void)command(argv[2]);
		function_result = shell_status;

		/* Returns the computed result. */
		return function_result;
	}

	/* Validates the command-line arguments. */
	if (argc > 1) {
		shell_name = argv[1];
		shell_positional_count = argc - 2;
		shell_positional = argv + 2;

		/* Computes the function result. */
		function_result = source_file_mode(argv[1], 0);
		function_result = function_result ? shell_status :
		    (shell_status != 0 ? shell_status : 1);

		/* Returns the computed result. */
		return function_result;
	}

	/* Validates the command-line arguments. */
	if (argc > 0 && argv[0] != NULL)
		shell_name = argv[0];
	using_history();

	pending = NULL;

	/* Continue until the operation reaches a terminal state. */
	for (;;) {
		/* Handles a failed getcwd operation. */
		if (getcwd(cwd, sizeof(cwd)) == NULL)
			strcpy(cwd, "/");

		/* Handles a failed gethostname operation. */
		if (gethostname(hostname, sizeof(hostname)) != 0)
			strcpy(hostname, "zedbsd");
		(void)snprintf(prompt, sizeof(prompt), "root@%s:%s$ ", hostname,
			       cwd);

		/* A command that is not finished asks for the rest of itself. */
		line = readline(pending != NULL ? "> " : prompt);

		/* Handles the line availability. */
		if (line == NULL) {
			(void)putchar('\n');

			/* Handles input that ended in the middle of a command. */
			if (pending != NULL) {
				fprintf(stderr,
					"sh: unexpected end of input\n");
				free(pending);
				shell_status = 2;
			}

			/* Returns the last command's status at end of input. */
			return shell_status;
		}

		/* Handles the line condition. */
		if (line[0] != '\0')
			add_history(line);

		/*
		 * The lines of one command are joined and offered whole,
		 * because where a construct ends is a question about all of
		 * them together.
		 */
		if (pending == NULL) {
			pending = line;
		} else {
			joined = malloc(strlen(pending) + strlen(line) + 2U);

			/* Handles a failed malloc operation. */
			if (joined == NULL) {
				fprintf(stderr, "sh: out of memory\n");
				free(line);
				free(pending);
				pending = NULL;
				continue;
			}
			(void)sprintf(joined, "%s\n%s", pending, line);
			free(pending);
			free(line);
			pending = joined;
		}
		(void)command(pending);

		/* Handles a command that is still waiting to be finished. */
		if (shell_incomplete)
			continue;
		free(pending);
		pending = NULL;
	}
}

/* Supports the input hold operation. */
static struct shell_input *
input_hold(
	struct shell_input *input)
{
	/* Handles the input availability. */
	if (input != NULL)
		input->references++;

	/* Returns the computed result. */
	return input;
}

/* Supports the input release operation. */
static void
input_release(
	struct shell_input *input)
{
	/* Handles the input availability. */
	if (input == NULL)
		return;
	input->references--;

	/* Something still points into it, so it stays. */
	if (input->references > 0)
		return;
	sh_node_free(input->tree);
	sh_tokens_free(&input->tokens);
	free(input);
}

/*
 * Supports the command operation.
 *
 * The text may hold several lines: a compound command is written across
 * them, and the words that open and close it are reserved only where a
 * command may begin, which is a question about position and so about the
 * whole of the input rather than about one line of it.
 */
static int
command(
	char *text)
{
	struct shell_input *input;
	const char *error_text;
	int incomplete;
	int result;

	shell_syntax_error = 0;
	shell_incomplete = 0;
	input = calloc(1, sizeof(*input));

	/* Handles a failed calloc operation. */
	if (input == NULL) {
		fprintf(stderr, "sh: out of memory\n");
		shell_status = 2;
		execution_status = 2;
		return 0;
	}
	input->references = 1;

	/* Handles an operation failure. */
	if (!sh_lex(text, &input->tokens, &error_text)) {
		/*
		 * A quotation, a substitution or a here-document that has
		 * been opened and not closed is a fault that more input can
		 * mend, and is how each of them is written across lines.
		 */
		if (strcmp(error_text, "unterminated quote") == 0 ||
		    strcmp(error_text, "unterminated here-document") == 0 ||
		    strcmp(error_text,
			   "unterminated command substitution") == 0) {
			free(input);
			shell_incomplete = 1;

			/* Returns the computed result. */
			return shell_status == 0;
		}
		fprintf(stderr, "sh: syntax error: %s\n", error_text);
		free(input);
		shell_status = 2;
		execution_status = 2;
		shell_syntax_error = 1;

		/* Reports successful completion. */
		return 0;
	}

	/* Handles an operation failure. */
	if (!sh_alias_expand(&input->tokens, &error_text)) {
		fprintf(stderr, "sh: alias: %s\n", error_text);
		input_release(input);
		shell_status = 2;
		execution_status = 2;
		shell_syntax_error = 1;

		/* Reports successful completion. */
		return 0;
	}

	/* Handles an operation failure. */
	if (!sh_parse(&input->tokens, &input->tree, &error_text, &incomplete)) {
		/*
		 * A construct that has been opened and not closed is not a
		 * mistake: the caller reads more input and offers the whole
		 * of it again.
		 */
		if (incomplete) {
			input_release(input);
			shell_incomplete = 1;

			/* Returns the computed result. */
			return shell_status == 0;
		}
		fprintf(stderr, "sh: syntax error: %s\n", error_text);
		input_release(input);
		shell_status = 2;
		execution_status = 2;
		shell_syntax_error = 1;

		/* Reports successful completion. */
		return 0;
	}

	/* Handles a failed run pending traps operation. */
	(void)run_pending_traps();

	/* An input of nothing but separators runs nothing and fails at nothing. */
	if (input->tree == NULL)
		result = shell_status == 0;
	else
		result = execute_node(input->tree, input, 0);

	/* Handles a failed run pending traps operation. */
	if (!run_pending_traps())
		result = 0;
	command_background = 0;
	execution_status = shell_status;
	input_release(input);

	/* Returns the computed result. */
	return result;
}

/* Supports the control flow pending operation. */
static int
control_flow_pending(
	void)
{
	/* Returns the computed result. */
	return break_pending != 0 || continue_pending != 0 ||
	       return_pending != 0;
}

/* Supports the expand context fill operation. */
static void
expand_context_fill(
	struct sh_expand_context *context)
{
	context->status = shell_status;
	context->shell_pid = (long)getpid();
	context->last_job = (long)last_job;
	context->lookup = shell_lookup;
	context->assign = shell_assign;
	context->command_substitute = shell_command_substitute;
	context->lookup_context = NULL;
	context->shell_name = shell_name;
	context->positional_count = shell_positional_count;
	context->positional = shell_positional;
	context->unset_is_error = option_unset_error;
}

/*
 * Supports the expand one word operation.
 *
 * Expands a single token into one word, for the places the grammar takes a
 * word rather than a command: the name a for loop sets, the value a case
 * matches, and the file a redirection names.
 */
static char *
expand_one_word(
	struct shell_input *input,
	size_t index)
{
	struct sh_expand_context context;
	const char *error_text;
	char *word;

	expand_context_fill(&context);

	/* Handles a failed expansion. */
	if (!sh_expand_word(&input->tokens.tokens[index], &context, &word,
			    &error_text)) {
		fprintf(stderr, "sh: expansion: %s\n", error_text);

		/* Reports that no result is available. */
		return NULL;
	}

	/* Returns the computed result. */
	return word;
}

/* Supports the positional free operation. */
static void
positional_free(
	char **values)
{
	size_t index;

	/* Handles the values availability. */
	if (values == NULL)
		return;

	/* Process each remaining element. */
	for (index = 0; values[index] != NULL; index++)
		free(values[index]);
	free(values);
}

/*
 * Supports the positional replace operation.
 *
 * Copies the words, because the vector they were read from is the one the
 * command was built in and is released as soon as the command ends.
 */
static int
positional_replace(
	int argc,
	char **argv,
	int first)
{
	char **values;
	int count;
	int index;

	count = argc - first;
	values = calloc((size_t)count + 1U, sizeof(*values));

	/* Handles a failed calloc operation. */
	if (values == NULL)
		return 0;

	/* Process each remaining element. */
	for (index = 0; index < count; index++) {
		values[index] = strdup(argv[first + index]);

		/* Handles a failed strdup operation. */
		if (values[index] == NULL) {
			positional_free(values);

			/* Reports successful completion. */
			return 0;
		}
	}
	positional_free(positional_owned);
	positional_owned = values;
	shell_positional = values;
	shell_positional_count = count;

	/* Reports operation failure. */
	return 1;
}

/* Supports the find function operation. */
static struct shell_function *
find_function(
	const char *name)
{
	size_t index;

	/* Process each remaining element. */
	for (index = 0; index < shell_function_count; index++)
		if (strcmp(shell_functions[index].name, name) == 0)
			return &shell_functions[index];

	/* Reports that no result is available. */
	return NULL;
}

/*
 * Supports the call function operation.
 *
 * The body sees the arguments as its positional parameters, and nothing
 * else about the shell changes: a function runs in the shell that called
 * it, so what it assigns and where it moves to are kept.
 */
static int
call_function(
	struct shell_function *function,
	int argc,
	char **argv)
{
	int saved_count;
	char **saved_positional;
	char **saved_owned;
	int saved_break;
	int saved_continue;
	int saved_depth;
	int result;

	/*
	 * A function that calls itself without end would take the shell down
	 * with it, and the stack is what would run out first, so the depth
	 * is limited to something no ordinary script reaches.
	 */
	if (function_depth >= SHELL_CALL_DEPTH_MAX) {
		fprintf(stderr, "sh: %s: too deeply nested\n", function->name);
		shell_status = 1;
		execution_status = 1;

		/* Reports successful completion. */
		return 0;
	}
	saved_count = shell_positional_count;
	saved_positional = shell_positional;
	saved_owned = positional_owned;
	positional_owned = NULL;
	saved_break = break_pending;
	saved_continue = continue_pending;
	saved_depth = loop_depth;
	shell_positional_count = argc - 1;
	shell_positional = argc > 1 ? argv + 1 : NULL;

	/*
	 * A break written in a function acts on a loop in the function, so
	 * the loops the caller is in are hidden for the length of the call.
	 */
	break_pending = 0;
	continue_pending = 0;
	loop_depth = 0;
	function_depth++;
	result = execute_node(function->body, function->input, 0);
	function_depth--;

	/* A return ends the call and says what it ended with. */
	if (return_pending) {
		return_pending = 0;
		shell_status = return_status;
		execution_status = return_status;
		result = return_status == 0;
	}
	loop_depth = saved_depth;
	break_pending = saved_break;
	continue_pending = saved_continue;

	/* Anything a set inside the function made is the function's alone. */
	positional_free(positional_owned);
	positional_owned = saved_owned;
	shell_positional_count = saved_count;
	shell_positional = saved_positional;

	/* Returns the computed result. */
	return result;
}

/*
 * Supports the redirect apply operation.
 *
 * Applies the redirections written after a compound command, which stand
 * for the whole of it.  What each descriptor held is kept so that it can be
 * put back when the command ends, because the command runs in this shell.
 */
static int
redirect_apply(
	const struct sh_node *node,
	struct shell_input *input,
	struct redirect_save *saves,
	int *save_count)
{
	struct redirection items[REDIRECT_MAX];
	struct sh_expand_context context;
	size_t position;
	int count;
	int index;
	int result;

	*save_count = 0;
	count = 0;

	/* An empty range is one that starts after it ends. */
	if (node->redirect_first > node->redirect_last)
		return 1;
	expand_context_fill(&context);
	position = node->redirect_first;
	result = 1;

	/* Continue while the operation condition remains true. */
	while (position <= node->redirect_last) {
		/* Checks the remaining item count. */
		if (count == REDIRECT_MAX) {
			fprintf(stderr, "sh: too many redirections\n");
			result = 0;
			break;
		}

		/* Handles a failed redirect read one operation. */
		if (!redirect_read_one(&input->tokens, &position,
				       node->redirect_last + 1U, &items[count],
				       &context)) {
			result = 0;
			break;
		}
		count++;
	}

	/* Handles a failed redirections apply operation. */
	if (result)
		result = redirections_apply(items, count, saves, save_count);

	/* Process each remaining element. */
	for (index = 0; index < count; index++)
		free(items[index].path);

	/* Returns the computed result. */
	return result;
}

/*
 * Supports the execute simple operation.
 *
 * The words are expanded here rather than when the command was read,
 * because the body of a loop is written once and run many times and has to
 * see what is true on each turn.
 */
static int
execute_simple(
	const struct sh_node *node,
	struct shell_input *input,
	int background)
{
	struct pipeline_command items[PIPELINE_MAX];
	struct sh_expand_context context;
	enum sh_token_type following;
	size_t index;
	int item_count;
	int result;

	expand_context_fill(&context);
	index = node->u.simple.first;

	/* Handles a failed parse pipeline operation. */
	if (!parse_pipeline(&input->tokens, &index, node->u.simple.last + 1U,
			    items, &item_count, &following, &context)) {
		shell_status = 2;
		shell_syntax_error = 1;

		/* Reports successful completion. */
		return 0;
	}

	/* A traced command is shown as it will be run, after expansion. */
	if (option_trace)
		trace_pipeline(items, item_count);
	execution_status = -1;
	result = execute_pipeline(items, item_count, background);
	shell_status = execution_status >= 0 ? execution_status :
		       (result ? 0 : 1);
	pipeline_free(items, item_count);

	/* Returns the computed result. */
	return shell_status == 0;
}

/*
 * Supports the trace pipeline operation.
 *
 * Writes the words of a command as they will be run.  They go to the error
 * output because they are about the shell rather than from the command, and
 * what the command writes must stay usable.
 */
static void
trace_pipeline(
	struct pipeline_command *items,
	int count)
{
	int index;
	int argument;

	/* Process each remaining element. */
	for (index = 0; index < count; index++) {
		fputs(index == 0 ? "+ " : "| ", stderr);

		/* Process each remaining command-line operand. */
		for (argument = 0; argument < items[index].argc; argument++)
			fprintf(stderr, argument == 0 ? "%s" : " %s",
				items[index].argv[argument]);
		fputc('\n', stderr);
	}
	(void)fflush(stderr);
}

/*
 * Supports the errexit check operation.
 *
 * Ends the shell where a command has failed and the failure was not asked
 * for.  A command whose answer a construct is waiting on is exempt, and so
 * is anything run while a function or a file is being read for its own
 * status, because those places are where a failure is handled.
 */
static void
errexit_check(
	int result)
{
	/* Handles a shell that was not asked to end at a failure. */
	if (!option_errexit || result || condition_depth != 0)
		return;

	/* Handles a failure that is not one, because nothing was run. */
	if (shell_status == 0)
		return;
	exit(shell_status);
}

/*
 * Supports the execute pipeline node operation.
 *
 * Runs a pipeline that holds a compound command.  Each command is a child
 * of its own, because what a pipeline joins is the output of one command to
 * the input of the next and a compound command has no other way to be given
 * either.
 */
static int
execute_pipeline_node(
	const struct sh_node *node,
	struct shell_input *input)
{
	pid_t children[PIPELINE_MAX];
	int descriptors[2];
	int previous;
	size_t index;
	size_t started;
	int status;

	/* Checks the remaining item count. */
	if (node->u.pipeline.count > PIPELINE_MAX) {
		fprintf(stderr, "sh: pipeline is too long\n");
		shell_status = 2;

		/* Reports successful completion. */
		return 0;
	}
	previous = -1;
	started = 0;

	/* Process each remaining element. */
	for (index = 0; index < node->u.pipeline.count; index++) {
		int last = index + 1U == node->u.pipeline.count;

		descriptors[0] = -1;
		descriptors[1] = -1;

		/* Handles a failed pipe operation. */
		if (!last && pipe(descriptors) != 0) {
			fprintf(stderr, "sh: cannot pipe: %s\n",
				strerror(errno));
			break;
		}
		children[index] = fork();

		/* Handles a failed fork operation. */
		if (children[index] < 0) {
			fprintf(stderr, "sh: cannot fork: %s\n",
				strerror(errno));
			if (descriptors[0] >= 0)
				(void)close(descriptors[0]);
			if (descriptors[1] >= 0)
				(void)close(descriptors[1]);
			break;
		}

		/* The child takes its end of each pipe and runs the command. */
		if (children[index] == 0) {
			if (previous >= 0) {
				(void)dup2(previous, 0);
				(void)close(previous);
			}
			if (!last) {
				(void)close(descriptors[0]);
				(void)dup2(descriptors[1], 1);
				(void)close(descriptors[1]);
			}
			(void)execute_node(node->u.pipeline.commands[index],
					   input, 0);
			_exit(shell_status);
		}
		started++;
		if (previous >= 0)
			(void)close(previous);
		if (!last) {
			(void)close(descriptors[1]);
			previous = descriptors[0];
		}
	}
	if (previous >= 0)
		(void)close(previous);

	/* Handles a pipeline that could not be started at all. */
	if (started == 0) {
		shell_status = 1;
		return 0;
	}
	status = 0;

	/* The status of the pipeline is the status of its last command. */
	for (index = 0; index < started; index++) {
		int child_status = 0;

		while (waitpid(children[index], &child_status, 0) < 0 &&
		       errno == EINTR)
			continue;
		if (index + 1U == started)
			status = child_status;
	}
	execution_status = -1;
	(void)wait_status_result(status);
	shell_status = execution_status;

	/* Returns the computed result. */
	return shell_status == 0;
}

/*
 * Supports the execute node operation.
 *
 * Walks the tree the parser built.  A command that has asked to leave a
 * loop or to return from a function stops the walk where it is: every
 * construct between it and the one it named simply gives up its turn.
 */
static int
execute_node(
	const struct sh_node *node,
	struct shell_input *input,
	int background)
{
	struct shell_function *function;
	const struct sh_case_arm *arm;
	struct sh_field_list fields;
	struct sh_expand_context context;
	const char *error_text;
	char **values;
	size_t value_count;
	size_t value_index;
	char *word;
	size_t index;
	size_t position;
	struct redirect_save saves[REDIRECT_MAX];
	int save_count;
	int redirected;
	int asked;
	int result;
	int matched;

	/* Handles the node availability. */
	if (node == NULL)
		return 1;
	result = 1;
	redirected = 0;
	asked = 0;

	/* A command a ! turns round is asked for its answer, either way. */
	condition_depth += node->negated;

	save_count = 0;

	/* Redirections written after a compound command stand for all of it. */
	if (node->redirect_first <= node->redirect_last) {
		/* Handles a failed redirect apply operation. */
		if (!redirect_apply(node, input, saves, &save_count)) {
			redirections_undo(saves, save_count);
			shell_status = 1;

			/* Reports successful completion. */
			return 0;
		}
		redirected = 1;
	}

	/* Dispatch the selected kind of command. */
	switch (node->kind) {
	case SH_NODE_SIMPLE:
		result = execute_simple(node, input, background);
		break;

	case SH_NODE_LIST:
		/* Process each remaining element. */
		for (index = 0; index < node->u.list.count; index++) {
			const struct sh_list_entry *entry =
			    &node->u.list.entries[index];
			int run;

			/* A command joined by && or || may be passed over. */
			if (index == 0)
				run = 1;
			else if (entry->join == SH_JOIN_AND)
				run = result;
			else if (entry->join == SH_JOIN_OR)
				run = !result;
			else
				run = 1;

			/* Handles a command that is not to be run. */
			if (!run)
				continue;

			/*
			 * A command with an && or an || after it is asked
			 * for its answer, so its failure is not one the
			 * shell should end at.
			 */
			asked = index + 1U < node->u.list.count &&
			    (node->u.list.entries[index + 1U].join ==
			     SH_JOIN_AND ||
			     node->u.list.entries[index + 1U].join ==
			     SH_JOIN_OR);
			condition_depth += asked;
			result = execute_node(entry->node, input,
					      entry->background);
			condition_depth -= asked;

			/* Stops where a command asked to leave. */
			if (control_flow_pending())
				goto done;

			/*
			 * A command whose answer the next one is waiting on
			 * was asked for that answer, so its failure is not
			 * one the shell should end at.
			 */
			if (!asked)
				errexit_check(result);
		}
		break;

	case SH_NODE_PIPELINE:
		result = execute_pipeline_node(node, input);
		break;

	case SH_NODE_IF:
		matched = 0;

		/* Process each remaining element. */
		for (index = 0; index < node->u.branch.count; index++) {
			condition_depth++;
			result = execute_node(node->u.branch.conditions[index],
					      input, 0);
			condition_depth--;

			/* Stops where a command asked to leave. */
			if (control_flow_pending())
				goto done;

			/* Takes the first branch whose condition held. */
			if (result) {
				result = execute_node(
				    node->u.branch.bodies[index], input, 0);
				matched = 1;
				break;
			}
		}

		/* Nothing held, so the else is taken when there is one. */
		if (!matched) {
			if (node->u.branch.otherwise != NULL) {
				result = execute_node(node->u.branch.otherwise,
						      input, 0);
			} else {
				/* An if that chose nothing has succeeded. */
				shell_status = 0;
				result = 1;
			}
		}
		break;

	case SH_NODE_WHILE:
	case SH_NODE_UNTIL:
		shell_status = 0;
		result = 1;
		loop_depth++;

		/* Continue until the operation reaches a terminal state. */
		for (;;) {
			int holds;

			condition_depth++;
			holds = execute_node(node->u.loop.condition, input, 0);
			condition_depth--;

			/* Stops where a command asked to leave. */
			if (control_flow_pending())
				break;

			/* An until loop runs while its condition does not hold. */
			if (node->kind == SH_NODE_UNTIL)
				holds = !holds;

			/* A loop that ran to its end has succeeded. */
			if (!holds) {
				shell_status = 0;
				result = 1;
				break;
			}
			result = execute_node(node->u.loop.body, input, 0);

			/* A break or a continue says how far out it acts. */
			if (continue_pending != 0) {
				continue_pending--;
				if (continue_pending != 0)
					break;
				continue;
			}
			if (break_pending != 0) {
				break_pending--;
				break;
			}
			if (return_pending != 0)
				break;
		}
		loop_depth--;
		break;

	case SH_NODE_FOR:
		word = expand_one_word(input, node->u.iterate.name);

		/* Handles a failed expansion of the name. */
		if (word == NULL) {
			shell_status = 2;
			result = 0;
			break;
		}
		values = NULL;
		value_count = 0;

		/*
		 * A for with no "in" walks the positional parameters, and
		 * one with words walks what those words expand to, which
		 * may be more words than were written.
		 */
		if (node->u.iterate.words_absent) {
			values = calloc((size_t)shell_positional_count + 1U,
					sizeof(*values));
			if (values != NULL) {
				for (index = 0;
				     index < (size_t)shell_positional_count;
				     index++)
					values[value_count++] =
					    strdup(shell_positional[index]);
			}
		} else {
			expand_context_fill(&context);

			/* Process each remaining element. */
			for (index = 0; index < node->u.iterate.word_count;
			     index++) {
				char **grown;

				memset(&fields, 0, sizeof(fields));
				if (!sh_expand_fields(&input->tokens.tokens[
				    node->u.iterate.words[index]], &context,
				    &fields, &error_text)) {
					fprintf(stderr, "sh: expansion: %s\n",
						error_text);
					result = 0;
					break;
				}
				if (!sh_glob_fields(&fields, &error_text)) {
					fprintf(stderr,
						"sh: pathname expansion: %s\n",
						error_text);
					sh_fields_free(&fields);
					result = 0;
					break;
				}
				grown = realloc(values, (value_count +
				    fields.count + 1U) * sizeof(*values));
				if (grown == NULL) {
					sh_fields_free(&fields);
					result = 0;
					break;
				}
				values = grown;
				for (position = 0; position < fields.count;
				     position++) {
					values[value_count++] =
					    fields.fields[position];
					fields.fields[position] = NULL;
				}
				sh_fields_free(&fields);
			}
		}

		/* Handles a failed expansion of the words. */
		if (!result) {
			shell_status = 2;
		} else {
			shell_status = 0;
			loop_depth++;

			/* Process each remaining element. */
			for (value_index = 0; value_index < value_count;
			     value_index++) {
				if (values[value_index] == NULL ||
				    sh_var_set(word, values[value_index],
					       -1) != 0) {
					fprintf(stderr, "sh: %s: %s\n", word,
						strerror(errno));
					shell_status = 1;
					result = 0;
					break;
				}
				result = execute_node(node->u.iterate.body,
						      input, 0);

				/* A break or a continue says how far out it acts. */
				if (continue_pending != 0) {
					continue_pending--;
					if (continue_pending != 0)
						break;
					continue;
				}
				if (break_pending != 0) {
					break_pending--;
					break;
				}
				if (return_pending != 0)
					break;
			}
			loop_depth--;
		}

		/* Process each remaining element. */
		for (value_index = 0; value_index < value_count; value_index++)
			free(values[value_index]);
		free(values);
		free(word);
		break;

	case SH_NODE_CASE:
		word = expand_one_word(input, node->u.select.word);

		/* Handles a failed expansion of the word to match. */
		if (word == NULL) {
			shell_status = 2;
			result = 0;
			break;
		}
		shell_status = 0;
		result = 1;
		expand_context_fill(&context);

		/* Process each remaining element. */
		for (index = 0; index < node->u.select.arm_count; index++) {
			arm = &node->u.select.arms[index];
			matched = 0;

			/* Process each remaining element. */
			for (position = 0;
			     position < arm->pattern_count && !matched;
			     position++) {
				memset(&fields, 0, sizeof(fields));

				/*
				 * The pattern keeps what of it was quoted,
				 * because a quoted star stands for itself.
				 */
				if (!sh_expand_fields(&input->tokens.tokens[
				    arm->pattern_first[position]], &context,
				    &fields, &error_text)) {
					fprintf(stderr, "sh: expansion: %s\n",
						error_text);
					continue;
				}
				if (fields.count != 0)
					matched = sh_glob_match(
					    fields.fields[0], fields.quoted[0],
					    word);
				sh_fields_free(&fields);
			}

			/* The first arm that matched is the only one taken. */
			if (matched) {
				result = execute_node(arm->body, input, 0);
				break;
			}
		}
		free(word);
		break;

	case SH_NODE_GROUP:
		result = execute_node(node->u.body, input, background);
		break;

	case SH_NODE_SUBSHELL:
		{
			pid_t child;
			int status;

			child = fork();

			/* Handles a failed fork operation. */
			if (child < 0) {
				fprintf(stderr, "sh: cannot fork: %s\n",
					strerror(errno));
				shell_status = 1;
				result = 0;
				break;
			}

			/* The child runs the commands and takes their status. */
			if (child == 0) {
				(void)execute_node(node->u.body, input, 0);
				_exit(shell_status);
			}
			status = 0;
			while (waitpid(child, &status, 0) < 0 &&
			       errno == EINTR)
				continue;
			execution_status = -1;
			(void)wait_status_result(status);
			shell_status = execution_status;
			result = shell_status == 0;
		}
		break;

	case SH_NODE_FUNCTION:
		word = expand_one_word(input, node->u.function.name);

		/* Handles a failed expansion of the name. */
		if (word == NULL) {
			shell_status = 2;
			result = 0;
			break;
		}
		function = find_function(word);

		/* Handles a table with no room for another. */
		if (function == NULL &&
		    shell_function_count == SHELL_FUNCTION_MAX) {
			fprintf(stderr, "sh: too many functions\n");
			free(word);
			shell_status = 1;
			result = 0;
			break;
		}

		/* A definition replaces the one the name had before. */
		if (function == NULL) {
			function = &shell_functions[shell_function_count];
			memset(function, 0, sizeof(*function));
			function->name = word;
			shell_function_count++;
		} else {
			free(word);
			input_release(function->input);
		}
		function->body = node->u.function.body;
		function->input = input_hold(input);
		shell_status = 0;
		result = 1;
		break;

	default:
		shell_status = 2;
		result = 0;
		break;
	}
done:

	condition_depth -= node->negated;

	/* A leading exclamation turns the answer round, but not the flow. */
	if (node->negated && !control_flow_pending()) {
		shell_status = shell_status == 0 ? 1 : 0;
		result = shell_status == 0;
	}

	/* Handles the redirected condition. */
	if (redirected)
		redirections_undo(saves, save_count);

	/* Returns the computed result. */
	return result;
}

/* Supports the run pending traps operation. */
static int
run_pending_traps(
	void)
{
	char *action;
	int number;
	int result;
	int saved_status;
	int saved_execution;

	/* Process each element required by the operation. */
	result = 1;
	saved_status = shell_status;
	saved_execution = execution_status;
	for (number = 1; number < SHELL_SIGNAL_MAX; number++) {
		/* Handles the trap pending condition. */
		if (!trap_pending[number] || trap_action[number] == NULL)
			continue;
		trap_pending[number] = 0;
		action = malloc(strlen(trap_action[number]) + 1U);

		/* Handles the action availability. */
		if (action == NULL)
			return 0;
		strcpy(action, trap_action[number]);

		/* Handles a failed command operation. */
		(void)command(action);
		shell_status = saved_status;
		execution_status = saved_execution;
		free(action);
	}

	/* Returns the computed result. */
	return result;
}

/*
 * Supports the parse pipeline operation.
 *
 * Reads the words of one pipeline and the redirections written with them.
 * The limit is one past the last token that belongs to it, because the
 * caller hands over a part of a larger input: a pipe beyond that limit
 * joins something else and is not this pipeline's to read.
 */
static int
parse_pipeline(
	const struct sh_token_list *list,
	size_t *position,
	size_t limit,
	struct pipeline_command *items,
	int *item_count,
	enum sh_token_type *following,
	const struct sh_expand_context *context)
{
	int assignment;
	struct sh_field_list fields_local;
	char *word;
	size_t field;
	enum sh_token_type type;
	int count;
	struct pipeline_command *item;
	const char *error_text;

	count = 1;
	item = &items[0];
	memset(items, 0, PIPELINE_MAX * sizeof(*items));

	/* Continue until the operation reaches a terminal state. */
	for (;;) {
		/* Anything past the limit belongs to another command. */
		type = *position < limit ? list->tokens[*position].type :
		       SH_TOKEN_END;

		/* Handles the type condition. */
		if (type == SH_TOKEN_WORD) {
			assignment = assignment_length(
		     list->tokens[*position].text) >= 0;

			/* Handles the assignment condition. */
			if (assignment) {
				/* Handles an operation failure. */
				if (!sh_expand_word(&list->tokens[*position],
						    context, &word,
						    &error_text)) {
					fprintf(stderr, "sh: expansion: %s\n",
						error_text);
					pipeline_free(items, count);

					/* Reports successful completion. */
					return 0;
				}
				memset(&fields_local, 0, sizeof(fields_local));
				fields_local.fields = malloc(sizeof(*fields_local.fields));
				fields_local.quoted =
				    calloc(1, sizeof(*fields_local.quoted));

				/* Handles the fields availability. */
				if (fields_local.fields == NULL ||
				    fields_local.quoted == NULL) {
					free(word);
					free(fields_local.fields);
					free(fields_local.quoted);
					pipeline_free(items, count);

					/* Reports successful completion. */
					return 0;
				}
				fields_local.fields[0] = word;
				fields_local.quoted[0] = NULL;
				fields_local.count = 1;
			} else if (!sh_expand_fields(&list->tokens[*position],
						     context, &fields_local,
						     &error_text)) {
				fprintf(stderr, "sh: expansion: %s\n",
					error_text);
				pipeline_free(items, count);

				/* Reports successful completion. */
				return 0;
			}

			/* Handles an operation failure. */
			if (!assignment &&
			    !sh_glob_fields(&fields_local, &error_text)) {
				fprintf(stderr, "sh: pathname expansion: %s\n",
					error_text);
				sh_fields_free(&fields_local);
				pipeline_free(items, count);

				/* Reports successful completion. */
				return 0;
			}

			/* Validates the command-line arguments. */
			if (fields_local.count > (size_t)(ARG_MAX - item->argc)) {
				fprintf(stderr, "sh: too many arguments\n");
				sh_fields_free(&fields_local);
				pipeline_free(items, count);

				/* Reports successful completion. */
				return 0;
			}

			/* Process each remaining element. */
			for (field = 0; field < fields_local.count; field++) {
				item->argv[item->argc++] = fields_local.fields[field];
				free(fields_local.quoted[field]);
			}
			free(fields_local.fields);
			free(fields_local.quoted);
			(*position)++;
			continue;
		}

		/* Handles the type condition. */
		if (type == SH_TOKEN_INPUT || type == SH_TOKEN_OUTPUT ||
		    type == SH_TOKEN_APPEND || type == SH_TOKEN_CLOBBER ||
		    type == SH_TOKEN_LESSAND || type == SH_TOKEN_GREATAND ||
		    type == SH_TOKEN_LESSGREAT || type == SH_TOKEN_DLESS ||
		    type == SH_TOKEN_DLESSDASH) {
			/* Checks the remaining item count. */
			if (item->redirect_count == REDIRECT_MAX) {
				fprintf(stderr, "sh: too many redirections\n");
				pipeline_free(items, count);

				/* Reports successful completion. */
				return 0;
			}

			/* Handles a failed redirect read one operation. */
			if (!redirect_read_one(list, position, limit,
					       &item->redirects[
						   item->redirect_count],
					       context)) {
				pipeline_free(items, count);

				/* Reports successful completion. */
				return 0;
			}
			item->redirect_count++;
			continue;
		}

		/* Validates the command-line arguments. */
		if (item->argc == 0) {
			fprintf(stderr, "sh: empty pipeline command\n");
			pipeline_free(items, count);

			/* Reports successful completion. */
			return 0;
		}
		item->argv[item->argc] = NULL;

		/* Handles the type condition. */
		if (type != SH_TOKEN_PIPE) {
			*following = type;
			*item_count = count;
			/* Reports operation failure. */
			return 1;
		}

		/* Checks the remaining item count. */
		if (count == PIPELINE_MAX) {
			fprintf(stderr, "sh: pipeline is too long\n");
			pipeline_free(items, count);

			/* Reports successful completion. */
			return 0;
		}
		(*position)++;
		item = &items[count++];
	}
}

/* Supports the assignment length operation. */
static int
assignment_length(
	const char *text)
{
	const char *cursor;

	cursor = text;

	/* Checks the current cursor position. */
	if (!((*cursor >= 'A' && *cursor <= 'Z') ||
	      (*cursor >= 'a' && *cursor <= 'z') || *cursor == '_'))

		/* Reports operation failure. */
		return -1;
	cursor++;

	/* Continue while the operation condition remains true. */
	while ((*cursor >= 'A' && *cursor <= 'Z') ||
	       (*cursor >= 'a' && *cursor <= 'z') || *cursor == '_' ||
	       (*cursor >= '0' && *cursor <= '9'))
		cursor++;

	/* Returns the computed result. */
	return *cursor == '=' ? (int)(cursor - text) : -1;
}

/*
 * Supports the redirect read one operation.
 *
 * Reads one redirection and the word that follows it.  The descriptor it
 * acts on is the one written before the operator, or the one the operator
 * implies: nought for a redirection that reads and one for a redirection
 * that writes.
 */
static int
redirect_read_one(
	const struct sh_token_list *list,
	size_t *position,
	size_t limit,
	struct redirection *result,
	const struct sh_expand_context *context)
{
	struct sh_field_list fields;
	enum sh_token_type type;
	const char *error_text;
	const char *word;
	char *end;
	long value;

	type = list->tokens[*position].type;
	memset(result, 0, sizeof(*result));
	result->source = REDIRECT_FROM_PATH;

	/*
	 * A here-document carries its own text, which the lexer took from
	 * the lines after the one it was written on.  What follows it in the
	 * token list is the word that named the delimiter, and that word
	 * names nothing to open, so it is stepped over unexpanded.
	 */
	if (type == SH_TOKEN_DLESS || type == SH_TOKEN_DLESSDASH) {
		struct sh_token body;
		char *text;

		result->descriptor = list->tokens[*position].io_number >= 0 ?
				     list->tokens[*position].io_number : 0;
		result->source = REDIRECT_HEREDOC;
		body = list->tokens[*position];
		body.type = SH_TOKEN_WORD;

		/* Handles a body that holds nothing at all. */
		if (body.text == NULL) {
			result->path = strdup("");

			/* Handles a failed strdup operation. */
			if (result->path == NULL)
				return 0;
		} else if (!sh_expand_word(&body, context, &text,
					   &error_text)) {
			fprintf(stderr, "sh: expansion: %s\n", error_text);

			/* Reports successful completion. */
			return 0;
		} else {
			result->path = text;
		}
		(*position)++;

		/* The delimiter is a word of the redirection, not a file. */
		if (*position < limit &&
		    list->tokens[*position].type == SH_TOKEN_WORD)
			(*position)++;

		/* Reports operation failure. */
		return 1;
	}

	/* Dispatch the selected kind of redirection. */
	switch (type) {
	case SH_TOKEN_INPUT:
		result->descriptor = 0;
		result->flags = O_RDONLY;
		break;
	case SH_TOKEN_OUTPUT:
	case SH_TOKEN_CLOBBER:
		result->descriptor = 1;
		result->flags = O_WRONLY | O_CREAT | O_TRUNC;
		break;
	case SH_TOKEN_APPEND:
		result->descriptor = 1;
		result->flags = O_WRONLY | O_CREAT | O_APPEND;
		break;
	case SH_TOKEN_LESSGREAT:
		result->descriptor = 0;
		result->flags = O_RDWR | O_CREAT;
		break;
	case SH_TOKEN_LESSAND:
		result->descriptor = 0;
		break;
	default:
		result->descriptor = 1;
		break;
	}

	/* A number written before the operator says which descriptor it is. */
	if (list->tokens[*position].io_number >= 0)
		result->descriptor = list->tokens[*position].io_number;
	(*position)++;

	/* Handles a redirection with nothing after it. */
	if (*position >= limit ||
	    list->tokens[*position].type != SH_TOKEN_WORD) {
		fprintf(stderr, "sh: redirection requires a path\n");

		/* Reports successful completion. */
		return 0;
	}
	memset(&fields, 0, sizeof(fields));

	/* Handles an operation failure. */
	if (!sh_expand_fields(&list->tokens[*position], context, &fields,
			      &error_text)) {
		fprintf(stderr, "sh: expansion: %s\n", error_text);

		/* Reports successful completion. */
		return 0;
	}

	/* Handles an operation failure. */
	if (!sh_glob_fields(&fields, &error_text)) {
		fprintf(stderr, "sh: pathname expansion: %s\n", error_text);
		sh_fields_free(&fields);

		/* Reports successful completion. */
		return 0;
	}
	(*position)++;

	/* Handles a word that named more files than one, or none. */
	if (fields.count != 1) {
		fprintf(stderr, "sh: ambiguous redirection\n");
		sh_fields_free(&fields);

		/* Reports successful completion. */
		return 0;
	}

	/*
	 * After an ampersand the word is not a file but another descriptor,
	 * or a dash, which asks for the descriptor to be closed.
	 */
	if (type == SH_TOKEN_LESSAND || type == SH_TOKEN_GREATAND) {
		word = fields.fields[0];

		/* Handles the dash, which closes the descriptor. */
		if (strcmp(word, "-") == 0) {
			result->source = REDIRECT_CLOSE;
			sh_fields_free(&fields);

			/* Reports operation failure. */
			return 1;
		}
		value = strtol(word, &end, 10);

		/* Handles a word that does not name a descriptor. */
		if (*word == '\0' || *end != '\0' || value < 0 ||
		    value > 1024) {
			fprintf(stderr, "sh: %s: not a descriptor\n", word);
			sh_fields_free(&fields);

			/* Reports successful completion. */
			return 0;
		}
		result->source = (int)value;
		sh_fields_free(&fields);

		/* Reports operation failure. */
		return 1;
	}
	result->path = fields.fields[0];
	fields.fields[0] = NULL;
	sh_fields_free(&fields);

	/* Reports operation failure. */
	return 1;
}

/*
 * Supports the heredoc open operation.
 *
 * Puts the text of a here-document on the descriptor.  A process of its own
 * writes it, so that a text longer than a pipe will hold does not stop the
 * shell: the reader is the command, which has not been started yet.
 */
static int
heredoc_open(
	const char *text,
	int descriptor)
{
	int descriptors[2];
	size_t length;
	pid_t child;

	/* Handles a failed pipe operation. */
	if (pipe(descriptors) != 0) {
		fprintf(stderr, "sh: cannot pipe: %s\n", strerror(errno));

		/* Reports successful completion. */
		return 0;
	}
	length = strlen(text);

	/*
	 * A pipe holds at least _POSIX_PIPE_BUF bytes, so a body that short
	 * can be written straight away with nobody reading: the command that
	 * will read it has not been started yet, and a process to write it
	 * would only have to be waited for.
	 */
	if (length <= 512U) {
		(void)shell_write_nosigpipe(descriptors[1], text, length);
		(void)close(descriptors[1]);
	} else {
		child = fork();

		/* Handles a failed fork operation. */
		if (child < 0) {
			fprintf(stderr, "sh: cannot fork: %s\n",
				strerror(errno));
			(void)close(descriptors[0]);
			(void)close(descriptors[1]);

			/* Reports successful completion. */
			return 0;
		}

		/*
		 * The writer is a child of a child, so that it is nobody's
		 * to wait for: this shell has no place to wait for it, and
		 * the command that reads it is not its parent.
		 */
		if (child == 0) {
			size_t written;
			ssize_t wrote;

			(void)close(descriptors[0]);

			/* The middle process exits at once and is waited for. */
			if (fork() != 0)
				_exit(0);
			written = 0;

			/* Continue while the operation condition remains true. */
			while (written < length) {
				wrote = write(descriptors[1], text + written,
					      length - written);

				/* Handles a write that went nowhere. */
				if (wrote <= 0)
					break;
				written += (size_t)wrote;
			}
			_exit(0);
		}
		(void)close(descriptors[1]);

		/* Continue while the operation condition remains true. */
		while (waitpid(child, NULL, 0) < 0 && errno == EINTR)
			continue;
	}

	/* Handles a failed dup2 operation. */
	if (descriptors[0] != descriptor) {
		if (dup2(descriptors[0], descriptor) < 0) {
			fprintf(stderr, "sh: cannot redirect: %s\n",
				strerror(errno));
			(void)close(descriptors[0]);

			/* Reports successful completion. */
			return 0;
		}
		(void)close(descriptors[0]);
	}

	/* Reports operation failure. */
	return 1;
}

/*
 * Supports the redirections apply operation.
 *
 * Applies the redirections in the order they were written, which is the
 * order that decides what a later one sees.  When a place to save them is
 * given, what each descriptor held first is kept there, so that a builtin
 * running in this shell can be given the descriptors back afterwards.
 */
static int
redirections_apply(
	const struct redirection *items,
	int count,
	struct redirect_save *saves,
	int *save_count)
{
	int descriptor;
	int index;
	int position;
	int kept;

	/* Handles the save availability. */
	if (save_count != NULL)
		*save_count = 0;

	/* Process each remaining element. */
	for (index = 0; index < count; index++) {
		/* Keeps what the descriptor held before anything changes it. */
		if (saves != NULL) {
			kept = 0;

			/* Process each remaining element. */
			for (position = 0; position < *save_count; position++)
				if (saves[position].descriptor ==
				    items[index].descriptor)
					kept = 1;

			/* Handles a descriptor that is not kept yet. */
			if (!kept) {
				saves[*save_count].descriptor =
				    items[index].descriptor;
				saves[*save_count].saved =
				    fcntl(items[index].descriptor,
					  F_DUPFD_CLOEXEC, 10);
				(*save_count)++;
			}
		}

		/* Handles a redirection that closes the descriptor. */
		if (items[index].source == REDIRECT_CLOSE) {
			(void)close(items[index].descriptor);
			continue;
		}

		/* Handles a here-document, which is written into a pipe. */
		if (items[index].source == REDIRECT_HEREDOC) {
			/* Handles a failed heredoc open operation. */
			if (!heredoc_open(items[index].path,
					  items[index].descriptor))
				return 0;
			continue;
		}

		/* Handles a redirection that names another descriptor. */
		if (items[index].source != REDIRECT_FROM_PATH) {
			/* Handles a failed dup2 operation. */
			if (dup2(items[index].source,
				 items[index].descriptor) < 0) {
				fprintf(stderr, "sh: %d: %s\n",
					items[index].source, strerror(errno));

				/* Reports successful completion. */
				return 0;
			}
			continue;
		}
		(void)fflush(NULL);
		descriptor = open(items[index].path, items[index].flags, 0666);

		/* Handles a failed open operation. */
		if (descriptor < 0) {
			fprintf(stderr, "sh: %s: %s\n", items[index].path,
				strerror(errno));

			/* Reports successful completion. */
			return 0;
		}

		/* Handles a failed dup2 operation. */
		if (descriptor != items[index].descriptor &&
		    dup2(descriptor, items[index].descriptor) < 0) {
			fprintf(stderr, "sh: %s: %s\n", items[index].path,
				strerror(errno));
			(void)close(descriptor);

			/* Reports successful completion. */
			return 0;
		}

		/* Checks the file descriptor. */
		if (descriptor != items[index].descriptor)
			(void)close(descriptor);
	}

	/* Reports operation failure. */
	return 1;
}

/* Supports the redirections undo operation. */
static void
redirections_undo(
	struct redirect_save *saves,
	int save_count)
{
	int index;

	(void)fflush(NULL);

	/* Process each remaining element. */
	for (index = save_count - 1; index >= 0; index--) {
		/* Handles a descriptor that held nothing to put back. */
		if (saves[index].saved < 0) {
			(void)close(saves[index].descriptor);
			continue;
		}
		(void)dup2(saves[index].saved, saves[index].descriptor);
		(void)close(saves[index].saved);
	}
}

/* Supports the pipeline free operation. */
static void
pipeline_free(
	struct pipeline_command *items,
	int count)
{
	int command_index, argument;

	/* Process each remaining element. */
	for (command_index = 0; command_index < count; command_index++) {
		/* Process each remaining command-line operand. */
		for (argument = 0; argument < items[command_index].argc;
		     argument++)
			free(items[command_index].argv[argument]);
		/* Process each remaining element. */
		for (argument = 0;
		     argument < items[command_index].redirect_count;
		     argument++)
			free(items[command_index].redirects[argument].path);
	}
}

/* Supports the execute pipeline operation. */
static int
execute_pipeline(
	struct pipeline_command *items,
	int count,
	int background)
{
	int function_result;
	char release_local;
	ssize_t release_count_local;
	char release_local1[PIPELINE_MAX];
	ssize_t release_count_local2;
	pid_t waited_local;
	pid_t waited_local3;
	pid_t child;
	int status;
	pid_t children[PIPELINE_MAX];
	pid_t stopped[PIPELINE_MAX];
	pid_t group;
	pid_t shell_group;
	int terminal;
	int synchronize;
	int gate[2] = {-1, -1};
	int terminal_owned;
	int active[PIPELINE_MAX] = {0};
	int input;
	int index, created;
	int stopped_count;
	int last_status;
	int saved_errno;
	int descriptors[2];

	group = 0;
	shell_group = getpgrp();
	terminal = !command_subshell && shell_controls_terminal();
	synchronize = terminal && !background;
	terminal_owned = 0;
	input = -1;
	created = 0;
	stopped_count = 0;
	last_status = 0;

	/* Checks the remaining item count. */
	if (count == 1 && !background) {
		/* Obtains the execute parent command result. */
		function_result = execute_parent_command(&items[0]);

		/* Returns the computed result. */
		return function_result;
	}

	/* Handles a failed pipe2 operation. */
	if (synchronize && pipe2(gate, O_CLOEXEC) != 0) {
		fprintf(stderr, "sh: pipeline: %s\n", strerror(errno));

		/* Reports successful completion. */
		return 0;
	}
	(void)fflush(NULL);

	/* Process each remaining element. */
	for (index = 0; index < count; index++) {
		descriptors[0] = -1;
		descriptors[1] = -1;

		/* Handles a failed pipe operation. */
		if (index + 1 < count && pipe(descriptors) != 0)
			goto failed;
		child = fork();

		/* Checks the child process state. */
		if (child < 0) {
			/* Handles the descriptors condition. */
			if (descriptors[0] >= 0)
				(void)close(descriptors[0]);

			/* Handles the descriptors condition. */
			if (descriptors[1] >= 0)
				(void)close(descriptors[1]);
			goto failed;
		}

		/* Checks the child process state. */
		if (child == 0) {
			/* Handles the synchronize condition. */
			if (synchronize)
				(void)close(gate[1]);

			/* Handles a failed setpgid operation. */
			if (setpgid(0, group == 0 ? 0 : group) != 0)
				_exit(126);

			/* Handles the synchronize condition. */
			if (synchronize) {
				do
					release_count_local =
					    read(gate[0], &release_local, 1);

				/* Process each remaining element. */
				while (release_count_local < 0 && errno == EINTR);
				(void)close(gate[0]);

				/* Handles the release count local condition. */
				if (release_count_local != 1 || release_local != 'x')
					_exit(126);
			}

			/* Handles a failed dup2 operation. */
			if (input >= 0 && dup2(input, STDIN_FILENO) < 0)
				_exit(126);

			/* Handles a failed dup2 operation. */
			if (descriptors[1] >= 0 &&
			    dup2(descriptors[1], STDOUT_FILENO) < 0)
				_exit(126);

			/* Validates the current input. */
			if (input >= 0)
				(void)close(input);

			/* Handles the descriptors condition. */
			if (descriptors[0] >= 0)
				(void)close(descriptors[0]);

			/* Handles the descriptors condition. */
			if (descriptors[1] >= 0)
				(void)close(descriptors[1]);

			/* Handles a failed pipeline child operation. */
			execution_status = -1;
			function_result = pipeline_child(&items[index]);
			(void)fflush(NULL);
			_exit(execution_status >= 0 ? execution_status :
			    (function_result ? 0 : 1));
		}

		/* Handles the group condition. */
		if (group == 0)
			group = child;
		children[created] = child;
		active[created++] = 1;

		/* Validates the current input. */
		if (input >= 0)
			(void)close(input);

		/* Handles the descriptors condition. */
		if (descriptors[1] >= 0)
			(void)close(descriptors[1]);
		input = descriptors[0];

		/* Handles the synchronize condition. */
		if (synchronize) {
			/* Handles a failed setpgid operation. */
			if (setpgid(child, group) != 0)
				goto failed;

			/* Handles a failed getpgid operation. */
			if (getpgid(child) != group) {
				errno = EPERM;
				goto failed;
			}
		} else {
			(void)setpgid(child, group);
		}
	}

	/* Validates the current input. */
	if (input >= 0)
		(void)close(input);
	input = -1;

	/* Handles the background condition. */
	if (background) {
		remember_job(group, children, created);
		printf("[%d]\n", (int)group);

		/* Reports operation failure. */
		return 1;
	}

	/* Handles the synchronize condition. */
	if (synchronize) {
		(void)close(gate[0]);
		gate[0] = -1;

		/* Handles a failed shell tcsetpgrp operation. */
		if (shell_tcsetpgrp(STDIN_FILENO, group) != 0)
			goto failed;
		terminal_owned = 1;
		memset(release_local1, 'x', (size_t)created);
		release_count_local2 =
		    shell_write_nosigpipe(gate[1], release_local1, (size_t)created);

		/* Handles the release count local2 condition. */
		if (release_count_local2 != created) {
			/* Handles the release count local2 condition. */
			if (release_count_local2 >= 0)
				errno = EIO;
			goto failed;
		}
		(void)close(gate[1]);
		gate[1] = -1;
	}

	/* Process each remaining element. */
	for (index = 0; index < created; index++) {
		status = 0;

		do

		/* Continue while the operation condition remains true. */
			waited_local = waitpid(children[index], &status, WUNTRACED);
		while (waited_local < 0 && errno == EINTR);

		/* Handles the waited local condition. */
		if (waited_local < 0) {
			saved_errno = errno;
			goto wait_failed;
		}

		/* Handles the children condition. */
		if (children[index] == children[created - 1])
			last_status = status;

		/* Checks the operation status. */
		if (WIFSTOPPED(status)) {
			stopped[stopped_count++] = children[index];
		} else {
			active[index] = 0;
		}
	}

	/* Handles a failed shell tcsetpgrp operation. */
	if (terminal_owned && shell_tcsetpgrp(STDIN_FILENO, shell_group) != 0) {
		fprintf(stderr,
			"sh: cannot restore foreground process group: %s\n",
			strerror(errno));
	}

	/* Handles the stopped count condition. */
	if (stopped_count > 0) {
		remember_job(group, stopped, stopped_count);
		printf("[%d] stopped\n", (int)group);
	}

	/* Computes the function result. */
	function_result = wait_status_result(last_status);

	/* Returns the computed result. */
	return function_result;
wait_failed:
	errno = saved_errno;
failed:
	saved_errno = errno;

	/* Validates the current input. */
	if (input >= 0)
		(void)close(input);

	/* Handles the gate condition. */
	if (gate[0] >= 0)
		(void)close(gate[0]);

	/* Handles the gate condition. */
	if (gate[1] >= 0)
		(void)close(gate[1]);

	/* Process each remaining element. */
	for (index = 0; index < created; index++) {
		/* Handles the active condition. */
		if (active[index]) {
			(void)kill(-group, SIGKILL);
			break;
		}
	}

	/* Process each remaining element. */
	for (index = 0; index < created; index++) {
		/* Handles the active condition. */
		if (active[index])
			(void)kill(children[index], SIGKILL);
	}

	/* Process each remaining element. */
	for (index = 0; index < created; index++) {
		/* Handles the active condition. */
		if (!active[index])
			continue;
		do

		/* Continue while the operation condition remains true. */
			waited_local3 = waitpid(children[index], NULL, 0);
		while (waited_local3 < 0 && errno == EINTR);
	}

	/* Handles the terminal owned condition. */
	if (terminal_owned)
		(void)shell_tcsetpgrp(STDIN_FILENO, shell_group);
	errno = saved_errno;
	fprintf(stderr, "sh: pipeline: %s\n", strerror(saved_errno));

	/* Reports successful completion. */
	return 0;
}

/* Supports the execute parent command operation. */
static int
execute_parent_command(
	struct pipeline_command *item)
{
	struct redirect_save saves[REDIRECT_MAX];
	int save_count;
	int result;

	save_count = 0;

	/* Handles a failed redirections apply operation. */
	if (!redirections_apply(item->redirects, item->redirect_count, saves,
				&save_count)) {
		redirections_undo(saves, save_count);
		execution_status = 1;

		/* Reports successful completion. */
		return 0;
	}
	command_background = 0;
	result = command_argv(item->argc, item->argv);
	(void)fflush(NULL);
	redirections_undo(saves, save_count);

	/* Returns the computed result. */
	return result;
}

/* Supports the command argv operation. */
/* Retains full statuses while adapting boolean builtins at one boundary. */
static int
command_argv(
	int argc,
	char **argv)
{
	int success;

	/* An execution helper may supply an exact child or nested-list status. */
	execution_status = -1;
	success = command_argv_body(argc, argv);
	if (execution_status < 0)
		execution_status = success ? 0 : 1;

	/* Returns the predicate used by the existing builtin dispatch API. */
	return success;
}

/* Converts a wait status without discarding its exit code or signal. */
static int
wait_status_result(
	int status)
{
	/* Normal exits retain all eight exit-status bits. */
	if (WIFEXITED(status))
		execution_status = WEXITSTATUS(status);
	else if (WIFSIGNALED(status))
		execution_status = 128 + WTERMSIG(status);
	else if (WIFSTOPPED(status))
		execution_status = 128 + WSTOPSIG(status);
	else
		execution_status = 1;

	/* Conditional operators consume success, expansion consumes the full code. */
	return execution_status == 0;
}

/* Executes assignments and dispatches one simple command. */
static int
command_argv_body(
	int argc,
	char **argv)
{
	struct sh_var_snapshot snapshots[ARG_MAX];
	int assignments;
	int temporary;
	int index;
	int result;

	assignments = 0;
	temporary = 0;

	/* Validates the command-line arguments. */
	if (argc == 0)
		return 1;

	/* Process each remaining command-line operand. */
	while (assignments < argc && assignment_length(argv[assignments]) >= 0)
		assignments++;

	/* Validates the command-line arguments. */
	if (assignments == argc ||
	    (assignments != 0 && special_builtin_name(argv[assignments]))) {
		/* Process each remaining element. */
		for (index = 0; index < assignments; index++) {
			/* Validates the command-line arguments. */
			if (apply_assignment(argv[index]) < 0) {
				fprintf(stderr, "sh: %s: %s\n", argv[index],
					strerror(errno));

				/* Reports successful completion. */
				return 0;
			}
		}
	} else {
		/* Process each remaining element. */
		for (index = 0; index < assignments; index++) {
			/* Validates the command-line arguments. */
			if (temporary_assignment(argv[index],
						 &snapshots[index]) != 0) {
				/* Process each remaining element. */
				while (index-- > 0)
					(void)sh_var_restore(&snapshots[index]);
				fprintf(stderr, "sh: %s: %s\n", argv[index + 1],
					strerror(errno));

				/* Reports successful completion. */
				return 0;
			}
			temporary++;
		}
	}

	/* Validates the command-line arguments. */
	if (assignments == argc)
		return 1;

	/* Continue while the operation condition remains true. */
	result = command_dispatch(argc - assignments, argv + assignments);
	while (temporary-- > 0) {
		/* Handles a failed sh var restore operation. */
		if (sh_var_restore(&snapshots[temporary]) != 0)
			result = 0;
	}

	/* Returns the computed result. */
	return result;
}

/* Supports the special builtin name operation. */
static int
special_builtin_name(
	const char *name)
{
	static const char *const names[] = {
	    ":",    ".",     "break",  "continue", "eval",
	    "exec", "exit",  "export", "readonly", "return",
	    "set",  "shift", "times",  "trap",	   "unset",  NULL};
	int index;

	/* Process each remaining element. */
	for (index = 0; names[index] != NULL; index++) {
		/* Selects the matching value. */
		if (strcmp(name, names[index]) == 0)
			return 1;
	}

	/* Reports successful completion. */
	return 0;
}

/* Supports the apply assignment operation. */
static int
apply_assignment(
	char *text)
{
	int length;
	char saved;
	int result;

	length = assignment_length(text);

	/* Checks the current data length. */
	if (length < 0)
		return 0;
	saved = text[length];
	text[length] = '\0';
	result = sh_var_set(text, text + length + 1, -1) == 0;
	text[length] = saved;

	/* Returns the computed result. */
	return result ? 1 : -1;
}

/* Supports the temporary assignment operation. */
static int
temporary_assignment(
	char *text,
	struct sh_var_snapshot *snapshot)
{
	int length;
	char saved;
	int result;

	length = assignment_length(text);

	/* Checks the current data length. */
	if (length < 0)
		return -1;
	saved = text[length];
	text[length] = '\0';

	/* Handles a failed sh var snapshot operation. */
	if (sh_var_snapshot(text, snapshot) != 0) {
		text[length] = saved;

		/* Reports operation failure. */
		return -1;
	}
	result = sh_var_set(text, text + length + 1, 1);
	text[length] = saved;

	/* Checks the operation result. */
	if (result != 0) {
		(void)sh_var_restore(snapshot);

		/* Reports operation failure. */
		return -1;
	}

	/* Reports successful completion. */
	return 0;
}

/* Supports the command dispatch operation. */
static int
command_dispatch(
	int argc,
	char **argv)
{
	int function_result;
	const char *name;
	int index_local;
	const char *value_local;
	int index_local1, result_local;
	char candidate_local[256];
	int index_local2;
	int result_local3;
	unsigned long value_local4;
	char candidate_local5[256];
	char candidate_local6[256];
	char **child_local;
	int result_local7;
	int i_local;
	int index_local8;
	int length_local;
	char saved_local;
	int index_local11;
	int length_local10;
	char saved_local9;
	char *end;
	int status;
	pid_t job;
	char *equals;
	const char *path;
	int number;
	const char *action;
	char *text;
	int index;
	int result;
	int success;
	long count;
	mode_t old;
	int first;
	int handled;

	/* Validates the command-line arguments. */
	if (argc == 0)
		return 1;

	/*
	 * A function stands where a command of that name would, ahead of the
	 * builtins the shell provides but behind the few whose behaviour the
	 * standard fixes.
	 */
	if (!special_builtin_name(argv[0])) {
		struct shell_function *function = find_function(argv[0]);

		if (function != NULL) {
			/* Obtains the call function result. */
			function_result = call_function(function, argc, argv);

			/* Returns the computed result. */
			return function_result;
		}
	}

	/*
	 * break and continue name how many enclosing loops they act on.
	 * Outside a loop there is nothing to leave, and the standard leaves
	 * that case to the shell, so it is quietly nothing.
	 */
	if (!strcmp(argv[0], "break") || !strcmp(argv[0], "continue")) {
		count = 1;

		/* Validates the command-line arguments. */
		if (argc > 2) {
			fprintf(stderr, "sh: %s: too many arguments\n",
				argv[0]);
			execution_status = 2;

			/* Reports successful completion. */
			return 0;
		}

		/* Validates the command-line arguments. */
		if (argc == 2) {
			count = strtol(argv[1], &end, 10);

			/* Validates the command-line arguments. */
			if (*argv[1] == '\0' || *end != '\0' || count < 1) {
				fprintf(stderr, "sh: %s: %s\n", argv[0],
					argv[1]);
				execution_status = 2;

				/* Reports successful completion. */
				return 0;
			}
		}

		/* Handles a loop count of none. */
		if (loop_depth == 0) {
			execution_status = 0;

			/* Reports operation failure. */
			return 1;
		}

		/* One may not leave more loops than one is inside. */
		if (count > loop_depth)
			count = loop_depth;
		if (!strcmp(argv[0], "break"))
			break_pending = (int)count;
		else
			continue_pending = (int)count;
		execution_status = 0;

		/* Reports operation failure. */
		return 1;
	}

	/*
	 * return ends a function, or the file a dot command is reading, with
	 * the status it is given or with the one the last command left.
	 */
	if (!strcmp(argv[0], "return")) {
		count = shell_status;

		/* Handles a return written where there is nothing to return from. */
		if (function_depth == 0 && source_depth == 0) {
			fprintf(stderr,
				"sh: return: not in a function or a file\n");
			execution_status = 2;

			/* Reports successful completion. */
			return 0;
		}

		/* Validates the command-line arguments. */
		if (argc > 2) {
			fprintf(stderr, "sh: return: too many arguments\n");
			execution_status = 2;

			/* Reports successful completion. */
			return 0;
		}

		/* Validates the command-line arguments. */
		if (argc == 2) {
			count = strtol(argv[1], &end, 10);

			/* Validates the command-line arguments. */
			if (*argv[1] == '\0' || *end != '\0') {
				fprintf(stderr, "sh: return: %s\n", argv[1]);
				execution_status = 2;

				/* Reports successful completion. */
				return 0;
			}
		}
		return_status = (int)(count & 0xff);
		return_pending = 1;
		execution_status = return_status;

		/* Returns the computed result. */
		return return_status == 0;
	}

	/*
	 * The time this shell and the commands it has waited for have spent.
	 * The clock counts in ticks, so the seconds and the hundredths are
	 * taken from it rather than printed as a number of ticks.
	 */
	if (!strcmp(argv[0], "times")) {
		struct tms spent;
		long ticks;

		ticks = sysconf(_SC_CLK_TCK);

		/* Handles a clock whose rate is not known. */
		if (ticks <= 0)
			ticks = 100;

		/* Handles a failed times operation. */
		if (times(&spent) == (clock_t)-1) {
			fprintf(stderr, "sh: times: %s\n", strerror(errno));
			execution_status = 1;

			/* Reports successful completion. */
			return 0;
		}
		printf("%ldm%ld.%02lds %ldm%ld.%02lds\n",
		       (long)spent.tms_utime / ticks / 60,
		       (long)spent.tms_utime / ticks % 60,
		       (long)spent.tms_utime % ticks * 100 / ticks,
		       (long)spent.tms_stime / ticks / 60,
		       (long)spent.tms_stime / ticks % 60,
		       (long)spent.tms_stime % ticks * 100 / ticks);
		printf("%ldm%ld.%02lds %ldm%ld.%02lds\n",
		       (long)spent.tms_cutime / ticks / 60,
		       (long)spent.tms_cutime / ticks % 60,
		       (long)spent.tms_cutime % ticks * 100 / ticks,
		       (long)spent.tms_cstime / ticks / 60,
		       (long)spent.tms_cstime / ticks % 60,
		       (long)spent.tms_cstime % ticks * 100 / ticks);
		execution_status = 0;

		/* Reports operation failure. */
		return 1;
	}

	/* Handles the selected command-line operation. */
	if (!strcmp(argv[0], "jobs")) {
		/* Handles the last job condition. */
		if (last_job > 0)
			printf("[%d] active or stopped\n", (int)last_job);

		/* Reports operation failure. */
		return 1;
	}

	/* Handles the selected command-line operation. */
	if (!strcmp(argv[0], "bg")) {
		/* Computes the function result. */
		function_result = last_job > 0 && kill(-last_job, SIGCONT) == 0;

		/* Returns the computed result. */
		return function_result;
	}

	/* Handles the selected command-line operation. */
	if (!strcmp(argv[0], "fg")) {
		status = 0;
		job = last_job;

		/* Handles the job condition. */
		if (job <= 0)
			return 0;

		/* Obtains the continue foreground result. */
		function_result = continue_foreground(job, &status);

		/* Returns the computed result. */
		return function_result;
	}

	/* Handles the selected command-line operation. */
	if (!strcmp(argv[0], "help")) {
		puts("help echo pwd cd true false jobs fg bg env set export "
		     "readonly unset wait source exit");

		/* Reports operation failure. */
		return 1;
	}

	/* Handles the selected command-line operation. */
	if (!strcmp(argv[0], "alias")) {
		/* Validates the command-line arguments. */
		if (argc == 1) {
			sh_alias_print();

			/* Reports operation failure. */
			return 1;
		}

		/* Process each remaining command-line operand. */
		for (index_local = 1; index_local < argc; index_local++) {
			equals = strchr(argv[index_local], '=');

			/* Handles the equals availability. */
			if (equals == NULL) {
				value_local = sh_alias_get(argv[index_local]);

				/* Handles the value local availability. */
				if (value_local == NULL)
					return 0;
				printf("alias %s='%s'\n", argv[index_local], value_local);
				continue;
			}
			*equals = '\0';
			/* Validates the command-line arguments. */
			if (sh_alias_set(argv[index_local], equals + 1) != 0) {
				*equals = '=';
				/* Reports successful completion. */
				return 0;
			}
			*equals = '=';
		}

		/* Reports operation failure. */
		return 1;
	}

	/* Handles the selected command-line operation. */
	if (!strcmp(argv[0], "unalias")) {
		result_local = 1;

		/* Handles the selected command-line operation. */
		if (argc == 2 && !strcmp(argv[1], "-a")) {
			sh_alias_clear();

			/* Reports operation failure. */
			return 1;
		}

		/* Validates the command-line arguments. */
		if (argc < 2)
			return 0;

		/* Process each remaining command-line operand. */
		for (index_local1 = 1; index_local1 < argc; index_local1++) {
			/* Validates the command-line arguments. */
			if (sh_alias_unset(argv[index_local1]) != 0)
				result_local = 0;
		}

		/* Returns the computed result. */
		return result_local;
	}

	/* Handles the selected command-line operation. */
	if (!strcmp(argv[0], "hash")) {
		path = sh_var_get("PATH");
		result = 1;

		/* Handles the selected command-line operation. */
		if (argc == 2 && !strcmp(argv[1], "-r")) {
			sh_hash_clear();

			/* Reports operation failure. */
			return 1;
		}

		/* Validates the command-line arguments. */
		if (argc > 1 && argv[1][0] == '-') {
			fprintf(stderr, "usage: hash [-r] [utility ...]\n");

			/* Reports successful completion. */
			return 0;
		}

		/* Handles a failed sh hash sync path operation. */
		if (sh_hash_sync_path(path) != 0)
			return 0;

		/* Validates the command-line arguments. */
		if (argc == 1) {
			sh_hash_print();

			/* Reports operation failure. */
			return 1;
		}

		/* Process each remaining command-line operand. */
		for (index = 1; index < argc; index++) {
			/* Validates the command-line arguments. */
			if (strchr(argv[index], '/') != NULL ||
			    !resolve_command(argv[index], candidate_local,
					     sizeof(candidate_local))) {
				fprintf(stderr, "hash: %s: not found\n",
					argv[index]);
				result = 0;
			} else if (sh_hash_store(argv[index], candidate_local) != 0)

				/* Reports successful completion. */
				return 0;
		}

		/* Returns the computed result. */
		return result;
	}

	/* Handles the selected command-line operation. */
	if (!strcmp(argv[0], "getopts")) {
		/* Obtains the shell getopts builtin result. */
		function_result = shell_getopts_builtin(argc, argv);

		/* Returns the computed result. */
		return function_result;
	}

	/* Handles the selected command-line operation. */
	if (!strcmp(argv[0], "trap")) {
		/* Validates the command-line arguments. */
		if (argc == 1) {
			/* Process each remaining element. */
			for (index_local2 = 1; index_local2 < SHELL_SIGNAL_MAX; index_local2++) {
				/* Handles the trap action condition. */
				if (trap_action[index_local2] != NULL) {
					printf("trap -- '%s' %d\n",
					       trap_action[index_local2], index_local2);
				}
			}

			/* Reports operation failure. */
			return 1;
		}

		/* Validates the command-line arguments. */
		if (argc < 3)
			return 0;

		/* Process each remaining command-line operand. */
		action = !strcmp(argv[1], "-") ? NULL : argv[1];
		for (index_local2 = 2; index_local2 < argc; index_local2++) {
			number = signal_number(argv[index_local2]);

			/* Handles a failed set trap operation. */
			if (number < 0 || !set_trap(action, number))
				return 0;
		}

		/* Reports operation failure. */
		return 1;
	}

	/* Handles the selected command-line operation. */
	if (!strcmp(argv[0], ".") || !strcmp(argv[0], "source")) {
		/* Computes the function result. */
		function_result = argc == 2 && source_file(argv[1]);

		/* Returns the computed result. */
		return function_result;
	}

	/* Handles the selected command-line operation. */
	if (!strcmp(argv[0], "eval")) {
		/* Validates the command-line arguments. */
		if (!join_arguments(argc, argv, 1, &text))
			return 0;
		result_local3 = command(text);
		free(text);

		/* Returns the computed result. */
		return result_local3;
	}

	/* Handles the selected command-line operation. */
	if (!strcmp(argv[0], "shift")) {
		count = 1;

		/* Validates the command-line arguments. */
		if (argc > 2)
			return 0;

		/* Validates the command-line arguments. */
		if (argc == 2) {
			count = strtol(argv[1], &end, 10);

			/* Validates the command-line arguments. */
			if (*argv[1] == '\0' || *end != '\0' || count < 0)
				return 0;
		}

		/* Checks the remaining item count. */
		if (count > shell_positional_count)
			return 0;
		shell_positional += count;
		shell_positional_count -= (int)count;

		/* Reports operation failure. */
		return 1;
	}

	/* Handles the selected command-line operation. */
	if (!strcmp(argv[0], "umask")) {
		/* Validates the command-line arguments. */
		if (argc == 1) {
			old = umask(0);
			(void)umask(old);
			printf("%04o\n", (unsigned)old);

			/* Reports operation failure. */
			return 1;
		}

		/* Validates the command-line arguments. */
		if (argc == 2) {
			value_local4 = strtoul(argv[1], &end, 8);

			/* Validates the command-line arguments. */
			if (*argv[1] == '\0' || *end != '\0' || value_local4 > 0777UL)
				return 0;
			(void)umask((mode_t)value_local4);

			/* Reports operation failure. */
			return 1;
		}

		/* Reports successful completion. */
		return 0;
	}

	/* Handles the selected command-line operation. */
	if (!strcmp(argv[0], "read")) {
		char *line;
		char *names[ARG_MAX + 1];
		int raw;
		int named;

		raw = 0;
		first = 1;

		/* A -r asks for the line exactly as it was written. */
		if (argc > 1 && strcmp(argv[1], "-r") == 0) {
			raw = 1;
			first = 2;
		}
		named = argc - first;

		/* With no name of its own the line is put in REPLY. */
		if (named == 0) {
			names[0] = (char *)"REPLY";
			child_local = names;
			named = 1;
			first = 0;
			argc = 1;
		} else {
			child_local = argv;
		}

		/* Process each remaining command-line operand. */
		for (index = first; index < argc; index++) {
			name = child_local[index];

			/* Validates the command-line arguments. */
			if (assignment_length(name) >= 0 ||
			    !(name[0] == '_' ||
			      (name[0] >= 'A' && name[0] <= 'Z') ||
			      (name[0] >= 'a' && name[0] <= 'z'))) {
				fprintf(stderr, "sh: read: %s: not a name\n",
					name);
				execution_status = 2;

				/* Reports successful completion. */
				return 0;
			}
		}

		/* Handles a failed read line operation. */
		if (read_line(&line, raw) < 0) {
			/* Every name is emptied when the input has ended. */
			for (index = first; index < argc; index++)
				(void)sh_var_set(child_local[index], "", -1);
			execution_status = 1;

			/* Reports successful completion. */
			return 0;
		}

		/* Computes the function result. */
		function_result = read_assign_fields(line, argc, child_local,
						     first);
		free(line);
		execution_status = function_result ? 0 : 1;

		/* Returns the computed result. */
		return function_result;
	}

	/* Handles the selected command-line operation. */
	if (!strcmp(argv[0], "wait")) {
		/* Obtains the shell wait builtin result. */
		function_result = shell_wait_builtin(argc, argv);

		/* Returns the computed result. */
		return function_result;
	}

	/* Handles the selected command-line operation. */
	if (!strcmp(argv[0], "type") || (!strcmp(argv[0], "command") &&
					 argc > 1 && !strcmp(argv[1], "-v"))) {
		/* Process each remaining command-line operand. */
		first = !strcmp(argv[0], "type") ? 1 : 2;
		success = first < argc;
		for (index = first; index < argc; index++) {
			/* Validates the command-line arguments. */
			if (shell_builtin_name(argv[index])) {
				printf("%s%s\n",
				       !strcmp(argv[0], "type")
					   ? "shell builtin: "
					   : "",
				       argv[index]);
			} else if (strchr(argv[index], '/') != NULL &&
				 access(argv[index], F_OK) == 0)
				puts(argv[index]);
			else if (search_path(argv[index], "", candidate_local5,
					     sizeof(candidate_local5)))
				puts(candidate_local5);
			else
				success = 0;
		}

		/* Returns the computed result. */
		return success;
	}

	/* Handles the selected command-line operation. */
	if (!strcmp(argv[0], "command")) {
		/* Computes the function result. */
		function_result = argc > 1 && command_argv(argc - 1, argv + 1);

		/* Returns the computed result. */
		return function_result;
	}

	/* Handles the selected command-line operation. */
	if (!strcmp(argv[0], "exec")) {
		child_local = argv + 1;

		/* Validates the command-line arguments. */
		if (argc < 2)
			return 1;

		/* Handles a failed strchr operation. */
		if (strchr(child_local[0], '/') == NULL) {
			/* Handles a failed search path operation. */
			if (!search_path(child_local[0], "", candidate_local6,
					 sizeof(candidate_local6))) {
				execution_status = 127;
				return 0;
			}
			child_local[0] = candidate_local6;
		}
		execve(child_local[0], child_local, environ);
		execution_status = errno == ENOENT ? 127 : 126;
		fprintf(stderr, "exec: %s: %s\n", child_local[0], strerror(errno));

		/* Reports successful completion. */
		return 0;
	}

	result_local7 = sh_builtin_dispatch(argc, argv, &handled);

	/* Handles the handled condition. */
	if (handled)
		return result_local7;

	/* Handles the selected command-line operation. */
	if (!strcmp(argv[0], "env")) {
		/* Process each element required by the operation. */
		for (i_local = 0; environ != NULL && environ[i_local] != NULL; i_local++)
			puts(environ[i_local]);

		/* Returns the computed result. */
		return argc == 1;
	}

	/* Handles the selected command-line operation. */
	if (!strcmp(argv[0], "set")) {
		first = 1;

		/*
		 * The options this shell knows are accepted and have no
		 * effect; what set is wanted for here is the parameters that
		 * follow the two dashes.
		 */
		while (first < argc && (argv[first][0] == '-' ||
					argv[first][0] == '+') &&
		       argv[first][1] != '\0') {
			/* Two dashes end the options and begin the words. */
			if (strcmp(argv[first], "--") == 0) {
				first++;
				break;
			}

			/* A dash turns an option on and a plus turns it off. */
			handled = argv[first][0] == '-';

			/* Process each remaining element. */
			for (index = 1; argv[first][index] != '\0'; index++) {
				/* Dispatch the selected option. */
				switch (argv[first][index]) {
				case 'e':
					option_errexit = handled;
					break;
				case 'x':
					option_trace = handled;
					break;
				case 'u':
					option_unset_error = handled;
					break;

				/*
				 * These the shell reads and does nothing
				 * with: what they ask for is either already
				 * how it behaves or is not offered.
				 */
				case 'a':
				case 'b':
				case 'f':
				case 'h':
				case 'm':
				case 'n':
				case 'v':
				case 'C':
					break;
				default:
					fprintf(stderr,
						"sh: set: -%c: bad option\n",
						argv[first][index]);
					execution_status = 2;

					/* Reports successful completion. */
					return 0;
				}
			}
			first++;
		}

		/* A set with nothing to set leaves the parameters alone. */
		if (first >= argc && argc > 1 &&
		    strcmp(argv[argc - 1], "--") != 0) {
			execution_status = 0;

			/* Reports operation failure. */
			return 1;
		}

		/* Handles a failed positional replace operation. */
		if (!positional_replace(argc, argv, first)) {
			fprintf(stderr, "sh: set: out of memory\n");
			execution_status = 1;

			/* Reports successful completion. */
			return 0;
		}
		execution_status = 0;

		/* Reports operation failure. */
		return 1;
	}

	/* Handles the selected command-line operation. */
	if (!strcmp(argv[0], "unset")) {
		/* Computes the function result. */
		function_result = argc == 2 && sh_var_unset(argv[1]) == 0;

		/* Returns the computed result. */
		return function_result;
	}

	/* Handles the selected command-line operation. */
	if (!strcmp(argv[0], "export")) {
		/* Validates the command-line arguments. */
		if (argc < 2)
			return 0;

		/* Process each remaining command-line operand. */
		for (index_local8 = 1; index_local8 < argc; index_local8++) {
			length_local = assignment_length(argv[index_local8]);

			/* Handles the length local condition. */
			if (length_local >= 0) {
				saved_local = argv[index_local8][length_local];
				argv[index_local8][length_local] = '\0';

				/* Validates the command-line arguments. */
				if (sh_var_set(argv[index_local8],
					       argv[index_local8] + length_local + 1,
					       1) != 0) {
					argv[index_local8][length_local] = saved_local;

					/* Reports successful completion. */
					return 0;
				}
				argv[index_local8][length_local] = saved_local;
			} else if (sh_var_export(argv[index_local8]) != 0)

				/* Reports successful completion. */
				return 0;
		}

		/* Reports operation failure. */
		return 1;
	}

	/* Handles the selected command-line operation. */
	if (!strcmp(argv[0], "readonly")) {
		/* Validates the command-line arguments. */
		if (argc < 2)
			return 0;

		/* Process each remaining command-line operand. */
		for (index_local11 = 1; index_local11 < argc; index_local11++) {
			length_local10 = assignment_length(argv[index_local11]);

			/* Handles the length local10 condition. */
			if (length_local10 >= 0) {
				saved_local9 = argv[index_local11][length_local10];
				argv[index_local11][length_local10] = '\0';

				/* Validates the command-line arguments. */
				if (sh_var_set(argv[index_local11],
					       argv[index_local11] + length_local10 + 1,
					       -1) != 0 ||
				    sh_var_readonly(argv[index_local11]) != 0) {
					argv[index_local11][length_local10] = saved_local9;

					/* Reports successful completion. */
					return 0;
				}
				argv[index_local11][length_local10] = saved_local9;
			} else if (sh_var_readonly(argv[index_local11]) != 0)

				/* Reports successful completion. */
				return 0;
		}

		/* Reports operation failure. */
		return 1;
	}

	/* Handles the selected command-line operation. */
	if (!strcmp(argv[0], ":"))
		return 1;

	/* Handles the selected command-line operation. */
	if (!strcmp(argv[0], "exit"))
		exit(argc == 2 ? atoi(argv[1]) : shell_status);

	/* Validates the command-line arguments. */
	if (strchr(argv[0], '/') != NULL) {
		/* Explicit paths obey the same executable check as PATH matches. */
		function_result = run_resolved(argc, argv, argv[0]);

		/* Returns the computed result. */
		return function_result;
	}

	/* Obtains the run search path result. */
	function_result = run_search_path(argc, argv);

	/* Returns the computed result. */
	return function_result;
}

/* Supports the continue foreground operation. */
static int
continue_foreground(
	pid_t pid,
	int *status)
{
	pid_t result;
	pid_t processes[PIPELINE_MAX];
	pid_t retained[PIPELINE_MAX];
	pid_t shell_pgrp;
	int terminal;
	int foreground_set;
	int process_count;
	int retained_count;
	int index;
	int wait_failed;
	int saved_errno;

	shell_pgrp = getpgrp();
	terminal = shell_controls_terminal();
	foreground_set = 0;
	process_count = last_job_process_count;
	retained_count = 0;
	wait_failed = 0;

	/* Handles the process count condition. */
	if (process_count <= 0) {
		processes[0] = pid;
		process_count = 1;
	} else {
		/* Process each remaining element. */
		for (index = 0; index < process_count; index++)
			processes[index] = last_job_processes[index];
	}

	/*
 * A stopped terminal reader must own the terminal before it resumes.
	 * Keep last_job intact until both operations succeed so a failed
	 * handoff remains retryable. */
	if (terminal) {
		/* Handles a failed shell tcsetpgrp operation. */
		if (shell_tcsetpgrp(STDIN_FILENO, pid) != 0) {
			fprintf(stderr,
				"fg: cannot foreground process %d: %s\n",
				(int)pid, strerror(errno));

			/* Reports successful completion. */
			return 0;
		}
		foreground_set = 1;
	}

	/* Handles a failed kill operation. */
	if (kill(-pid, SIGCONT) != 0) {
		saved_errno = errno;

		/* Handles a failed shell tcsetpgrp operation. */
		if (foreground_set &&
		    shell_tcsetpgrp(STDIN_FILENO, shell_pgrp) != 0) {
			fprintf(
			    stderr,
			    "sh: cannot restore foreground process group: %s\n",
			    strerror(errno));
		}
		errno = saved_errno;
		fprintf(stderr, "fg: cannot continue process %d: %s\n",
			(int)pid, strerror(errno));

		/* Reports successful completion. */
		return 0;
	}

	/* Process each remaining element. */
	for (index = 0; index < process_count; index++) {
		do

		/* Continue while the operation condition remains true. */
			result = waitpid(processes[index], status, WUNTRACED);
		while (result < 0 && errno == EINTR);

		/* Checks the operation result. */
		if (result < 0) {
			/* Handles the reported system error. */
			if (errno == ECHILD)
				continue;

			/* Process each remaining element. */
			saved_errno = errno;
			wait_failed = 1;
			for (; index < process_count; index++)
				retained[retained_count++] = processes[index];
			break;
		}

		/* Checks the operation status. */
		if (WIFSTOPPED(*status))
			retained[retained_count++] = processes[index];
	}

	/* Handles a failed shell tcsetpgrp operation. */
	if (foreground_set && shell_tcsetpgrp(STDIN_FILENO, shell_pgrp) != 0) {
		fprintf(stderr,
			"sh: cannot restore foreground process group: %s\n",
			strerror(errno));
	}

	/* Handles an operation failure. */
	if (wait_failed) {
		remember_job(pid, retained, retained_count);
		errno = saved_errno;
		fprintf(stderr, "fg: cannot wait for process %d: %s\n",
			(int)pid, strerror(errno));

		/* Reports successful completion. */
		return 0;
	}

	/* Handles the retained count condition. */
	if (retained_count > 0) {
		remember_job(pid, retained, retained_count);
		printf("[%d] stopped\n", (int)pid);
	} else {
		forget_job();
	}

	/* Reports operation failure. */
	return 1;
}

/*
 * Supports the shell controls terminal operation.
 *
 * Reports whether this shell may hand the terminal to a command it starts.
 * Standing on a terminal is not enough: a shell started by init, or by
 * another program, shares that terminal with whoever already holds it, and
 * taking it would be refused and would take the command down with it.  The
 * shell may hand over only what it already has.
 */
static int
shell_controls_terminal(
	void)
{
	/* Handles a descriptor that is not a terminal at all. */
	if (!isatty(STDIN_FILENO))
		return 0;

	/* Returns the computed result. */
	return tcgetpgrp(STDIN_FILENO) == getpgrp();
}

/* Supports the shell tcsetpgrp operation. */
static int
shell_tcsetpgrp(
	int descriptor,
	pid_t pgrp)
{
	void (*previous)(int);
	int error, saved_errno;

	/*
 * A shell restoring itself from the background must not be stopped by
	 * the TIOCSPGRP operation which makes it foreground again. */
	previous = signal(SIGTTOU, (sighandler_t)SIG_IGN);

	/* Handles the previous condition. */
	if (previous == (sighandler_t)SIG_ERR)
		return -1;
	error = tcsetpgrp(descriptor, pgrp);
	saved_errno = errno;
	(void)signal(SIGTTOU, previous);
	errno = saved_errno;

	/* Returns the computed result. */
	return error;
}

/* Supports the remember job operation. */
static void
remember_job(
	pid_t group,
	const pid_t *processes,
	int count)
{
	int index;

	/* Process each remaining element. */
	last_job = group;
	last_job_process_count = count;
	for (index = 0; index < count; index++)
		last_job_processes[index] = processes[index];
}

/* Supports the forget job operation. */
static void
forget_job(
	void)
{
	last_job = 0;
	last_job_process_count = 0;
}

/* Supports the resolve command operation. */
static int
resolve_command(
	const char *name,
	char *candidate,
	size_t capacity)
{
	int function_result;

	/* Obtains the search path result. */
	function_result = search_path(name, "", candidate, capacity);

	/* Returns the computed result. */
	return function_result;
}

/* Supports the search path operation. */
static int
search_path(
	const char *name,
	const char *suffix,
	char *candidate,
	size_t capacity)
{
	int last;
	int result;
	const char *path;
	size_t position;

	path = sh_var_get("PATH");
	position = 0;

	/* Handles the path availability. */
	if (path == NULL)

	/* Continue until the operation reaches a terminal state. */
		path = "/bin:/usr/bin";
	for (;;) {
		result = path_candidate(path, &position, name, suffix,
					    candidate, capacity, &last);

		/* Handles a failed executable file operation. */
		if (result > 0 && is_executable_file(candidate))
			return 1;

		/* Handles the last condition. */
		if (last)
			break;
	}

	/* Reports successful completion. */
	return 0;
}

/* Supports the path candidate operation. */
static int
path_candidate(
	const char *path,
	size_t *position,
	const char *name,
	const char *suffix,
	char *candidate,
	size_t capacity,
	int *last)
{
	size_t start;
	size_t length;
	size_t name_length;
	size_t suffix_length;

	/* Continue while the operation condition remains true. */
	start = *position;
	name_length = strlen(name);
	suffix_length = strlen(suffix);
	while (path[*position] != '\0' && path[*position] != ':')
		(*position)++;
	length = *position - start;
	*last = path[*position] == '\0';
	/* Handles the last condition. */
	if (!*last)
		(*position)++;

	/* Checks the current data length. */
	if (length == 0) {
		/* Handles the name length condition. */
		if (2U + name_length + suffix_length > capacity)
			return -1;
		candidate[0] = '.';
		candidate[1] = '/';
		memcpy(candidate + 2, name, name_length);
		memcpy(candidate + 2 + name_length, suffix, suffix_length + 1U);

		/* Reports operation failure. */
		return 1;
	}

	/* Checks the current data length. */
	if (length + 1U + name_length + suffix_length + 1U > capacity)
		return -1;
	memcpy(candidate, path + start, length);
	candidate[length] = '/';
	memcpy(candidate + length + 1U, name, name_length);
	memcpy(candidate + length + 1U + name_length, suffix,
	       suffix_length + 1U);

	/* Reports operation failure. */
	return 1;
}

/* Supports the is executable file operation. */
static int
is_executable_file(
	const char *path)
{
	int function_result;
	struct stat status;

	/* Handles a failed stat operation. */
	if (stat(path, &status) != 0)
		return 0;

	/* Handles a failed S ISREG operation. */
	if (!S_ISREG(status.st_mode)) {
		errno = EACCES;

		/* Reports successful completion. */
		return 0;
	}

	/* Computes the function result. */
	function_result = access(path, X_OK) == 0;

	/* Returns the computed result. */
	return function_result;
}

/* Supports the shell getopts builtin operation. */
static int
shell_getopts_builtin(
	int argc,
	char **argv)
{
	int function_result;
	const char *argument;
	const char *value;
	const char *options;
	const char *index_text;
	char **arguments;
	int argument_count;
	char *end;
	long option_index;
	char option_name[2] = {0, 0};
	char bad[2];
	char missing[2];
	const char *definition;
	int silent;

	/* Validates the command-line arguments. */
	if (argc < 3 || !sh_var_name(argv[2]))
		return 0;
	options = argv[1];
	silent = options[0] == ':';

	/* Handles the silent condition. */
	if (silent)
		options++;
	arguments = argc > 3 ? argv + 3 : shell_positional;
	argument_count = argc > 3 ? argc - 3 : shell_positional_count;
	index_text = sh_var_get("OPTIND");
	option_index = index_text == NULL ? 1 : strtol(index_text, &end, 10);

	/* Handles the index text availability. */
	if (index_text != NULL && (*index_text == '\0' || *end != '\0'))
		option_index = 1;

	/* Handles the option index condition. */
	if (option_index < 1)
		option_index = 1;

	/* Handles the option index condition. */
	if (option_index != getopts_last_index)
		getopts_offset = 1;

	/* Handles the option index condition. */
	if (option_index > argument_count)
		return 0;

	/* Handles the getopts offset condition. */
	if (getopts_offset == 1) {
		argument = arguments[option_index - 1];

		/* Handles the argument condition. */
		if (argument[0] != '-' || argument[1] == '\0')
			return 0;

		/* Selects the matching value. */
		if (!strcmp(argument, "--")) {
			option_index++;
			getopts_last_index = option_index;
			(void)set_decimal_variable("OPTIND", option_index);

			/* Reports successful completion. */
			return 0;
		}
	}
	option_name[0] = arguments[option_index - 1][getopts_offset++];

	/* Handles the arguments condition. */
	if (arguments[option_index - 1][getopts_offset] == '\0') {
		option_index++;
		getopts_offset = 1;
	}
	definition = strchr(options, option_name[0]);

	/* Handles the definition availability. */
	if (definition == NULL) {
		bad[0] = option_name[0];
		bad[1] = '\0';
		(void)sh_var_set(argv[2], "?", -1);

		/* Handles the silent condition. */
		if (silent)
			(void)sh_var_set("OPTARG", bad, -1);
		else
			fprintf(stderr, "getopts: illegal option -- %c\n",
				option_name[0]);
		getopts_last_index = option_index;
		(void)set_decimal_variable("OPTIND", option_index);

		/* Reports operation failure. */
		return 1;
	}

	/* Handles the definition condition. */
	if (definition[1] == ':') {
		/* Handles the getopts offset condition. */
		if (getopts_offset != 1) {
			value = arguments[option_index - 1] + getopts_offset;
			option_index++;
			getopts_offset = 1;
		} else if (option_index <= argument_count) {
			value = arguments[option_index - 1];
			option_index++;
		} else {
			missing[0] = option_name[0];
			missing[1] = '\0';
			(void)sh_var_set(argv[2], silent ? ":" : "?", -1);

			/* Handles the silent condition. */
			if (silent)
				(void)sh_var_set("OPTARG", missing, -1);
			else
				fprintf(stderr,
					"getopts: option requires an argument "
					"-- %c\n",
					option_name[0]);
			getopts_last_index = option_index;
			(void)set_decimal_variable("OPTIND", option_index);

			/* Reports operation failure. */
			return 1;
		}
		(void)sh_var_set("OPTARG", value, -1);
	} else {
		(void)sh_var_unset("OPTARG");
	}
	(void)sh_var_set(argv[2], option_name, -1);
	getopts_last_index = option_index;

	/* Obtains the set decimal variable result. */
	function_result = set_decimal_variable("OPTIND", option_index);

	/* Returns the computed result. */
	return function_result;
}

/* Supports the set decimal variable operation. */
static int
set_decimal_variable(
	const char *name,
	long value)
{
	int function_result;
	char buffer[32];
	int length;

	length = snprintf(buffer, sizeof(buffer), "%ld", value);

	/* Computes the function result. */
	function_result = length > 0 && (size_t)length < sizeof(buffer) &&
	       sh_var_set(name, buffer, -1) == 0;

	/* Returns the computed result. */
	return function_result;
}

/* Supports the signal number operation. */
static int
signal_number(
	const char *name)
{
	static const struct {
		const char *name;
		int number;
	} names[] = {{"HUP", SIGHUP},	{"INT", SIGINT},   {"QUIT", SIGQUIT},
		     {"ILL", SIGILL},	{"TRAP", SIGTRAP}, {"ABRT", SIGABRT},
		     {"FPE", SIGFPE},	{"KILL", SIGKILL}, {"BUS", SIGBUS},
		     {"SEGV", SIGSEGV}, {"PIPE", SIGPIPE}, {"ALRM", SIGALRM},
		     {"TERM", SIGTERM}, {"USR1", SIGUSR1}, {"USR2", SIGUSR2},
		     {"CHLD", SIGCHLD}, {"CONT", SIGCONT}, {"STOP", SIGSTOP},
		     {"TSTP", SIGTSTP}, {"TTIN", SIGTTIN}, {"TTOU", SIGTTOU}};
	char *end;
	long value;
	size_t index;

	/* Selects the matching prefix. */
	if (!strncmp(name, "SIG", 3))
		name += 3;
	value = strtol(name, &end, 10);

	/* Validates the current name. */
	if (*name != '\0' && *end == '\0' && value > 0 &&
	    value < SHELL_SIGNAL_MAX)

		/* Returns the computed result. */
		return (int)value;

	/* Process each remaining element. */
	for (index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
		/* Selects the matching value. */
		if (!strcmp(name, names[index].name))
			return names[index].number;
	}

	/* Reports operation failure. */
	return -1;
}

/* Supports the set trap operation. */
static int
set_trap(
	const char *action,
	int number)
{
	struct sigaction disposition;
	char *copy;

	copy = NULL;

	/* Handles the number condition. */
	if (number <= 0 || number >= SHELL_SIGNAL_MAX || number == SIGKILL ||
	    number == SIGSTOP) {
		errno = EINVAL;

		/* Reports successful completion. */
		return 0;
	}

	/* Handles the action availability. */
	if (action != NULL && action[0] != '\0') {
		copy = malloc(strlen(action) + 1U);

		/* Handles the copy availability. */
		if (copy == NULL)
			return 0;
		strcpy(copy, action);
	}
	memset(&disposition, 0, sizeof(disposition));

	/* Handles the action availability. */
	if (action == NULL)
		disposition.sa_handler = SIG_DFL;
	else if (action[0] == '\0')
		disposition.sa_handler = SIG_IGN;
	else
		disposition.sa_handler = shell_signal_handler;
	disposition.sa_flags = SA_RESTART;
	sigemptyset(&disposition.sa_mask);

	/* Handles a failed sigaction operation. */
	if (sigaction(number, &disposition, NULL) != 0) {
		free(copy);

		/* Reports successful completion. */
		return 0;
	}
	free(trap_action[number]);
	trap_action[number] = copy;
	trap_pending[number] = 0;

	/* Reports operation failure. */
	return 1;
}

/* Supports the source file operation. */
static int
source_file(
	const char *path)
{
	int function_result;

	/* Obtains the source file mode result. */
	function_result = source_file_mode(path, 0);

	/* Returns the computed result. */
	return function_result;
}

/*
 * Supports the source file mode operation.
 *
 * The whole file is offered at once rather than a line at a time, because
 * a compound command is written across lines and only the whole of it says
 * where it ends.
 */
static int
source_file_mode(
	const char *path,
	int continue_on_error)
{
	FILE *file;
	char *buffer;
	struct stat status;

	(void)continue_on_error;

	/* Handles a failed stat operation. */
	if (stat(path, &status) != 0 || status.st_size < 0 ||
	    status.st_size >= SOURCE_MAX)

		/* Reports successful completion. */
		return 0;
	file = fopen(path, "rb");

	/* Handles the file availability. */
	if (file == NULL)
		return 0;
	buffer = malloc((size_t)status.st_size + 1U);

	/* Handles the buffer availability. */
	if (buffer == NULL) {
		fclose(file);

		/* Reports successful completion. */
		return 0;
	}

	/* Handles a failed fread operation. */
	if (fread(buffer, 1, (size_t)status.st_size, file) !=
	    (size_t)status.st_size) {
		fclose(file);
		free(buffer);

		/* Reports successful completion. */
		return 0;
	}
	fclose(file);
	buffer[status.st_size] = '\0';

	/* Ordinary failures do not enable an implicit errexit mode. */
	source_depth++;
	(void)command(buffer);
	source_depth--;
	free(buffer);

	/* A file that ended in the middle of a command did not hold one. */
	if (shell_incomplete) {
		fprintf(stderr, "sh: %s: unexpected end of file\n", path);
		shell_status = 2;
		shell_syntax_error = 1;
	}

	/* Handles the shell syntax error condition. */
	if (shell_syntax_error)
		return 0;

	/*
	 * A return written in the file ends the file rather than whatever is
	 * reading it, so it is answered here.
	 */
	if (return_pending) {
		return_pending = 0;
		shell_status = return_status;
	}

	/* Reports operation failure. */
	execution_status = shell_status;
	return shell_status == 0;
}

/* Supports the join arguments operation. */
static int
join_arguments(
	int argc,
	char **argv,
	int first,
	char **result)
{
	size_t item_local;
	size_t item_local1;
	size_t length;
	int index;
	char *text, *cursor;

	/* Process each remaining command-line operand. */
	length = 0;
	for (index = first; index < argc; index++) {
		item_local = strlen(argv[index]);

		/* Checks the current data length. */
		if (length > (size_t)-1 - item_local - 2U)
			return 0;
		length += item_local + (index != first);
	}
	text = malloc(length + 1U);

	/* Handles the text availability. */
	if (text == NULL)
		return 0;

	/* Process each remaining command-line operand. */
	cursor = text;
	for (index = first; index < argc; index++) {
		item_local1 = strlen(argv[index]);

		/* Checks the current index. */
		if (index != first)
			*cursor++ = ' ';
		memcpy(cursor, argv[index], item_local1);
		cursor += item_local1;
	}
	*cursor = '\0';
	*result = text;
	/* Reports operation failure. */
	return 1;
}

/*
 * Supports the read line operation.
 *
 * Reads one line, a character at a time.  The descriptor may be a pipe or a
 * terminal shared with whatever runs next, so nothing beyond the newline
 * may be taken: what is read here would otherwise be lost to the command
 * that reads after it.
 */
static int
read_line(
	char **result,
	int raw)
{
	char *buffer;
	char *grown;
	size_t capacity;
	size_t length;
	char value;
	ssize_t got;
	int any;

	capacity = 64;
	buffer = malloc(capacity);

	/* Handles a failed malloc operation. */
	if (buffer == NULL)
		return -1;
	length = 0;
	any = 0;

	/* Continue until the operation reaches a terminal state. */
	for (;;) {
		got = read(0, &value, 1);

		/* Handles a read that was interrupted by a signal. */
		if (got < 0 && errno == EINTR)
			continue;

		/* Handles the end of the input. */
		if (got <= 0)
			break;
		any = 1;

		/* Handles the end of the line. */
		if (value == '\n')
			break;

		/*
		 * A backslash before a newline joins the lines, and before
		 * anything else stands for that character alone, unless the
		 * caller asked for the line exactly as written.
		 */
		if (!raw && value == '\\') {
			char next;

			got = read(0, &next, 1);

			/* Handles the end of the input. */
			if (got <= 0)
				break;
			if (next == '\n')
				continue;
			value = next;
		}

		/* Handles a buffer with no room left. */
		if (length + 1U >= capacity) {
			capacity *= 2U;
			grown = realloc(buffer, capacity);

			/* Handles a failed realloc operation. */
			if (grown == NULL) {
				free(buffer);
				return -1;
			}
			buffer = grown;
		}
		buffer[length++] = value;
	}
	buffer[length] = '\0';

	/* Handles input that ended before anything at all was read. */
	if (!any) {
		free(buffer);
		return -1;
	}
	*result = buffer;

	/* Returns the computed result. */
	return (int)length;
}

/*
 * Supports the read assign fields operation.
 *
 * Splits the line on the characters IFS names and gives one field to each
 * name.  The last name is given everything that is left, separators and
 * all, so that a line holding more fields than there are names is not lost.
 */
static int
read_assign_fields(
	char *line,
	int argc,
	char **argv,
	int first)
{
	const char *separators;
	char *cursor;
	char *field;
	int index;
	int result;

	separators = sh_var_get("IFS");

	/* Handles an IFS that was never set. */
	if (separators == NULL)
		separators = " \t\n";
	cursor = line;
	result = 1;

	/* Process each remaining element. */
	for (index = first; index < argc; index++) {
		/* Leading separators belong to no field. */
		while (*cursor != '\0' && strchr(separators, *cursor) != NULL)
			cursor++;

		/* The last name is given the rest of the line as it stands. */
		if (index + 1 == argc) {
			field = cursor;
			cursor += strlen(cursor);

			/* Trailing separators belong to no field either. */
			while (field < cursor &&
			       strchr(separators, cursor[-1]) != NULL)
				cursor--;
			*cursor = '\0';
		} else {
			field = cursor;
			while (*cursor != '\0' &&
			       strchr(separators, *cursor) == NULL)
				cursor++;
			if (*cursor != '\0')
				*cursor++ = '\0';
		}

		/* Handles a failed sh var set operation. */
		if (sh_var_set(argv[index], field, -1) != 0) {
			fprintf(stderr, "sh: read: %s: %s\n", argv[index],
				strerror(errno));
			result = 0;
		}
	}

	/* Returns the computed result. */
	return result;
}

/* Supports the shell wait builtin operation. */
static int
shell_wait_builtin(
	int argc,
	char **argv)
{
	int function_result;
	char *end;
	long value;
	pid_t group;
	pid_t processes[PIPELINE_MAX];
	pid_t remaining[PIPELINE_MAX];
	int index;
	int count;
	int remaining_count;
	pid_t target;
	int status;

	status = 0;

	/* Validates the command-line arguments. */
	if (argc > 2) {
		fprintf(stderr, "usage: wait [PID]\n");

		/* Reports successful completion. */
		return 0;
	}

	/* Validates the command-line arguments. */
	if (argc == 2) {
		value = strtol(argv[1], &end, 10);

		/* Validates the command-line arguments. */
		if (*argv[1] == '\0' || *end != '\0' || value <= 0) {
			fprintf(stderr, "wait: invalid pid: %s\n", argv[1]);

			/* Reports successful completion. */
			return 0;
		}
		target = (pid_t)value;

		/* Handles a failed waitpid operation. */
		if (waitpid(target, &status, 0) != target)
			return 0;
	} else if (last_job > 0) {
		group = last_job;

		count = last_job_process_count;
		remaining_count = 0;

		/* Checks the remaining item count. */
		if (count <= 0) {
			processes[0] = group;
			count = 1;
		} else {
			/* Process each remaining element. */
			for (index = 0; index < count; index++)
				processes[index] = last_job_processes[index];
		}

		/* Process each remaining element. */
		for (index = 0; index < count; index++) {
			do

			/* Continue while the operation condition remains true. */
				target = waitpid(processes[index], &status, 0);
			while (target < 0 && errno == EINTR);

			/* Handles the reported system error. */
			if (target < 0 && errno != ECHILD) {
				/* Process each remaining element. */
				for (; index < count; index++) {
					remaining[remaining_count++] =
					    processes[index];
				}
				remember_job(group, remaining, remaining_count);

				/* Reports successful completion. */
				return 0;
			}
		}
		forget_job();
	} else {
		/* Continue while the operation condition remains true. */
		while (waitpid(-1, &status, 0) > 0)
			;

		/* Returns the computed result. */
		return errno == ECHILD;
	}

	/* Computes the function result. */
	function_result = wait_status_result(status);

	/* Returns the computed result. */
	return function_result;
}

/* Supports the shell builtin name operation. */
static int
shell_builtin_name(
	const char *name)
{
	static const char *const names[] = {
	    ":",       ".",	  "[",	     "alias",	 "bg",	     "break",
	    "cd",      "command", "continue", "echo",	 "env",	     "eval",
	    "exec",    "exit",	  "export",   "false",	 "fg",	     "getopts",
	    "hash",    "help",	  "jobs",     "printf",	 "pwd",	     "read",
	    "readonly", "return", "set",      "shift",	 "source",   "true",
	    "type",    "test",	  "umask",    "unalias", "ulimit",   "unset",
	    "times",   "wait",	  NULL};
	int index;

	/* Process each remaining element. */
	for (index = 0; names[index] != NULL; index++) {
		/* Selects the matching value. */
		if (strcmp(name, names[index]) == 0)
			return 1;
	}

	/* Reports successful completion. */
	return 0;
}

/* Supports the is elf operation. */
static int
is_elf(
	const char *path)
{
	int function_result;
	unsigned char magic[4];
	int fd;
	ssize_t count;

	fd = open(path, O_RDONLY);

	/* Checks the file descriptor. */
	if (fd < 0)
		return 0;
	count = read(fd, magic, sizeof(magic));
	close(fd);

	/* Computes the function result. */
	function_result = count == (ssize_t)sizeof(magic) && magic[0] == 0x7f &&
	       magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';

	/* Returns the computed result. */
	return function_result;
}

/* Supports the run external operation. */
static int
run_external(
	char *const argv[])
{
	int function_result;

	/* Obtains the spawn wait result. */
	function_result = spawn_wait(argv);

	/* Returns the computed result. */
	return function_result;
}

/* Supports the spawn wait operation. */
static int
spawn_wait(
	char *const argv[])
{
	int function_result;
	pid_t waited;
	posix_spawnattr_t attributes;
	posix_spawnattr_t *attribute_pointer;
	pid_t pid;
	int error;
	int status;

	attribute_pointer = NULL;
	status = 0;

	/* Handles a failed isatty operation. */
	if (!command_subshell && !command_background &&
	    shell_controls_terminal()) {
		/* Validates the command-line arguments. */
		if (spawn_foreground_tty(argv, &status) != 0) {
			fprintf(stderr, "sh: %s: %s\n", argv[0],
				strerror(errno));

			/* Reports successful completion. */
			return 0;
		}

		/* Checks the operation status. */
		if (WIFSIGNALED(status)) {
			fprintf(stderr, "%s\n",
				signal_message(WTERMSIG(status)));

			return wait_status_result(status);
		}

		/* Computes the function result. */
		function_result = wait_status_result(status);

		/* Returns the computed result. */
		return function_result;
	}

	/* Handles the command subshell condition. */
	if (!command_subshell) {
		/* Handles a failed posix spawnattr init operation. */
		if (posix_spawnattr_init(&attributes) != 0 ||
		    posix_spawnattr_setflags(&attributes,
					     POSIX_SPAWN_SETPGROUP) != 0 ||
		    posix_spawnattr_setpgroup(&attributes, 0) != 0) {
			fprintf(stderr,
				"sh: unable to prepare process group\n");

			/* Reports successful completion. */
			return 0;
		}
		attribute_pointer = &attributes;
	}
	error =
	    posix_spawn(&pid, argv[0], NULL, attribute_pointer, argv, environ);

	/* Handles the attribute pointer availability. */
	if (attribute_pointer != NULL)
		(void)posix_spawnattr_destroy(attribute_pointer);

	/* Handles an operation failure. */
	if (error != 0) {
		execution_status = error == ENOENT ? 127 : 126;
		fprintf(stderr, "sh: %s: %s\n", argv[0], strerror(error));

		/* Reports successful completion. */
		return 0;
	}

	/* Handles the command subshell condition. */
	if (!command_subshell)
		(void)setpgid(pid, pid);

	/* Handles the command background condition. */
	if (command_background) {
		remember_single_job(pid);
		printf("[%d]\n", (int)pid);

		/* Reports operation failure. */
		return 1;
	}

	/* Handles the command subshell condition. */
	if (command_subshell) {
		do

		/* Continue while the operation condition remains true. */
			waited = waitpid(pid, &status, 0);
		while (waited < 0 && errno == EINTR);

		/* Handles the waited condition. */
		if (waited < 0) {
			fprintf(stderr, "wait: %d\n", errno);

			/* Reports successful completion. */
			return 0;
		}
	} else if (!wait_foreground(pid, &status)) {
		fprintf(stderr, "wait: %d\n", errno);

		/* Reports successful completion. */
		return 0;
	}

	/* Checks the operation status. */
	if (WIFSIGNALED(status)) {
		fprintf(stderr, "%s\n", signal_message(WTERMSIG(status)));

		return wait_status_result(status);
	}

	/* Computes the function result. */
	function_result = wait_status_result(status);

	/* Returns the computed result. */
	return function_result;
}

/* Supports the spawn foreground tty operation. */
static int
spawn_foreground_tty(
	char *const argv[],
	int *status)
{
	int saved_errno_local;
	char release;
	int gate[2];
	int saved_errno;
	pid_t child, waited;
	pid_t shell_pgrp;
	ssize_t count;

	release = 'x';
	shell_pgrp = getpgrp();

	/*
 * posix_spawn() returns only after the child has executed.  A child
	 * which reads the terminal can therefore receive SIGTTIN before the
	 * parent makes its new process group foreground.  Hold the pre-exec
	 * child behind a close-on-exec pipe until the terminal hand-off is
	 * complete. */
	if (pipe2(gate, O_CLOEXEC) != 0)
		return -1;
	child = fork();

	/* Checks the child process state. */
	if (child < 0) {
		saved_errno_local = errno;

		(void)close(gate[0]);
		(void)close(gate[1]);
		errno = saved_errno_local;

		/* Reports operation failure. */
		return -1;
	}

	/* Checks the child process state. */
	if (child == 0) {
		(void)close(gate[1]);

		/* Handles a failed setpgid operation. */
		if (setpgid(0, 0) != 0)
			_exit(126);
		do

		/* Process each remaining element. */
			count = read(gate[0], &release, 1);
		while (count < 0 && errno == EINTR);
		(void)close(gate[0]);

		/* Checks the remaining item count. */
		if (count != 1)
			_exit(126);
		execve(argv[0], argv, environ);
		fprintf(stderr, "sh: %s: %s\n", argv[0], strerror(errno));
		(void)fflush(stderr);
		_exit(127);
	}

	(void)close(gate[0]);

	/* Handles a failed setpgid operation. */
	if (setpgid(child, child) != 0)
		goto setup_failed;

	/* Handles a failed shell tcsetpgrp operation. */
	if (shell_tcsetpgrp(STDIN_FILENO, child) != 0)
		goto setup_failed;
	count = shell_write_nosigpipe(gate[1], &release, 1);

	/* Checks the remaining item count. */
	if (count != 1) {
		/* Checks the remaining item count. */
		if (count >= 0)
			errno = EIO;
		goto setup_failed;
	}
	(void)close(gate[1]);
	do

	/* Continue while the operation condition remains true. */
		waited = waitpid(child, status, WUNTRACED);
	while (waited < 0 && errno == EINTR);

	/* Handles a failed shell tcsetpgrp operation. */
	if (shell_tcsetpgrp(STDIN_FILENO, shell_pgrp) != 0) {
		fprintf(stderr,
			"sh: cannot restore foreground process group: %s\n",
			strerror(errno));
	}

	/* Handles the waited condition. */
	if (waited < 0)
		return -1;

	/* Checks the operation status. */
	if (WIFSTOPPED(*status)) {
		remember_single_job(child);
		printf("[%d] stopped\n", (int)child);
	}

	/* Reports successful completion. */
	return 0;

setup_failed:
	saved_errno = errno;
	(void)close(gate[1]);
	(void)kill(child, SIGKILL);
	do

	/* Continue while the operation condition remains true. */
		waited = waitpid(child, status, 0);
	while (waited < 0 && errno == EINTR);
	(void)shell_tcsetpgrp(STDIN_FILENO, shell_pgrp);
	errno = saved_errno;

	/* Reports operation failure. */
	return -1;
}

/* Supports the shell write nosigpipe operation. */
static ssize_t
shell_write_nosigpipe(
	int descriptor,
	const void *buffer,
	size_t length)
{
	void (*previous)(int);
	ssize_t result;
	int saved_errno;

	previous = signal(SIGPIPE, (sighandler_t)SIG_IGN);

	/* Handles the previous condition. */
	if (previous == (sighandler_t)SIG_ERR)
		return -1;
	do

	/* Continue while the operation condition remains true. */
		result = write(descriptor, buffer, length);
	while (result < 0 && errno == EINTR);
	saved_errno = errno;

	/* Handles a failed signal operation. */
	if (signal(SIGPIPE, previous) == (sighandler_t)SIG_ERR)
		return -1;
	errno = saved_errno;

	/* Returns the computed result. */
	return result;
}

/* Supports the remember single job operation. */
static void
remember_single_job(
	pid_t process)
{
	remember_job(process, &process, 1);
}

/* Supports the signal message operation. */
static const char *
signal_message(
	int number)
{
	/* Dispatch the selected operation case. */
	switch (number) {
	case SIGHUP:
		/* Returns the computed result. */
		return "Hangup";
	case SIGINT:
		/* Returns the computed result. */
		return "Interrupt";
	case SIGQUIT:
		/* Returns the computed result. */
		return "Quit";
	case SIGILL:
		/* Returns the computed result. */
		return "Illegal instruction";
	case SIGTRAP:
		/* Returns the computed result. */
		return "Trace/BPT trap";
	case SIGABRT:
		/* Returns the computed result. */
		return "Abort trap";
	case SIGFPE:
		/* Returns the computed result. */
		return "Floating point exception";
	case SIGKILL:
		/* Returns the computed result. */
		return "Killed";
	case SIGBUS:
		/* Returns the computed result. */
		return "Bus error";
	case SIGSEGV:
		/* Returns the computed result. */
		return "Segmentation fault";
	case SIGPIPE:
		/* Returns the computed result. */
		return "Broken pipe";
	case SIGALRM:
		/* Returns the computed result. */
		return "Alarm clock";
	case SIGTERM:
		/* Returns the computed result. */
		return "Terminated";
	default:
		/* Returns the computed result. */
		return "Terminated by signal";
	}
}

/* Supports the wait foreground operation. */
static int
wait_foreground(
	pid_t pid,
	int *status)
{
	pid_t shell_pgrp;
	int terminal;
	int foreground_set;
	pid_t result;

	shell_pgrp = getpgrp();
	terminal = shell_controls_terminal();
	foreground_set = 0;

	/* Checks the terminal state. */
	if (terminal) {
		/* Handles a failed shell tcsetpgrp operation. */
		if (shell_tcsetpgrp(0, pid) == 0)
			foreground_set = 1;
		else
			fprintf(stderr,
				"sh: cannot foreground process %d: %s\n",
				(int)pid, strerror(errno));
	}
	do

	/* Continue while the operation condition remains true. */
		result = waitpid(pid, status, WUNTRACED);
	while (result < 0 && errno == EINTR);

	/* Handles a failed shell tcsetpgrp operation. */
	if (foreground_set && shell_tcsetpgrp(0, shell_pgrp) != 0) {
		fprintf(stderr,
			"sh: cannot restore foreground process group: %s\n",
			strerror(errno));
	}

	/* Checks the operation result. */
	if (result < 0)
		return 0;

	/* Checks the operation status. */
	if (WIFSTOPPED(*status)) {
		remember_single_job(pid);
		printf("[%d] stopped\n", (int)pid);
	}

	/* Reports operation failure. */
	return 1;
}

/* Supports the run shell script operation. */
static int
run_shell_script(
	int argc,
	char **argv,
	const char *path)
{
	int function_result;
	char *child[ARG_MAX + 1];
	int i;

	/* Validates the command-line arguments. */
	if (argc + 1 >= ARG_MAX)
		return 0;

	/* Process each remaining command-line operand. */
	child[0] = "/bin/sh";
	child[1] = (char *)path;
	for (i = 1; i < argc; i++)
		child[i + 1] = argv[i];
	child[argc + 1] = NULL;

	/* Obtains the run external result. */
	function_result = run_external(child);

	/* Returns the computed result. */
	return function_result;
}

/* Supports the run search path operation. */
static int
run_search_path(
	int argc,
	char **argv)
{
	int function_result;
	char candidate[256];
	const char *path;
	const char *cached;

	path = sh_var_get("PATH");

	/* Handles a failed sh hash sync path operation. */
	if (sh_hash_sync_path(path) != 0)
		return 0;
	cached = sh_hash_lookup(argv[0]);

	/* Handles the cached availability. */
	if (cached != NULL) {
		/* Obtains the run resolved result. */
		function_result = run_resolved(argc, argv, cached);

		/* Returns the computed result. */
		return function_result;
	}

	/* Validates the command-line arguments. */
	if (resolve_command(argv[0], candidate, sizeof(candidate))) {
		/* Validates the command-line arguments. */
		if (sh_hash_store(argv[0], candidate) != 0)
			return 0;

		/* Obtains the run resolved result. */
		function_result = run_resolved(argc, argv, candidate);

		/* Returns the computed result. */
		return function_result;
	}
	fprintf(stderr, "sh: %s: not found\n", argv[0]);
	execution_status = 127;

	/* Reports successful completion. */
	return 0;
}

/* Supports the run resolved operation. */
static int
run_resolved(
	int argc,
	char **argv,
	const char *path)
{
	int function_result;
	char *child[ARG_MAX + 1];
	int index;

	/* Handles a failed executable file operation. */
	if (!is_executable_file(path)) {
		execution_status = errno == ENOENT ? 127 : 126;
		fprintf(stderr, "sh: %s: %s\n", path, strerror(errno));

		/* Reports successful completion. */
		return 0;
	}

	/* Handles a failed elf operation. */
	if (!is_elf(path)) {
		/* Obtains the run shell script result. */
		function_result = run_shell_script(argc, argv, path);

		/* Returns the computed result. */
		return function_result;
	}

	/* Process each remaining command-line operand. */
	child[0] = (char *)path;
	for (index = 1; index < argc; index++)
		child[index] = argv[index];
	child[argc] = NULL;

	/* Obtains the run external result. */
	function_result = run_external(child);

	/* Returns the computed result. */
	return function_result;
}

/* Supports the pipeline child operation. */
static int
pipeline_child(
	struct pipeline_command *item)
{
	int function_result;

	/* Handles a failed redirections apply operation. */
	if (!redirections_apply(item->redirects, item->redirect_count, NULL,
				NULL))

		/* Reports successful completion. */
		return 0;
	command_subshell = 1;
	command_background = 0;

	/* Obtains the command argv result. */
	function_result = command_argv(item->argc, item->argv);

	/* Returns the computed result. */
	return function_result;
}

/* Supports the shell lookup operation. */
static const char *
shell_lookup(
	void *context,
	const char *name)
{
	const char *function_result;

	(void)context;

	/* Obtains the sh var get result. */
	function_result = sh_var_get(name);

	/* Returns the computed result. */
	return function_result;
}

/* Supports the shell assign operation. */
static int
shell_assign(
	void *context,
	const char *name,
	const char *value)
{
	int function_result;

	(void)context;

	/* Obtains the sh var set result. */
	function_result = sh_var_set(name, value, -1);

	/* Returns the computed result. */
	return function_result;
}

/* Supports the shell signal handler operation. */
static void
shell_signal_handler(
	int signal_number)
{
	/* Handles the signal number condition. */
	if (signal_number > 0 && signal_number < SHELL_SIGNAL_MAX)
		trap_pending[signal_number] = 1;
}

/* Supports the shell command substitute operation. */
static int
shell_command_substitute(
	void *context,
	const char *source,
	char **result)
{
	char chunk[256];
	ssize_t count;
	char *larger;
	int descriptors[2];
	pid_t child;
	char *output;
	size_t length, capacity;
	int status;

	output = NULL;
	length = 0;
	capacity = 0;
	(void)context;
	*result = NULL;
	/* Handles a failed pipe operation. */
	if (pipe(descriptors) != 0)
		return 0;
	child = fork();

	/* Checks the child process state. */
	if (child < 0) {
		(void)close(descriptors[0]);
		(void)close(descriptors[1]);

		/* Reports successful completion. */
		return 0;
	}

	/* Checks the child process state. */
	if (child == 0) {
		(void)close(descriptors[0]);

		/* Handles a failed dup2 operation. */
		if (dup2(descriptors[1], STDOUT_FILENO) < 0)
			_exit(1);
		(void)close(descriptors[1]);
		command_subshell = 1;
		(void)command((char *)source);
		(void)fflush(NULL);
		_exit(shell_status);
	}
	(void)close(descriptors[1]);

	/* Continue until the operation reaches a terminal state. */
	for (;;) {
		count = read(descriptors[0], chunk, sizeof(chunk));

		/* Checks the remaining item count. */
		if (count < 0) {
			/* Handles the reported system error. */
			if (errno == EINTR)
				continue;
			free(output);
			(void)close(descriptors[0]);
			(void)waitpid(child, NULL, 0);

			/* Reports successful completion. */
			return 0;
		}

		/* Checks the remaining item count. */
		if (count == 0)
			break;

		/* Checks the current data length. */
		if (length + (size_t)count + 1U < length) {
			free(output);
			(void)close(descriptors[0]);
			(void)waitpid(child, NULL, 0);

			/* Reports successful completion. */
			return 0;
		}

		/* Checks the current data length. */
		if (length + (size_t)count + 1U > capacity) {
			/* Process each remaining element. */
			capacity = capacity == 0 ? 512U : capacity;
			while (capacity < length + (size_t)count + 1U)
				capacity *= 2U;
			larger = realloc(output, capacity);

			/* Handles the larger availability. */
			if (larger == NULL) {
				free(output);
				(void)close(descriptors[0]);
				(void)waitpid(child, NULL, 0);

				/* Reports successful completion. */
				return 0;
			}
			output = larger;
		}
		memcpy(output + length, chunk, (size_t)count);
		length += (size_t)count;
	}
	(void)close(descriptors[0]);

	/* Handles a failed waitpid operation. */
	if (waitpid(child, &status, 0) < 0) {
		free(output);

		/* Reports successful completion. */
		return 0;
	}
	while (length != 0 && output[length - 1U] == '\n')
		length--;

	/* Handles the output availability. */
	if (output == NULL) {
		output = malloc(1U);

		/* Handles the output availability. */
		if (output == NULL)
			return 0;
	}
	output[length] = '\0';
	*result = output;
	/* Reports operation failure. */
	return 1;
}
