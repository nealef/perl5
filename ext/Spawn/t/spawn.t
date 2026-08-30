use strict;
use warnings;
use Test::More;
use Config;

# 1. Module Compilation Test
BEGIN {
    use_ok('My::Spawn') or bail_out("Failed to load My::Spawn module");
}

# 2. Check Execution Path Reporting
diag("Operating System: $Config{osname}");
diag("d_fork status: " . ($Config{d_fork} // 'undef'));

if ($Config{d_fork} && $Config{d_fork} eq 'define') {
    diag("Test Strategy: Using IPC::Open3 fallback path");
} else {
    diag("Test Strategy: Using C posix_spawn XS path");
}

# 3. Test STDOUT Capture
subtest 'Capture STDOUT' => sub {
    my ($pid, $stdin, $stdout, $stderr) = spawn('perl', '-e', 'print "Hello STDOUT\n";');
    
    ok(defined $pid && $pid > 0, 'Spawn returned a valid child PID');
    
    my $out_line = <$stdout>;
    is($out_line, "Hello STDOUT\n", 'Correctly captured child STDOUT');
    
    close($stdin);
    close($stdout);
    close($stderr);
    
    waitpid($pid, 0);
    is($? >> 8, 0, 'Child exited with status 0');
};

# 4. Test STDERR Capture
subtest 'Capture STDERR' => sub {
    my ($pid, $stdin, $stdout, $stderr) = spawn('perl', '-e', 'print STDERR "Hello STDERR\n";');
    
    ok(defined $pid && $pid > 0, 'Spawn returned a valid child PID');
    
    my $err_line = <$stderr>;
    is($err_line, "Hello STDERR\n", 'Correctly captured child STDERR');
    
    close($stdin);
    close($stdout);
    close($stderr);
    
    waitpid($pid, 0);
    is($? >> 8, 0, 'Child exited with status 0');
};

# 5. Test STDIN Writing (Bidirectional Pipeline)
subtest 'Write to STDIN and Echo back' => sub {
    my ($pid, $stdin, $stdout, $stderr) = spawn('perl', '-pe', 'tr/a-z/A-Z/');
    
    ok(defined $pid && $pid > 0, 'Spawn returned a valid child PID');
    
    # Write to child's STDIN
    print $stdin "test line 1\n";
    print $stdin "test line 2\n";
    close($stdin); # Signal EOF to child
    
    my $out1 = <$stdout>;
    my $out2 = <$stdout>;
    
    is($out1, "TEST LINE 1\n", 'Child correctly processed and echoed STDIN line 1');
    is($out2, "TEST LINE 2\n", 'Child correctly processed and echoed STDIN line 2');
    
    close($stdout);
    close($stderr);
    
    waitpid($pid, 0);
    is($? >> 8, 0, 'Child process completed successfully after EOF');
};

# 6. Test Non-Zero Exit Code Capture
subtest 'Exit Code Status' => sub {
    my ($pid, $stdin, $stdout, $stderr) = spawn('perl', '-e', 'exit 42;');
    
    ok(defined $pid && $pid > 0, 'Spawn returned a valid child PID');
    
    close($stdin);
    close($stdout);
    close($stderr);
    
    waitpid($pid, 0);
    my $exit_code = $? >> 8;
    is($exit_code, 42, 'Propagated child exit status (42) correctly');
};

# 7. Test Non-Existent Binary Handling
subtest 'Command Execution Failure' => sub {
    my ($pid, $stdin, $stdout, $stderr) = spawn('/non/existent/binary/path/12345');
    
    if (defined $pid && $pid > 0) {
        # POSIX spawn might succeed creating the process shell before failing exec
        waitpid($pid, 0);
        ok(($? >> 8) != 0, 'Child failed with non-zero exit code');
    } else {
        # Direct failure on spawn
        ok(!defined $pid, 'Spawn returned undef for non-existent command');
    }
};

done_testing();
