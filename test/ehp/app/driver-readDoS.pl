#!/usr/bin/env perl

my $NSEC = 20;

use strict;
use warnings;

use threads;
use Time::HiRes qw(usleep nanosleep gettimeofday tv_interval);

if (not @ARGV) {
  die "Usage: $0 PORT\n";
}

my $PORT = $ARGV[0];

print "Starting legitimate client on port $PORT\n";
my $t1 = threads->create(\&runLegitimateClient, ($PORT, $NSEC));

print "Starting malicious client on port $PORT\n";
my $t2 = threads->create(\&runMaliciousClient, ($PORT, $NSEC));

$t1->join();
$t2->join();

print "Done\n";
exit 0;

sub runLegitimateClient {
  my ($PORT, $nSeconds) = @_;

  print "Legitimate clients\n";
  system("ab -n 99999999 -t $nSeconds -c 80 'http://localhost:$PORT/\?fileToRead=/tmp/staticFile.txt' > /dev/null 2>&1");

  return;
}

sub runMaliciousClient {
  my ($PORT, $nSeconds) = @_;

  sleep 2;
  
  for (my $i = 0; $i < $nSeconds; $i++) {
    print time . ": Malicious request $i\n";
    for (1 .. 1) {
      my $url = "http://localhost:$PORT/?fileToRead=/dev/random";
      system("wget '$url' > /dev/null 2>&1 &");
    }
    sleep 1;
  }
}
