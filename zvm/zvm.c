#include <stdio.h>
#include <stdlib.h>
#include <spawn.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>
#include "EXTERN.h"
#include "perl.h"

extern char **environ;

/**
 * @brief Create an fdMap for spawn based on open files that are to be inherited
 *
 * @param fdExclude Possible pipe ends that will be used by spawn()
 * @returns Array of FDs
 */
static int
build_fd_map(int **fdMap, int *fdExclude)
{
    int maxOpen = sysconf(_SC_OPEN_MAX),
        maxFD = 2,
        nFD = 3,
        count;

    /**
     * Count the number of open file descriptors that aren't:
     * - Not stdin, stdout, stderr
     * - Part of a pipe
     * - Flagged to be closed on fork/exec
     */
    for (count = 3; count < maxOpen; count++) {
        if (!fdExclude || (count != fdExclude[0] && count != fdExclude[1])) {
            int flags = fcntl(count, F_GETFD);
            if (flags > -1) {
                if (!(flags & FD_CLOEXEC) && !(flags & FD_CLOFORK)) {
                    maxFD = count;
                    nFD++;
                }
            }
        }
    }

    /**
     * Allocate and populate fd map
     */
    *fdMap = malloc(nFD * sizeof(int));
    if (fdMap) {
        memset(*fdMap, -1, (nFD * sizeof(int)));
        for (count = 0; count <= maxFD; count++) {
            if (fdExclude == NULL || (count != fdExclude[0] && count != fdExclude[1])) {
                int flags = fcntl(count, F_GETFD);
                if (flags > -1) {
                    if (!(flags & FD_CLOEXEC) && !(flags & FD_CLOFORK)) 
                        (*fdMap)[count] = count;
                }
            }
        }
    } else {
        perror("Malloc");
        abort();
    }
    return nFD;
}


static inline void
S_spawn_failed(pTHX_ const char *cmd)
{
    const int e = errno;

    ck_warner(packWARN(WARN_EXEC), "Can't exec \"%s\": %s",
              cmd, Strerror(e));
}

static inline int 
chgfdccsid(int fd, unsigned short ccsid)
{
  attrib_t attr;
  memset(&attr, 0, sizeof(attr));
  attr.att_filetagchg = 1;
  attr.att_filetag.ft_ccsid = ccsid;
  if (ccsid != FT_BINARY)
    attr.att_filetag.ft_txtflag = 1;
  return __fchattr(fd, &attr, sizeof(attr));
}

/**
 * @brief Copy the parent's environment vars to the child's
 *
 * @returns Array of environment variables
 */
static char **
copy_env() 
{
    int iEnv;
    char **newEnv,
         **oldEnv;
    size_t lEnv;

    oldEnv = environ;

    for (iEnv=0; oldEnv[iEnv] != NULL; iEnv++);

    lEnv = sizeof(uintptr_t) * (iEnv + 1);
    newEnv = malloc(lEnv);
    memset(newEnv, 0, lEnv);

    for (iEnv = 0; oldEnv[iEnv] != NULL; iEnv++)
        newEnv[iEnv] = strdup(oldEnv[iEnv]);

    return newEnv;
}

/**
 * @brief Free the array of environment variables inherited by child
 *
 * @param[in] env Array of environment variables
 */
static void
free_env(char **env) 
{
    int iEnv;

    for (iEnv=0; env[iEnv] != NULL; iEnv++)
        free(env[iEnv]);

    free(env);
}

