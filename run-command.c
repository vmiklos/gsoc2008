#include "cache.h"
#include "run-command.h"
#include "exec_cmd.h"

static inline void close_pair(int fd[2])
{
	close(fd[0]);
	close(fd[1]);
}

static inline void dup_devnull(int to)
{
	int fd = open("/dev/null", O_RDWR);
	dup2(fd, to);
	close(fd);
}

int start_command(struct child_process *cmd)
{
	int need_in, need_out, need_err;
	int fdin[2], fdout[2], fderr[2];

	/*
	 * In case of errors we must keep the promise to close FDs
	 * that have been passed in via ->in and ->out.
	 */

	need_in = !cmd->no_stdin && cmd->in < 0;
	if (need_in) {
		if (pipe(fdin) < 0) {
			if (cmd->out > 0)
				close(cmd->out);
			return -ERR_RUN_COMMAND_PIPE;
		}
		cmd->in = fdin[1];
	}

	need_out = !cmd->no_stdout
		&& !cmd->stdout_to_stderr
		&& cmd->out < 0;
	if (need_out) {
		if (pipe(fdout) < 0) {
			if (need_in)
				close_pair(fdin);
			else if (cmd->in)
				close(cmd->in);
			return -ERR_RUN_COMMAND_PIPE;
		}
		cmd->out = fdout[0];
	}

	need_err = !cmd->no_stderr && cmd->err < 0;
	if (need_err) {
		if (pipe(fderr) < 0) {
			if (need_in)
				close_pair(fdin);
			else if (cmd->in)
				close(cmd->in);
			if (need_out)
				close_pair(fdout);
			else if (cmd->out)
				close(cmd->out);
			return -ERR_RUN_COMMAND_PIPE;
		}
		cmd->err = fderr[0];
	}

	trace_argv_printf(cmd->argv, "trace: run_command:");

#ifndef __MINGW32__
	fflush(NULL);
	cmd->pid = fork();
	if (!cmd->pid) {
		if (cmd->no_stdin)
			dup_devnull(0);
		else if (need_in) {
			dup2(fdin[0], 0);
			close_pair(fdin);
		} else if (cmd->in) {
			dup2(cmd->in, 0);
			close(cmd->in);
		}

		if (cmd->no_stderr)
			dup_devnull(2);
		else if (need_err) {
			dup2(fderr[1], 2);
			close_pair(fderr);
		}

		if (cmd->no_stdout)
			dup_devnull(1);
		else if (cmd->stdout_to_stderr)
			dup2(2, 1);
		else if (need_out) {
			dup2(fdout[1], 1);
			close_pair(fdout);
		} else if (cmd->out > 1) {
			dup2(cmd->out, 1);
			close(cmd->out);
		}

		if (cmd->dir && chdir(cmd->dir))
			die("exec %s: cd to %s failed (%s)", cmd->argv[0],
			    cmd->dir, strerror(errno));
		if (cmd->env) {
			for (; *cmd->env; cmd->env++) {
				if (strchr(*cmd->env, '='))
					putenv((char*)*cmd->env);
				else
					unsetenv(*cmd->env);
			}
		}
		if (cmd->git_cmd) {
			execv_git_cmd(cmd->argv);
		} else {
			execvp(cmd->argv[0], (char *const*) cmd->argv);
		}
		die("exec %s failed.", cmd->argv[0]);
	}
#else
	int s0 = -1, s1 = -1, s2 = -1;	/* backups of stdin, stdout, stderr */
	const char **sargv = cmd->argv;
	char **env = environ;

	if (cmd->no_stdin) {
		s0 = dup(0);
		dup_devnull(0);
	} else if (need_in) {
		s0 = dup(0);
		dup2(fdin[0], 0);
	} else if (cmd->in) {
		s0 = dup(0);
		dup2(cmd->in, 0);
	}

	if (cmd->no_stderr) {
		s2 = dup(2);
		dup_devnull(2);
	} else if (need_err) {
		s2 = dup(2);
		dup2(fderr[1], 2);
	}

	if (cmd->no_stdout) {
		s1 = dup(1);
		dup_devnull(1);
	} else if (cmd->stdout_to_stderr) {
		s1 = dup(1);
		dup2(2, 1);
	} else if (need_out) {
		s1 = dup(1);
		dup2(fdout[1], 1);
	} else if (cmd->out > 1) {
		s1 = dup(1);
		dup2(cmd->out, 1);
	}

	if (cmd->dir)
		die("chdir in start_command() not implemented");
	if (cmd->env) {
		env = copy_environ();
		for (; *cmd->env; cmd->env++)
			env = env_setenv(env, *cmd->env);
	}

	if (cmd->git_cmd) {
		cmd->argv = prepare_git_cmd(cmd->argv);
	}

	cmd->pid = mingw_spawnvpe(cmd->argv[0], cmd->argv, env);

	if (cmd->env)
		free_environ(env);
	if (cmd->git_cmd)
		free(cmd->argv);

	cmd->argv = sargv;
	if (s0 >= 0)
		dup2(s0, 0), close(s0);
	if (s1 >= 0)
		dup2(s1, 1), close(s1);
	if (s2 >= 0)
		dup2(s2, 2), close(s2);
#endif

	if (cmd->pid < 0) {
		if (need_in)
			close_pair(fdin);
		else if (cmd->in)
			close(cmd->in);
		if (need_out)
			close_pair(fdout);
		else if (cmd->out)
			close(cmd->out);
		if (need_err)
			close_pair(fderr);
		return -ERR_RUN_COMMAND_FORK;
	}

	if (need_in)
		close(fdin[0]);
	else if (cmd->in)
		close(cmd->in);

	if (need_out)
		close(fdout[1]);
	else if (cmd->out)
		close(cmd->out);

	if (need_err)
		close(fderr[1]);

	return 0;
}

