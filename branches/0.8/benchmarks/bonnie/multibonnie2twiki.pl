#!/usr/bin/perl

# extract multibonnie.sh results file and summarize for use on a
# wiki or plain text.  Requires both the stdout and stderr from the
# multibonnie.sh script, to determine the number and size of the
# tests during the performance calculation.
#
# Example:
#  ./multibonnie.sh 1 10 >160MB.bonnie 2>&1
#  ./multibonnie.sh 2 10 >>160MB.bonnie 2>&1
#  ./multibonnie2twiki.pl 160MB.bonnie


use strict;

my $nway=0;
my $files=0;
my $max=0;
my $min=0;
my $dirs=0;
my $bonnie;
my %results;

while (<>) {

  if (/BONNIE=(\S+)/) {
    $bonnie = $1;
  } elsif (/bonnie.*-p (\d+)/) {
    $nway = $1;
  } elsif (/bonnie.* -n (\d+):(\d+):(\d+):(\d+)/) {
    $files = $1;
    $max = $2;
    $min = $3;
    $dirs = $4;
    $results{$nway} = {};
  } elsif (/^([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*)$/) {
    $results{$nway}{name}=$1;
    $results{$nway}{size}=$2;
    $results{$nway}{charout}=$3;
    $results{$nway}{charoutpct}=$4;
    $results{$nway}{blockout}=$5;
    $results{$nway}{blockoutpct}=$6;
    $results{$nway}{rewrite}=$7;
    $results{$nway}{rewritepct}=$8;
    $results{$nway}{charin}=$9;
    $results{$nway}{charinpct}=$10;
    $results{$nway}{blockin}=$11;
    $results{$nway}{blockinpct}=$12;
    $results{$nway}{seeks}=$13;
    $results{$nway}{seekspct}=$14;
    $results{$nway}{files}=$15;
    $results{$nway}{seqcreate}=$16;
    $results{$nway}{seqcreatepct}=$17;
    $results{$nway}{seqread}=$18;
    $results{$nway}{seqreadpct}=$19;
    $results{$nway}{seqdelete}=$20;
    $results{$nway}{seqdeletepct}=$21;
    $results{$nway}{randcreate}=$22;
    $results{$nway}{randcreatepct}=$23;
    $results{$nway}{randread}=$24;
    $results{$nway}{randreadpct}=$25;
    $results{$nway}{randdelete}=$26;
    $results{$nway}{randdeletepct}=$27;
  }
}


print <<EOF;
| *system* | *N* |  *file*                   |||  *block*        ||
|          |     |  *(MB/s)*                 |||  *(MB/s)*       ||
|          |     | *write* | *read* | *delete* | *write* | *read* |
EOF

foreach $nway (sort { $a <=> $b } keys %results) {
  printf("|          | %3d |", $nway);

   printf(" %7.2f |", $nway*($max+$min)/2 * $results{$nway}{randcreate}/1000000);
   printf(" %7.2f |", $nway*($max+$min)/2 * $results{$nway}{randread}/1000000);
   printf(" %7.2f |", $nway*($max+$min)/2 * $results{$nway}{randdelete}/1000000);

   printf(" %7.2f |", $nway*$results{$nway}{blockout}*1024/1000000);
   printf(" %7.2f |", $nway*$results{$nway}{blockin}*1024/1000000);


#  printf(" %6.2f |", $nway*$directories*$files*$filesize/$tar{$nway}/1000000);

#  printf(" %8.2f |", $nway*$directories*$files*$filesize/$rm{$nway}/1000000);
#  printf(" %7.2f |", $nway*$blksize*$blkcount/$bwrite{$nway}/1000000);
#  printf(" %6.2f |", $nway*$blksize*$blkcount/$bread{$nway}/1000000);
#  printf(" %8.2f |\n", $nway*$blksize*$blkcount/$brm{$nway}/1000000);

  print "\n";
}