PerlIO *
Perl_my_popen(pTHX_ const char *cmd, const char *mode)
{
    PerlIO *res;
    SV *sv;
    int p[2];
    I32 pid, newfd;
    const char *argv[4] = { NULL, "-c", NULL, NULL };
    char **env;
    struct inheritance in;
    int *fdMap, nFd;
char *debug = getenv("PERL_DEBUG");

    if (__isVM()) 
        argv[0] = "/bin/sh";
    else
        argv[0] = "bash";

    if (TAINTING_get) {
        taint_env();
        taint_proper("Insecure %s%s", "EXEC");
    }

    if (pipe(p) < 0)
        return NULL;

    nFd = build_fd_map(&fdMap, p);

    if (*mode == 'w') {
        fdMap[0] = p[0];
        fdMap[1] = 1;
        fdMap[2] = 2;
    } else {
        fdMap[0] = 0;
        fdMap[1] = p[1];
        fdMap[2] = 2;
    }

    if (!__isVM()) {
        chgfdccsid(p[0], 819);
        chgfdccsid(p[1], 819);
    }

    argv[2] = cmd;

    env = copy_env();
    memset(&in, 0, sizeof(in));
    in.flags = SPAWN_SETGROUP | SPAWN_SETSIGDEF;
    in.pgroup = SPAWN_NEWPGROUP;                

if (debug != NULL) {
fprintf(stderr, "%s:%d - mode: %c p[0]: %d p[1]: %d\nargv: ",__func__,__LINE__,*mode,fdMap[0],fdMap[1]);
for (int i = 0; argv[i] != NULL; i++) 
    fprintf(stderr,"[%s] ", argv[i]);
fprintf(stderr,"\nenv: ");
for (int i = 0; env[i] != NULL; i++) 
    fprintf(stderr,"[%s] ", env[i]);
fprintf(stderr,"\n");
}
    pid = spawnp(argv[0], nFd, fdMap, &in, argv, (const char **)env);

    free_env(env);

    if (pid < 0) {
        close(p[0]);
        close(p[1]);
        return NULL;
    }

    if (*mode == 'r') {
        close(p[1]);
        newfd = p[0];
    } else {
        close(p[0]);
        newfd = p[1];
    }

    free(fdMap);

    sv_setiv(*av_fetch(PL_fdpid, newfd, TRUE), pid);
    PL_forkprocess = pid;

    return PerlIO_fdopen(newfd, mode);
}

PerlIO *
zvm_popenlist(pTHX_ const char *mode, int n, SV **args)
{
    PerlIO *res;
    SV *sv;
    int p[2], *fdMap, nFd;
    const char **argv;
    char **env;
    I32 pid, newfd;
    struct inheritance in;
char *debug = getenv("PERL_DEBUG");

    Newx(argv, n, const char *);
    SAVEFREEPV(argv);

    for (int i = 0; i < n; i++) {
        char *arg = savepv(SvPV_nolen_const(args[i]));
        SAVEFREEPV(arg);
        argv[i] = arg;
    }
    argv[n] = NULL;

    if (pipe(p) < 0)
        return NULL;

    nFd = build_fd_map(&fdMap, p);

    if (*mode == 'w') {
        fdMap[0] = p[0];
        fdMap[1] = 1;
        fdMap[2] = 2;
    } else {
        fdMap[0] = 0;
        fdMap[1] = p[1];
        fdMap[2] = 2;
    }

    if (!__isVM()) {
        chgfdccsid(p[0], 819);
        chgfdccsid(p[1], 819);
    }

    env = copy_env();

    memset(&in, 0, sizeof(in));
    in.flags = SPAWN_SETGROUP | SPAWN_SETSIGDEF;
    in.pgroup = SPAWN_NEWPGROUP;                

if (debug != NULL) {
DEBUG_PRINT("nargs: %d cmd: %s", n, argv[0]);
for (int i = 1; argv[i] != NULL; i++)
 fprintf(stderr, "[%s] ", argv[i]);
fprintf(stderr, "\n");
}
    pid = spawnp(argv[0], nFd, fdMap, &in, argv, (const char **)env);

    free_env(env);

    if (pid < 0) {
        close(p[0]);
        close(p[1]);
        return NULL;
    }

    if (*mode == 'r') {
        close(p[1]);
        newfd = p[0];
    } else {
        close(p[0]);
        newfd = p[1];
    }

    free(fdMap);

    sv_setiv(*av_fetch(PL_fdpid, newfd, TRUE), pid);
    PL_forkprocess = pid;

    return PerlIO_fdopen(newfd, mode);
}

