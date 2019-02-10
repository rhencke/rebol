//
//  File: %call-posix.c
//  Summary: "Implemention of CALL native for POSIX"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 Atronix Engineering
// Copyright 2012-2019 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//

#if !defined(__cplusplus) && defined(TO_LINUX)
    //
    // See feature_test_macros(7), this definition is redundant under C++
    //
    #define _GNU_SOURCE // Needed for pipe2 when #including <unistd.h>
#endif
#include <unistd.h>
#include <stdlib.h>

// The location of "environ" (environment variables inventory that you
// can walk on POSIX) can vary.  Some put it in stdlib, some put it
// in <unistd.h>.  And OS X doesn't define it in a header at all, you
// just have to declare it yourself.  :-/
//
// https://stackoverflow.com/a/31347357/211160
//
#if defined(TO_OSX) || defined(TO_OPENBSD_X64)
    extern char **environ;
#endif

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#if !defined(WIFCONTINUED) && defined(TO_ANDROID)
// old version of bionic doesn't define WIFCONTINUED
// https://android.googlesource.com/platform/bionic/+/c6043f6b27dc8961890fed12ddb5d99622204d6d%5E%21/#F0
    # define WIFCONTINUED(x) (WIFSTOPPED(x) && WSTOPSIG(x) == 0xffff)
#endif


#include "sys-core.h"

#include "tmp-mod-process.h"

#include "reb-process.h"

static inline bool Open_Pipe_Fails(int pipefd[2]) {
  #ifdef USE_PIPE2_NOT_PIPE
    //
    // NOTE: pipe() is POSIX, but pipe2() is Linux-specific.  With pipe() it
    // takes an additional call to fcntl() to request non-blocking behavior,
    // so it's a small amount more work.  However, there are other flags which
    // if aren't passed atomically at the moment of opening allow for a race
    // condition in threading if split, e.g. FD_CLOEXEC.
    //
    // (If you don't have FD_CLOEXEC set on the file descriptor, then all
    // instances of CALL will act as a /WAIT.)
    //
    // At time of writing, this is mostly academic...but the code needed to be
    // patched to work with pipe() since some older libcs do not have pipe2().
    // So the ability to target both are kept around, saving the pipe2() call
    // for later Linuxes known to have it (and O_CLOEXEC).
    //
    if (pipe2(pipefd, O_CLOEXEC))
        return true;
  #else
    if (pipe(pipefd) < 0)
        return true;
    int direction;  // READ=0, WRITE=1
    for (direction = 0; direction < 2; ++direction) {
        int oldflags = fcntl(pipefd[direction], F_GETFD);
        if (oldflags < 0)
            return true;
        if (fcntl(pipefd[direction], F_SETFD, oldflags | FD_CLOEXEC) < 0)
            return true;
    }
  #endif
    return false;
}

static inline bool Set_Nonblocking_Fails(int fd) {
    int oldflags;
    oldflags = fcntl(fd, F_GETFL);
    if (oldflags < 0)
        return true;
    if (fcntl(fd, F_SETFL, oldflags | O_NONBLOCK) < 0)
        return true;

    return false;
}


