#include "process.h"
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "common.h"
#include "fs.h"
#include "log.h"
#include "meta.h"
#include "users.h"

static void prepare_box(const char *bin_path, int uid, char *command)
{
	pid_t res;

	check_sys(res = fork());
	if (res > 0) {
		int status;

		waitpid(res, &status, 0);
		if (!WIFEXITED(status)) {
			log_err("Could not %s the sandbox (%d)", command, status);
			exit(1);
		}
		if (WEXITSTATUS(status)) {
			log_err("Could not %s the sandbox (exit code %d)", command, WEXITSTATUS(status));
			exit(1);
		}
		return;
	}
	fd_to_null(1);
	fd_to_null(2);
	check_sys(execl(bin_path, bin_path,
			"--silent",
			ssprintf("--box-id=%d", uid),
			ssprintf("--%s", command),
			NULL));
}

static void redirect_std(const char *meta_dir)
{
	int fd;
	char *fname;

	fname = ssprintf("%s/output", meta_dir);
	check_sys(fd = creat(fname, 0666));
	sfree(fname);
	check_sys(dup2(fd, 1));
	check_sys(dup2(fd, 2));
	close(fd);
	fd_to_null(0);
}

static void reassign_pipe(int pin, int pout)
{
	/* The following is safe: we have 4 pipe descriptors, the first one
	 * in each pair is for reading. Worst case, dup2 will be a noop for
	 * some of the fds but we won't overwrite a fd that we still need to
	 * reassign. */
	check_sys(dup2(pin, 3));
	check_sys(dup2(pout, 4));
	close_fds(4);
}

static pid_t run_box(const char *bin_dir, const char *bin_path, const char *meta_dir, int uid,
		     int max_secs, int pin, int pout)
{
	pid_t res;

	check_sys(res = fork());
	if (res > 0)
		return res;

	redirect_std(meta_dir);
	reassign_pipe(pin, pout);

	check_sys(execl(bin_path, bin_path,
			"--silent",
			ssprintf("--box-id=%d", uid),
			ssprintf("--wall-time=%d", max_secs),
			"--mem=50000",
			ssprintf("--meta=%s/status", meta_dir),
			"--inherit-fds",
			"--no-default-dirs",
			"--dir=box=./box",
			ssprintf("--dir=etc=%s/root/etc", bin_dir),
			ssprintf("--dir=lib=%s/root/lib", bin_dir),
			ssprintf("--dir=lib64=%s/root/lib64", bin_dir),
			ssprintf("--dir=usr=%s/root/usr", bin_dir),
			"--dir=proc=proc:fs",
			"--env=HOME=/box",
			"--run",
			"/usr/bin/python",
			"program.py",
			NULL));
	return 0; /* can't happen but needed to shut up gcc */
}

static pid_t run_master(const char *bin_dir, int pin, int pout)
{
	pid_t res;
	char *master;

	check_sys(res = fork());
	if (res > 0)
		return res;

	master = ssprintf("%s/mock_master.py", bin_dir);
	reassign_pipe(pin, pout);
	check_sys(execl(master, master, NULL));
	return 0; /* can't happen but needed to shut up gcc */
}

static void control(const char *bin_dir, const char *login, int uid, const char *prg, int max_secs)
{
	char *meta_dir, *bin_path;
	pid_t res, pid_master, pid_box;
	int pfd[4];

	check_sys(res = fork());
	if (res > 0)
		return;

	bin_path = ssprintf("%s/isolate.bin", bin_dir);
	prepare_box(bin_path, uid, "cleanup");
	prepare_box(bin_path, uid, "init");

	meta_dir = meta_new(login);
	log_info("meta directory %s", meta_dir);
	if (meta_cp_prg(prg, meta_dir, uid) < 0) {
		log_err("cannot copy %s", prg);
		sfree(bin_path);
		return;
	}

	check_sys(pipe(pfd));
	check_sys(pipe(pfd + 2));

	pid_master = run_master(bin_dir, pfd[0], pfd[3]);
	log_info("master pid %d started", pid_master);

	pid_box = run_box(bin_dir, bin_path, meta_dir, uid, max_secs, pfd[2], pfd[1]);
	log_info("box pid %d started", pid_box);

	while (1) {
		res = wait(NULL);
		if (res == pid_master) {
			log_info("master pid %d finished", pid_master);
			kill(pid_box, SIGTERM);
			break;
		} else if (res == pid_box) {
			log_info("box pid %d finished", pid_box);
			kill(pid_master, SIGTERM);
			break;
		}
	}

	exit(0);
}

void proc_start(const char *bin_dir, const char *login, const char *prg, int max_secs)
{
	int uid = usr_get_uid(login);

	log_info("starting for user %s, program %s", login, prg);
	if (uid < 0) {
		log_err("uknown user %s", login);
		return;
	}

	if (meta_running(login)) {
		log_err("user %s: already running", login);
		return;
	}

	control(bin_dir, login, uid, prg, max_secs);
}