static int wait_or_whine(pid_t pid)
{
	for (;;) {
		int status, code;
		pid_t waiting = waitpid(pid, &status, 0);

		if (waiting < 0) {
			if (errno == EINTR)
				continue;
			error("waitpid failed (%s)", strerror(errno));
			return -ERR_RUN_COMMAND_WAITPID;
		}
		if (waiting != pid)
			return -ERR_RUN_COMMAND_WAITPID_WRONG_PID;
		if (WIFSIGNALED(status))
			return -ERR_RUN_COMMAND_WAITPID_SIGNAL;

		if (!WIFEXITED(status))
			return -ERR_RUN_COMMAND_WAITPID_NOEXIT;
		code = WEXITSTATUS(status);
		if (code)
			return -code;
		return 0;
	}
}

int finish_command(struct child_process *cmd)
{
	return wait_or_whine(cmd->pid);
}

int run_command(struct child_process *cmd)
{
	int code = start_command(cmd);
	if (code)
		return code;
	return finish_command(cmd);
}

static void prepare_run_command_v_opt(struct child_process *cmd,
				      const char **argv,
				      int opt)
{
	memset(cmd, 0, sizeof(*cmd));
	cmd->argv = argv;
	cmd->no_stdin = opt & RUN_COMMAND_NO_STDIN ? 1 : 0;
	cmd->git_cmd = opt & RUN_GIT_CMD ? 1 : 0;
	cmd->stdout_to_stderr = opt & RUN_COMMAND_STDOUT_TO_STDERR ? 1 : 0;
}

int run_command_v_opt(const char **argv, int opt)
{
	struct child_process cmd;
	prepare_run_command_v_opt(&cmd, argv, opt);
	return run_command(&cmd);
}

int run_command_v_opt_cd(const char **argv, int opt, const char *dir)
{
	struct child_process cmd;
	prepare_run_command_v_opt(&cmd, argv, opt);
	cmd.dir = dir;
	return run_command(&cmd);
}

int run_command_v_opt_cd_env(const char **argv, int opt, const char *dir, const char *const *env)
{
	struct child_process cmd;
	prepare_run_command_v_opt(&cmd, argv, opt);
	cmd.dir = dir;
	cmd.env = env;
	return run_command(&cmd);
}

#ifdef __MINGW32__
static __stdcall unsigned run_thread(void *data)
{
	struct async *async = data;
	return async->proc(async->fd_for_proc, async->data);
}
#endif

int start_async(struct async *async)
{
	int pipe_out[2];

	if (pipe(pipe_out) < 0)
		return error("cannot create pipe: %s", strerror(errno));
	async->out = pipe_out[0];

#ifndef __MINGW32__
	/* Flush stdio before fork() to avoid cloning buffers */
	fflush(NULL);

	async->pid = fork();
	if (async->pid < 0) {
		error("fork (async) failed: %s", strerror(errno));
		close_pair(pipe_out);
		return -1;
	}
	if (!async->pid) {
		close(pipe_out[0]);
		exit(!!async->proc(pipe_out[1], async->data));
	}
	close(pipe_out[1]);
#else
	async->fd_for_proc = pipe_out[1];
	async->tid = (HANDLE) _beginthreadex(NULL, 0, run_thread, async, 0, NULL);
	if (!async->tid) {
		error("cannot create thread: %s", strerror(errno));
		close_pair(pipe_out);
		return -1;
	}
#endif
	return 0;
}

int finish_async(struct async *async)
{
#ifndef __MINGW32__
	int ret = 0;

	if (wait_or_whine(async->pid))
		ret = error("waitpid (async) failed");
#else
	DWORD ret = 0;
	if (WaitForSingleObject(async->tid, INFINITE) != WAIT_OBJECT_0)
		ret = error("waiting for thread failed: %lu", GetLastError());
	else if (!GetExitCodeThread(async->tid, &ret))
		ret = error("cannot get thread exit code: %lu", GetLastError());
	CloseHandle(async->tid);
#endif
	return ret;
}
