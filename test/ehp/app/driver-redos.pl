#!/usr/bin/env perl

use strict;
use warnings;

use threads;
use Time::HiRes qw(usleep nanosleep gettimeofday tv_interval);

if (not @ARGV) {
  die "Usage: $0 PORT\n";
}

my $PORT = $ARGV[0];

print "Starting legitimate client on port $PORT\n";
my $t1 = threads->create(\&runLegitimateClient, ($PORT, 5000, 8));

print "Starting malicious client on port $PORT\n";
my $t2 = threads->create(\&runMaliciousClient, ($PORT));

$t1->join();
$t2->join();

print "Done\n";
exit 0;

sub runLegitimateClient {
  my ($PORT, $reqPerHalfSec, $nSeconds) = @_;

  my $nIter = 2*$nSeconds;

  for (my $i = 0; $i < $nIter; $i++) {
    print "$i: Legitimate clients\n";
    print time . "\n";
    my @before = gettimeofday;

    `ab -n $reqPerHalfSec -c 80 "http://localhost:$PORT/\?fileToRead=/tmp/staticFile.txt" 2>&1`;

    my @after = gettimeofday;
    my $elapsed_s = tv_interval(\@before, \@after);
    print "before <@before> after <@after> elapsed: $elapsed_s\n";
    my $remainder = (0.5 - $elapsed_s);
    print "remainder: $remainder\n";

    if (0 < $remainder) {
      my $remainder_us = $remainder * 1000000;
      print "sleeping for $remainder_us\n";
      usleep($remainder_us);
    }
  }
}

sub runMaliciousClient {
  my ($PORT) = @_;

  sleep 1;
  print "Starting malicious client\n";
  my $url = "http://localhost:$PORT/?fileToRead=//////////////////////////////////////////////////\n";
  `wget '$url'`;
}
