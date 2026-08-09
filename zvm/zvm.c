#include <stdio.h>
#include <stdlib.h>
#include <spawn.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include "EXTERN.h"
#include "perl.h"

extern char **environ;

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
void
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
    int fdMap[3];
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

if (debug != NULL)
fprintf(stderr, "%s:%d - cmd: %s - p[0]: %d p[1]: %d argv[0]: %s argv[1]: %s argv[2]: %s\n",__func__,__LINE__,cmd,fdMap[0],fdMap[1],argv[0],argv[1],argv[2]);
    pid = spawnp(argv[0], 3, fdMap, &in, argv, (const char **)env);

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

    sv_setiv(*av_fetch(PL_fdpid, newfd, TRUE), pid);
    PL_forkprocess = pid;

    return PerlIO_fdopen(newfd, mode);
}

PerlIO *
zvm_popenlist(pTHX_ const char *mode, int n, SV **args)
{
    PerlIO *res;
    SV *sv;
    int p[2], fdMap[3];
    const char **argv;
    const char *cmd[2] = { NULL, "-c" };
    char **env;
    I32 pid, newfd;
    struct inheritance in;
char *debug = getenv("PERL_DEBUG");

    if (__isVM()) 
        cmd[0] = "/bin/sh";
    else
        cmd[0] = "bash";

if (debug != NULL) fprintf(stderr,"%s:%d\n", __func__, __LINE__);
    argv = __alloca(sizeof(uintptr_t) * (n + 3));
    argv[0] = cmd[0];
    argv[1] = cmd[1];
    for (int i = 0; i < n; i++)
        argv[i+2] = SvPV_nolen(args[i]);
    argv[n + 2] = NULL;

    PERL_SET_THX(aTHX);

    if (TAINTING_get) {
        taint_env();
        taint_proper("Insecure %s%s", "EXEC");
    }

    if (pipe(p) < 0)
        return NULL;

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

if (debug != NULL)
fprintf(stderr, "%s:%d - cmd: %s - p[0]: %d p[1]: %d argv[0]: %s argv[1]: %s argv[2]: %s\n",__func__,__LINE__,cmd[0],fdMap[0],fdMap[1],argv[0],argv[1],argv[2]);
    pid = spawnp(argv[0], 3, fdMap, &in, argv, (const char **)env);

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

    sv_setiv(*av_fetch(PL_fdpid, newfd, TRUE), pid);
    PL_forkprocess = pid;

    return PerlIO_fdopen(newfd, mode);
}
