#define PERL_NO_GET_CONTEXT
#include "EXTERN.h"
#include "perl.h"
#include "perliol.h"
#include "XSUB.h"
#include <spawn.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>

extern char **environ;

/*
 * Converts a PerlIO* handle into a valid Perl scalar handle (IO::Handle).
 * mode: "r" for STDOUT/STDERR (readable), "w" for STDIN (writable).
 */
static SV *
create_perl_handle(pTHX_ PerlIO *pio, const char *mode)
{
    if (!pio) return &PL_sv_undef;

    // 1. Create a new Anonymous Typeglob (GV)
    GV *gv = newGVgen("IO::Handle");
    
    // 2. Add an IO object to the Typeglob
    IO *io = sv_2io((SV*)gv);

    // 3. Attach the PerlIO stream pointer based on read/write mode
    if (mode[0] == 'r') {
        IoIFP(io) = pio;  // Input File Pointer
    } else if (mode[0] == 'w') {
        IoOFP(io) = pio;  // Output File Pointer
    }

    // 4. Return a Reference to the Glob (*HANDLE) as an blessed SV*
    return newRV_noinc((SV*)gv);
}

MODULE = Spawn    PACKAGE = Spawn

void
_posix_spawn_c(...)
    PPCODE:
    {
        dTHX;
        int items_count = items;
        if (items_count == 0) {
            XSRETURN_EMPTY;
        }

        // 1. Create pipes for STDIN, STDOUT, and STDERR
        int stdin_pipe[2];
        int stdout_pipe[2];
        int stderr_pipe[2];

        if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) {
            XSRETURN_EMPTY;
        }

        // 2. Initialize posix_spawn file actions
#if defined(OEZVM) || defined(OEMVS)
        struct inheritance in;
        int fdMap[3];

        memset(&in, 0, sizeof(in));
        in.flags = SPAWN_SETGROUP | SPAWN_SETSIGDEF;
        in.pgroup = SPAWN_NEWPGROUP;                
        fdMap[0] = stdin_pipe[0];
        fdMap[1] = stdout_pipe[1];
        fdMap[2] = stderr_pipe[1];
#else
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);

        // Map read end of stdin_pipe to STDIN_FILENO (0)
        posix_spawn_file_actions_adddup2(&actions, stdin_pipe[0], STDIN_FILENO);
        // Map write end of stdout_pipe to STDOUT_FILENO (1)
        posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDOUT_FILENO);
        // Map write end of stderr_pipe to STDERR_FILENO (2)
        posix_spawn_file_actions_adddup2(&actions, stderr_pipe[1], STDERR_FILENO);

        // Close unused descriptor ends in the child process
        posix_spawn_file_actions_addclose(&actions, stdin_pipe[0]);
        posix_spawn_file_actions_addclose(&actions, stdin_pipe[1]);
        posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]);
        posix_spawn_file_actions_addclose(&actions, stdout_pipe[1]);
        posix_spawn_file_actions_addclose(&actions, stderr_pipe[0]);
        posix_spawn_file_actions_addclose(&actions, stderr_pipe[1]);
#endif

        // 3. Build argv array
        const char **argv = (const char **)safemalloc(sizeof(char *) * (items_count + 1));
        for (int i = 0; i < items_count; i++) {
            argv[i] = (char *)SvPV_nolen(ST(i));
        }
        argv[items_count] = NULL;

        // 4. Spawn child process without fork()
        pid_t pid;
#if defined(OEZVM) || defined(OEMVS)
        pid = spawnp(argv[0], 3, fdMap, &in, argv, (const char **)environ);
#else
        int status = posix_spawn(&pid, argv[0], &actions, NULL, argv, environ);
        posix_spawn_file_actions_destroy(&actions);
#endif

        // Clean up allocation
        safefree(argv);

        // Close child-side pipe ends in the parent process
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        // If we failed to launch
#if defined(OEZVM) || defined(OEMVS)
        if (pid == -1) {
#else
        if (status != 0) {
            SETERRNO(status, 0);
#endif
            close(stdin_pipe[1]);
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);
            XSRETURN_EMPTY;
        }

        // 5. Convert C read file descriptors to Perl filehandles
        PerlIO *pio_in = PerlIO_fdopen(stdin_pipe[0], "w");
        PerlIO *pio_out = PerlIO_fdopen(stdout_pipe[0], "r");
        PerlIO *pio_err = PerlIO_fdopen(stderr_pipe[0], "r");

	    // --- ERROR CHECK FOR PerlIO_fdopen FAILURES ---
        if (!pio_in || !pio_out || !pio_err) {
            // Save current errno or default to ENOMEM
            int err = errno ? errno : ENOMEM;

            // Clean up successfully opened PerlIO streams (closes underlying fd automatically)
            if (pio_in)  PerlIO_close(pio_in);  else close(stdin_pipe[1]);
            if (pio_out) PerlIO_close(pio_out); else close(stdout_pipe[0]);
            if (pio_err) PerlIO_close(pio_err); else close(stderr_pipe[0]);

            // Terminate child to prevent orphaned/zombie process
            kill(pid, SIGTERM);
            int dummy_status;
            waitpid(pid, &dummy_status, 0);

            // Set Perl's $! variable
            SETERRNO(err, 0);
            XSRETURN_EMPTY;
        }

        SV *sv_in  = create_perl_handle(aTHX_ pio_in,  "w");
        SV *sv_out = create_perl_handle(aTHX_ pio_out, "r");
        SV *sv_err = create_perl_handle(aTHX_ pio_err, "r");

        // 6. Return (PID, STDOUT_HANDLE, STDERR_HANDLE) to Perl
        EXTEND(SP, 4);
        PUSHs(sv_2mortal(newSViv((IV)pid)));
        PUSHs(sv_2mortal(sv_in));
        PUSHs(sv_2mortal(sv_out));
        PUSHs(sv_2mortal(sv_err));
    }
