#!./perl

BEGIN {
    chdir 't' if -d 't';
    @INC = '../lib';
    require './test.pl';
}

use strict;
use warnings;
use Config;

# Skip test suite if the platform/build lacks posix_spawn or spawn
unless ($Config{d_posix_spawn} || $Config{d_spawn}) {
    skip_all("Neither posix_spawn nor fork available");
}

plan(tests => 11);

# -------------------------------------------------------------------
# Test 1: Basic Execution & PID Return
# -------------------------------------------------------------------
{
    my ($pid, $in, $out, $err) = spawn($^X, '-e', 'print "OK_SPAWN"');
    
    ok($pid > 0, 'spawn returns a positive integer PID');
    
    my $stdout_content = <$out>;
    is($stdout_content, 'OK_SPAWN', 'spawn captures child STDOUT correctly');
    
    close $in; close $out; close $err;
    waitpid($pid, 0);
    is($? >> 8, 0, 'Child process exited with status 0');
}

# -------------------------------------------------------------------
# Test 2: STDERR Redirection
# -------------------------------------------------------------------
{
    my ($pid, $in, $out, $err) = spawn($^X, '-e', 'print STDERR "ERR_SPAWN"');
    
    ok($pid > 0, 'spawn executed for STDERR test');
    
    my $stderr_content = <$err>;
    is($stderr_content, 'ERR_SPAWN', 'spawn captures child STDERR correctly');
    
    close $in; close $out; close $err;
    waitpid($pid, 0);
}

# -------------------------------------------------------------------
# Test 3: Bidirectional STDIN -> STDOUT Piping
# -------------------------------------------------------------------
{
    my ($pid, $in, $out, $err) = spawn($^X, '-pe', 'tr/a-z/A-Z/');
    
    ok($pid > 0, 'spawn executed for STDIN piping test');
    
    print $in "hello world\n";
    close $in; # Signal EOF to child
    
    my $reply = <$out>;
    is($reply, "HELLO WORLD\n", 'Child correctly read from STDIN and transformed text');
    
    close $out; close $err;
    waitpid($pid, 0);
}

# -------------------------------------------------------------------
# Test 4: Exit Code Propagation
# -------------------------------------------------------------------
{
    my ($pid, $in, $out, $err) = spawn($^X, '-e', 'exit 37;');
    
    ok($pid > 0, 'spawn executed exit code process');
    
    close $in; close $out; close $err;
    waitpid($pid, 0);
    is($? >> 8, 37, 'waitpid correctly captures child non-zero exit status (37)');
}

# -------------------------------------------------------------------
# Test 5: Invalid Binary Handling & Errno Setting
# -------------------------------------------------------------------
{
    local $! = 0;
    my ($pid, $in, $out, $err) = spawn('/non/existent/path/to/binary_xyz_123');
    
    if ($pid == -1) {
        pass('spawn returned -1 directly for non-existent binary');
        ok($! != 0, '$! (errno) was set when spawn failed');
    } else {
        # POSIX spawn can succeed creating shell wrapper before exec fails in child
        waitpid($pid, 0);
        ok(($? >> 8) != 0, 'Child process failed asynchronously with non-zero exit code');
        pass('Asynchronous binary error handling verified');
    }
}
