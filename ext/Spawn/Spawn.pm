package Spawn;

use strict;
use warnings;
use Config;
use Exporter 'import';

our @EXPORT = qw(spawn);
our $VERSION = '0.01';

if (!defined $Config{d_fork} || $Config{d_fork} ne 'define') {
    require XSLoader;
    XSLoader::load('Spawn', $VERSION);
}

sub spawn {
    if ($Config{d_fork} && $Config{d_fork} eq 'define') {
        return _spawn_via_open3(@_);
    } else {
        return _posix_spawn_c(@_);
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

1;