//
//  OS_Create_Process: C
//
// flags:
//     1: wait, is implied when I/O redirection is enabled
//     2: console
//     4: shell
//     8: info
//     16: show
//
// Return -1 on error, otherwise the process return code.
//
// POSIX previous simple version was just 'return system(call);'
// This uses 'execvp' which is "POSIX.1 conforming, UNIX compatible"
//
int OS_Create_Process(
    REBFRM *frame_,  // stopgap: allows access to CALL's ARG() and REF()
    const char *call,
    int argc,
    const char* argv[],
    bool flag_wait,  // distinct from REF(wait)
    uint64_t *pid,
    int *exit_code,
    char *input,
    uint32_t input_len,
    char **output,
    uint32_t *output_len,
    char **err,
    uint32_t *err_len
){
    PROCESS_INCLUDE_PARAMS_OF_CALL;

    UNUSED(ARG(command));  // translated into call and argc/argv
    UNUSED(REF(wait));  // flag_wait controls this
    UNUSED(REF(input));
    UNUSED(REF(output));
    UNUSED(REF(error));

    UNUSED(REF(console));  // actually not paid attention to

    UNUSED(call);

    int status = 0;
    int ret = 0;
    int non_errno_ret = 0; // "ret" above should be valid errno

    // An "info" pipe is used to send back an error code from the child
    // process back to the parent if there is a problem.  It only writes
    // an integer's worth of data in that case, but it may need a bigger
    // buffer if more interesting data needs to pass between them.
    //
    char *info = NULL;
    off_t info_size = 0;
    uint32_t info_len = 0;

    // suppress unused warnings but keep flags for future use
    UNUSED(REF(info));
    UNUSED(REF(console));

    const unsigned int R = 0;
    const unsigned int W = 1;
    int stdin_pipe[] = {-1, -1};
    int stdout_pipe[] = {-1, -1};
    int stderr_pipe[] = {-1, -1};
    int info_pipe[] = {-1, -1};

    if (IS_TEXT(ARG(in)) or IS_BINARY(ARG(in))) {
        if (Open_Pipe_Fails(stdin_pipe))
            goto stdin_pipe_err;
    }

    if (IS_TEXT(ARG(out)) or IS_BINARY(ARG(out))) {
        if (Open_Pipe_Fails(stdout_pipe))
            goto stdout_pipe_err;
    }

    if (IS_TEXT(ARG(err)) or IS_BINARY(ARG(err))) {
        if (Open_Pipe_Fails(stderr_pipe))
            goto stdout_pipe_err;
    }

    if (Open_Pipe_Fails(info_pipe))
        goto info_pipe_err;

    pid_t fpid;  // gotos would cross initialization
    fpid = fork();
    if (fpid == 0) {
        //
        // This is the child branch of the fork.  In GDB if you want to debug
        // the child you need to use `set follow-fork-mode child`:
        //
        // http://stackoverflow.com/questions/15126925/

        if (IS_TEXT(ARG(in)) or IS_BINARY(ARG(in))) {
            close(stdin_pipe[W]);
            if (dup2(stdin_pipe[R], STDIN_FILENO) < 0)
                goto child_error;
            close(stdin_pipe[R]);
        }
        else if (IS_FILE(ARG(in))) {
            char *local_utf8 = rebSpell("file-to-local", ARG(in), rebEND);

            int fd = open(local_utf8, O_RDONLY);

            rebFree(local_utf8);

            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDIN_FILENO) < 0)
                goto child_error;
            close(fd);
        }
        else if (IS_BLANK(ARG(in))) {
            int fd = open("/dev/null", O_RDONLY);
            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDIN_FILENO) < 0)
                goto child_error;
            close(fd);
        }
        else {
            assert(IS_NULLED(ARG(in)));
            // inherit stdin from the parent
        }

        if (IS_TEXT(ARG(out)) or IS_BINARY(ARG(out))) {
            close(stdout_pipe[R]);
            if (dup2(stdout_pipe[W], STDOUT_FILENO) < 0)
                goto child_error;
            close(stdout_pipe[W]);
        }
        else if (IS_FILE(ARG(out))) {
            char *local_utf8 = rebSpell("file-to-local", ARG(out), rebEND);

            int fd = open(local_utf8, O_CREAT | O_WRONLY, 0666);

            rebFree(local_utf8);

            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDOUT_FILENO) < 0)
                goto child_error;
            close(fd);
        }
        else if (IS_BLANK(ARG(out))) {
            int fd = open("/dev/null", O_WRONLY);
            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDOUT_FILENO) < 0)
                goto child_error;
            close(fd);
        }
        else {
            assert(IS_NULLED(ARG(out)));
            // inherit stdout from the parent
        }

        if (IS_TEXT(ARG(err)) or IS_BINARY(ARG(err))) {
            close(stderr_pipe[R]);
            if (dup2(stderr_pipe[W], STDERR_FILENO) < 0)
                goto child_error;
            close(stderr_pipe[W]);
        }
        else if (IS_FILE(ARG(err))) {
            char *local_utf8 = rebSpell("file-to-local", ARG(err), rebEND);

            int fd = open(local_utf8, O_CREAT | O_WRONLY, 0666);

            rebFree(local_utf8);

            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDERR_FILENO) < 0)
                goto child_error;
            close(fd);
        }
        else if (IS_BLANK(ARG(err))) {
            int fd = open("/dev/null", O_WRONLY);
            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDERR_FILENO) < 0)
                goto child_error;
            close(fd);
        }
        else {
            assert(IS_NULLED(ARG(err)));
            // inherit stderr from the parent
        }

        close(info_pipe[R]);

        /* printf("flag_shell in child: %hhu\n", flag_shell); */

        // We want to be able to compile with most all warnings as errors, and
        // we'd like to use -Wcast-qual (in builds where it is possible--it
        // is not possible in plain C builds).  We must tunnel under the cast.
        //
        char * const *argv_hack;

        if (REF(shell)) {
            const char *sh = getenv("SHELL");

            if (sh == NULL) {  // shell does not exist
                int err = 2;
                if (write(info_pipe[W], &err, sizeof(err)) == -1) {
                    //
                    // Nothing we can do, but need to stop compiler warning
                    // (cast to void is insufficient for warn_unused_result)
                }
                exit(EXIT_FAILURE);
            }

            const char ** argv_new = cast(
                const char**,
                malloc((argc + 3) * sizeof(argv[0])
            ));
            argv_new[0] = sh;
            argv_new[1] = "-c";
            memcpy(&argv_new[2], argv, argc * sizeof(argv[0]));
            argv_new[argc + 2] = NULL;

            memcpy(&argv_hack, &argv_new, sizeof(argv_hack));
            execvp(sh, argv_hack);
        }
        else {
            memcpy(&argv_hack, &argv, sizeof(argv_hack));
            execvp(argv[0], argv_hack);
        }

        // Note: execvp() will take over the process and not return, unless
        // there was a problem in the execution.  So you shouldn't be able
        // to get here *unless* there was an error, which will be in errno.

      child_error: ;  // semicolon necessary, next statement is declaration

        // The original implementation of this code would write errno to the
        // info pipe.  However, errno may be volatile (and it is on Android).
        // write() does not accept volatile pointers, so copy it to a
        // temporary value first.
        //
        int nonvolatile_errno = errno;

        if (write(info_pipe[W], &nonvolatile_errno, sizeof(int)) == -1) {
            //
            // Nothing we can do, but need to stop compiler warning
            // (cast to void is insufficient for warn_unused_result)
            //
            assert(false);
        }
        exit(EXIT_FAILURE);  // get here only when exec fails
    }
    else if (fpid > 0) {
        //
        // This is the parent branch, so it may (or may not) wait on the
        // child fork branch, based on /WAIT.  Even if you are not using
        // /WAIT, it will use the info pipe to make sure the process did
        // actually start.
        //
        nfds_t nfds = 0;
        struct pollfd pfds[4];
        unsigned int i;
        ssize_t nbytes;
        off_t input_size = 0;
        off_t output_size = 0;
        off_t err_size = 0;
        int valid_nfds;

        // Only put the input pipe in the consideration if we can write to
        // it and we have data to send to it.

        if ((stdin_pipe[W] > 0) && (input_size = strlen(input)) > 0) {
            /* printf("stdin_pipe[W]: %d\n", stdin_pipe[W]); */
            if (Set_Nonblocking_Fails(stdin_pipe[W]))
                goto kill;

            // the passed in input_len is in characters, not in bytes
            //
            input_len = 0;

            pfds[nfds].fd = stdin_pipe[W];
            pfds[nfds].events = POLLOUT;
            nfds++;

            close(stdin_pipe[R]);
            stdin_pipe[R] = -1;
        }
        if (stdout_pipe[R] > 0) {
            /* printf("stdout_pipe[R]: %d\n", stdout_pipe[R]); */
            if (Set_Nonblocking_Fails(stdout_pipe[R]))
                goto kill;

            output_size = BUF_SIZE_CHUNK;

            *output = cast(char*, malloc(output_size));
            *output_len = 0;

            pfds[nfds].fd = stdout_pipe[R];
            pfds[nfds].events = POLLIN;
            nfds++;

            close(stdout_pipe[W]);
            stdout_pipe[W] = -1;
        }
        if (stderr_pipe[R] > 0) {
            /* printf("stderr_pipe[R]: %d\n", stderr_pipe[R]); */
            if (Set_Nonblocking_Fails(stderr_pipe[R]))
                goto kill;

            err_size = BUF_SIZE_CHUNK;

            *err = cast(char*, malloc(err_size));
            *err_len = 0;

            pfds[nfds].fd = stderr_pipe[R];
            pfds[nfds].events = POLLIN;
            nfds++;

            close(stderr_pipe[W]);
            stderr_pipe[W] = -1;
        }

        if (info_pipe[R] > 0) {
            if (Set_Nonblocking_Fails(info_pipe[R]))
                goto kill;

            pfds[nfds].fd = info_pipe[R];
            pfds[nfds].events = POLLIN;
            nfds++;

            info_size = 4;

            info = cast(char*, malloc(info_size));

            close(info_pipe[W]);
            info_pipe[W] = -1;
        }

        valid_nfds = nfds;
        while (valid_nfds > 0) {
            pid_t xpid = waitpid(fpid, &status, WNOHANG);
            if (xpid == -1) {
                ret = errno;
                goto error;
            }

            if (xpid == fpid) {
                //
                // try one more time to read any remainding output/err
                //
                if (stdout_pipe[R] > 0) {
                    nbytes = read(
                        stdout_pipe[R],
                        *output + *output_len,
                        output_size - *output_len
                    );

                    if (nbytes > 0) {
                        *output_len += nbytes;
                    }
                }

                if (stderr_pipe[R] > 0) {
                    nbytes = read(
                        stderr_pipe[R],
                        *err + *err_len,
                        err_size - *err_len
                    );
                    if (nbytes > 0) {
                        *err_len += nbytes;
                    }
                }

                if (info_pipe[R] > 0) {
                    nbytes = read(
                        info_pipe[R],
                        info + info_len,
                        info_size - info_len
                    );
                    if (nbytes > 0) {
                        info_len += nbytes;
                    }
                }

                if (WIFSTOPPED(status)) {
                    //
                    // TODO: Review, What's the expected behavior if the
                    // child process is stopped?
                    //
                    continue;
                } else if  (WIFCONTINUED(status)) {
                    // pass
                } else {
                    // exited normally or due to signals
                    break;
                }
            }

            /*
            for (i = 0; i < nfds; ++i) {
                printf(" %d", pfds[i].fd);
            }
            printf(" / %d\n", nfds);
            */
            if (poll(pfds, nfds, -1) < 0) {
                ret = errno;
                goto kill;
            }

            for (i = 0; i < nfds && valid_nfds > 0; ++i) {
                /* printf("check: %d [%d/%d]\n", pfds[i].fd, i, nfds); */

                if (pfds[i].revents & POLLERR) {
                    /* printf("POLLERR: %d [%d/%d]\n", pfds[i].fd, i, nfds); */

                    close(pfds[i].fd);
                    pfds[i].fd = -1;
                    valid_nfds --;
                }
                else if (pfds[i].revents & POLLOUT) {
                    /* printf("POLLOUT: %d [%d/%d]\n", pfds[i].fd, i, nfds); */

                    nbytes = write(pfds[i].fd, input, input_size - input_len);
                    if (nbytes <= 0) {
                        ret = errno;
                        goto kill;
                    }
                    /* printf("POLLOUT: %d bytes\n", nbytes); */
                    input_len += nbytes;
                    if (cast(off_t, input_len) >= input_size) {
                        close(pfds[i].fd);
                        pfds[i].fd = -1;
                        valid_nfds --;
                    }
                }
                else if (pfds[i].revents & POLLIN) {
                    /* printf("POLLIN: %d [%d/%d]\n", pfds[i].fd, i, nfds); */
                    char **buffer = NULL;
                    uint32_t *offset;
                    ssize_t to_read = 0;
                    off_t *size;
                    if (pfds[i].fd == stdout_pipe[R]) {
                        buffer = output;
                        offset = output_len;
                        size = &output_size;
                    }
                    else if (pfds[i].fd == stderr_pipe[R]) {
                        buffer = err;
                        offset = err_len;
                        size = &err_size;
                    }
                    else {
                        assert(pfds[i].fd == info_pipe[R]);
                        buffer = &info;
                        offset = &info_len;
                        size = &info_size;
                    }

                    do {
                        to_read = *size - *offset;
                        assert (to_read > 0);
                        /* printf("to read %d bytes\n", to_read); */
                        nbytes = read(pfds[i].fd, *buffer + *offset, to_read);

                        // The man page of poll says about POLLIN:
                        //
                        // POLLIN      Data other than high-priority data may be read without blocking.

                        //    For STREAMS, this flag is set in revents even if the message is of _zero_ length. This flag shall be equivalent to POLLRDNORM | POLLRDBAND.
                        // POLLHUP     A  device  has been disconnected, or a pipe or FIFO has been closed by the last process that had it open for writing. Once set, the hangup state of a FIFO shall persist until some process opens the FIFO for writing or until all read-only file descriptors for the FIFO  are  closed.  This  event  and POLLOUT  are  mutually-exclusive; a stream can never be writable if a hangup has occurred. However, this event and POLLIN, POLLRDNORM, POLLRDBAND, or POLLPRI are not mutually-exclusive. This flag is only valid in the revents bitmask; it shall be ignored in the events member.
                        // So "nbytes = 0" could be a valid return with POLLIN, and not indicating the other end closed the pipe, which is indicated by POLLHUP
                        if (nbytes <= 0)
                            break;

                        /* printf("POLLIN: %d bytes\n", nbytes); */

                        *offset += nbytes;
                        assert(cast(off_t, *offset) <= *size);

                        if (cast(off_t, *offset) == *size) {
                            char *larger = cast(
                                char*,
                                malloc(*size + BUF_SIZE_CHUNK)
                            );
                            if (larger == NULL)
                                goto kill;
                            memcpy(larger, *buffer, *size);
                            free(*buffer);
                            *buffer = larger;
                            *size += BUF_SIZE_CHUNK;
                        }
                        assert(cast(off_t, *offset) < *size);
                    } while (nbytes == to_read);
                }
                else if (pfds[i].revents & POLLHUP) {
                    /* printf("POLLHUP: %d [%d/%d]\n", pfds[i].fd, i, nfds); */
                    close(pfds[i].fd);
                    pfds[i].fd = -1;
                    valid_nfds --;
                }
                else if (pfds[i].revents & POLLNVAL) {
                    /* printf("POLLNVAL: %d [%d/%d]\n", pfds[i].fd, i, nfds); */
                    ret = errno;
                    goto kill;
                }
            }
        }

        if (valid_nfds == 0 && flag_wait) {
            if (waitpid(fpid, &status, 0) < 0) {
                ret = errno;
                goto error;
            }
        }

    }
    else {  // error
        ret = errno;
        goto error;
    }

    goto cleanup;

  kill:

    kill(fpid, SIGKILL);
    waitpid(fpid, NULL, 0);

  error:

    if (ret == 0)
        non_errno_ret = -1024;  // !!! randomly picked

  cleanup:

    // CALL only expects to have to free the output or error buffer if there
    // was a non-zero number of bytes returned.  If there was no data, take
    // care of it here.
    //
    // !!! This won't be done this way when this routine actually appends to
    // the BINARY! or STRING! itself.

    if (output and *output)
        if (*output_len == 0) {  // buffer allocated but never used
            free(*output);
            *output = NULL;
        }

    if (err and *err)
        if (*err_len == 0) {  // buffer allocated but never used
            free(*err);
            *err = NULL;
        }

    if (info_pipe[R] > 0)
        close(info_pipe[R]);

    if (info_pipe[W] > 0)
        close(info_pipe[W]);

    if (info_len == sizeof(int)) {
        //
        // exec in child process failed, set to errno for reporting.
        //
        ret = *cast(int*, info);
    }
    else if (WIFEXITED(status)) {
        assert(info_len == 0);

       *exit_code = WEXITSTATUS(status);
       *pid = fpid;
    }
    else if (WIFSIGNALED(status)) {
        non_errno_ret = WTERMSIG(status);
    }
    else if (WIFSTOPPED(status)) {
        //
        // Shouldn't be here, as the current behavior is keeping waiting when
        // child is stopped
        //
        assert(false);
        if (info)
            free(info);
        rebJumps("fail {Child process is stopped}", rebEND);
    }
    else {
        non_errno_ret = -2048;  // !!! randomly picked
    }

    if (info != NULL)
        free(info);

  info_pipe_err:

    if (stderr_pipe[R] > 0)
        close(stderr_pipe[R]);

    if (stderr_pipe[W] > 0)
        close(stderr_pipe[W]);

    goto stderr_pipe_err;  // no jumps to `info_pipe_err:` yet, avoid warning

  stderr_pipe_err:

    if (stdout_pipe[R] > 0)
        close(stdout_pipe[R]);

    if (stdout_pipe[W] > 0)
        close(stdout_pipe[W]);

  stdout_pipe_err:

    if (stdin_pipe[R] > 0)
        close(stdin_pipe[R]);

    if (stdin_pipe[W] > 0)
        close(stdin_pipe[W]);

  stdin_pipe_err:

    // We will get to this point on success, as well as error (so ret may
    // be 0.  This is the return value of the host kit function to Rebol, not
    // the process exit code (that's written into the pointer arg 'exit_code')

    if (non_errno_ret > 0) {
        rebJumps(
            "fail [{Child process is terminated by signal:}",
                rebI(non_errno_ret), rebEND
        );
    }
    else if (non_errno_ret < 0)
        rebJumps("fail {Unknown error happened in CALL}");

    return ret;
}
