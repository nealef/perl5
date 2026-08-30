package Spawn;

use strict;
use warnings;
use Config;
use Exporter 'import';

our @EXPORT = qw(spawn);
our $VERSION = '0.01';

sub Spawn {
    if ($Config{d_fork} && $Config{d_fork} eq 'define') {
        return _spawn_via_open3(@_);
    } else {
        return _posix_spawn(@_);
    }
}

sub _spawn_via_open3 {
    require IPC::Open3;
    require Symbol;
    
    my (@cmd) = @_;
    my ($in, $out, $err) = (Symbol::gensym(), Symbol::gensym(), Symbol::gensym());
    my $pid = IPC::Open3::open3($in, $out, $err, @cmd);
    return ($pid, $in, $out, $err);
}


sub _posix_spawn {
    my (@cmd) = @_;
    my ($pid $in, $out, $err) = spawn(@cmd);
    return ($pid, $in, $out, $err);
}

1;