int
Perl_do_aspawn(pTHX_ SV* really, SV** mark, SV** sp)
{
    int res;

    assert(sp >= mark);
    ENTER;
    {
        int *fdMap, nFd;
        const char **argv, **a;
        const char *tmps = NULL;
        char **env;
        I32 pid = 0, result, status;
        struct inheritance in;
char *debug = getenv("PERL_DEBUG");

        Newx(argv, sp - mark + 1, const char *);
        SAVEFREEPV(argv);
        a = argv;
        while (++mark <= sp) {
            if (*mark) {
                char *arg = savepv(SvPV_nolen_const(*mark));
                SAVEFREEPV(arg);
                *a++ = arg;
            } else
                *a++ = "";
        }
        *a = NULL;
        if (really) {
            tmps = savepv(SvPV_nolen_const(really));
            SAVEFREEPV(tmps);
        }
        if ((!really && argv[0] && *argv[0] != '/') ||
            (really && *tmps != '/'))		/* will execvp use PATH? */
            TAINT_ENV();		/* testing IFS here is overkill, probably */
        PERL_FPU_PRE_EXEC

        env = copy_env();

        memset(&in, 0, sizeof(in));
        in.flags = SPAWN_SETGROUP | SPAWN_SETSIGDEF;
        in.pgroup = SPAWN_NEWPGROUP;                

        nFd = build_fd_map(&fdMap, NULL);

        env = copy_env();

        memset(&in, 0, sizeof(in));
        in.flags = SPAWN_SETGROUP | SPAWN_SETSIGDEF;
        in.pgroup = SPAWN_NEWPGROUP;                

        if (really && *tmps) {
if (debug != NULL)
fprintf(stderr, "%s:%d - cmd: %s\n", __func__, __LINE__, tmps);
            pid = spawnp(tmps, nFd, fdMap, &in, argv, (const char **)env);
        } else if (argv[0]) {
if (debug != NULL) {
fprintf(stderr, "%s:%d - nfd: %d fdMap[1]: %d argv ", __func__, __LINE__, nFd, fdMap[1]);
for (int i = 0; argv[i] != NULL; i++) 
    fprintf(stderr, "[%s] ", argv[i]);
fprintf(stderr, "\n%s:%d - env: ", __func__, __LINE__);
for (int i = 0; env[i] != NULL; i++) 
    fprintf(stderr, "[%s] ", env[i]);
fprintf(stderr, "\n");
fprintf(stderr, "\n%s:%d - fdmap: ", __func__, __LINE__);
for (int i = 0; i < nFd; i++) 
    fprintf(stderr, "[%d] ", fdMap[i]);
fprintf(stderr, "\n");
}
            pid = spawnp(argv[0], nFd, fdMap, &in, argv, (const char **)env);
if (debug != NULL)            
fprintf(stderr, "%s:%d - pid: %d error: %s\n", __func__, __LINE__, pid, (pid < 0 ? strerror(errno) : "no error"));
        } else {
            SETERRNO(ENOENT, RMS_FNF);
        }
        PERL_FPU_POST_EXEC

        if (pid < 0) {
            S_spawn_failed(aTHX_ (really ? tmps : argv[0] ? argv[0] : ""));
            res = -1;
        } else {
            errno = 0;
            do {
                result = waitpid(pid, &status, 0);
            } while (result == -1 && errno == EINTR);
if (debug != NULL)
fprintf(stderr, "%s:%d - result: %d status: %x errnno: %d\n", __func__, __LINE__, result, res, errno);
            res = (result == -1 ? -1 : status);
        }

        free_env(env);
        free(fdMap);
    }
    LEAVE;

    return res;
}

