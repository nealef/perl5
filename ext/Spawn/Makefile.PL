use ExtUtils::MakeMaker;
use strict;
use warnings;


WriteMakefile(
    NAME	     => "Spawn",
    VERSION_FROM => "Spawn.pm",
    LIBS         => [''],
    DEFINE       => '',
    INC          => '-I.',
);