int
Perl_do_spawn(pTHX_ const char *incmd)
{
    const char **argv, **a;
    char *s, *buf, *cmd, **env;
    /* Make a copy so we can change it */
    const Size_t cmdlen = strlen(incmd) + 1;
    struct inheritance in;
    int *fdMap, nFd, result, status, pid, res;
    char *sh;

    if (__isVM()) 
        sh = "/bin/sh";
    else
        sh = "bash";

    PERL_ARGS_ASSERT_DO_EXEC3;

    ENTER;
    Newx(buf, cmdlen, char);
    SAVEFREEPV(buf);

    nFd = build_fd_map(&fdMap, NULL);

    cmd = buf;
    memcpy(cmd, incmd, cmdlen);

    while (*cmd && isSPACE(*cmd))
        cmd++;

    /* see if there are shell metacharacters in it */

    if (*cmd == '.' && isSPACE(cmd[1]))
        goto doshell;

    if (strBEGINs(cmd,"exec") && isSPACE(cmd[4]))
        goto doshell;

    s = cmd;
    while (isWORDCHAR(*s))
        s++;	/* catch VAR=val gizmo */
    if (*s == '=')
        goto doshell;

    for (s = cmd; *s; s++) {
        if (*s != ' ' && !isALPHA(*s) &&
            memCHRs("$&*(){}[]'\";\\|?<>~`\n",*s)) {
            if (*s == '\n' && !s[1]) {
                *s = '\0';
                break;
            }
            /* handle the 2>&1 construct at the end */
            if (*s == '>' && s[1] == '&' && s[2] == '1'
                && s > cmd + 1 && s[-1] == '2' && isSPACE(s[-2])
                && (!s[3] || isSPACE(s[3])))
            {
                const char *t = s + 3;

                while (*t && isSPACE(*t))
                    ++t;
                fdMap[2] = fdMap[1];        // dup stdout to stderr
                if (!*t) {
                    s[-2] = '\0';
                    break;
                }
            }
          doshell:
            PERL_FPU_PRE_EXEC
            const char *shcmd[4] = { sh, "-c", cmd, NULL };
            memset(&in, 0, sizeof(in));
            in.flags = SPAWN_SETGROUP | SPAWN_SETSIGDEF;
            in.pgroup = SPAWN_NEWPGROUP;                
            env = copy_env();

            pid = spawnp(shcmd[0], nFd, fdMap, &in, shcmd, (const char **)env);
            PERL_FPU_POST_EXEC
            if (pid < 0) {
                S_spawn_failed(aTHX_ PL_sh_path);
                res = -1;
            } else {
                do {
                    result = wait4pid(pid, &status, 0);
                } while (result == -1 && errno == EINTR);
                res = (result == -1 ? -1 : status);
            }

            free_env(env);
            free(fdMap);
            goto leave;
        }
    }

    Newx(argv, (s - cmd) / 2 + 2, const char*);
    SAVEFREEPV(argv);
    cmd = savepvn(cmd, s-cmd);
    SAVEFREEPV(cmd);
    a = argv;
    for (s = cmd; *s;) {
        while (isSPACE(*s))
            s++;
        if (*s)
            *(a++) = s;
        while (*s && !isSPACE(*s))
            s++;
        if (*s)
            *s++ = '\0';
    }
    *a = NULL;
    if (argv[0]) {
        PERL_FPU_PRE_EXEC
        memset(&in, 0, sizeof(in));
        in.flags = SPAWN_SETGROUP | SPAWN_SETSIGDEF;
        in.pgroup = SPAWN_NEWPGROUP;                
        env = copy_env();

        pid = spawnp(argv[0], nFd, fdMap, &in, argv, (const char **)env);
        if ((pid < 0) && (errno == ENOEXEC)) {
            const char *shcmd[4] = { sh, "-c", cmd, NULL };
            pid = spawnp(shcmd[0], nFd, fdMap, &in, shcmd, (const char **)env);
        }
        PERL_FPU_POST_EXEC
        if (pid < 0) {
            S_spawn_failed(aTHX_ PL_sh_path);
            STATUS_NATIVE_CHILD_SET(-1);
            res = -1;
        } else {
            do {
                result = waitpid(pid, &status, 0);
            } while (result == -1 && errno == EINTR);
            res = (result == -1 ? -1 : status);
        }

        free_env(env);
        free(fdMap);
    }
leave:
    LEAVE;
    return res;
}
