#!/usr/bin/perl

use strict;

sub process_dir {
	my $directory = $_[0];
	my $mkfs_test;
	my $test_name;
	my $test_num;

	my %real_info = ();
	my %usr_info  = ();
	my %sys_info  = (); # could just have one hash, and assign an array to it

	print "directory: $directory\n";
	while ( <$directory/*> ) {
		if ($_ =~ /runtest/ ) {
			$test_num = $_;
			$test_num =~ s/$directory\/runtest//g;
			open (FILE, "$_");
			my @contents = <FILE>;
			close (FILE);
			chomp $contents[0];
			chomp $contents[1];
			chomp $contents[2];
                        chomp $contents[3];
                        chomp $contents[4];
                        chomp $contents[5];
			my ($crap, $real_0) = split( / /, $contents[0]);
			my ($crap, $usr_0) = split( / /, $contents[1]);
			my ($crap, $sys_0) = split( / /, $contents[2]);
                        my ($crap, $real_1) = split( / /, $contents[3]);
			my ($crap, $usr_1) = split( / /, $contents[4]);
			my ($crap, $sys_1) = split( / /, $contents[5]);
			#print "$test_num real: $real usr: $usr sys: $sys\n";
                        my ($real) = $real_0 + $real_1;
                        my ($usr) = $usr_0 + $usr_1;
                        my ($sys) = $sys_0 + $sys_1; 
			$real_info{$test_num} = $real;	
			$usr_info{$test_num} = $usr;	
			$sys_info{$test_num} = $sys;	
		}
		elsif ($_ =~ /mkfs/ ) {
			$mkfs_test=$_;
		}
		elsif ($_ =~ /umount/) {
		}
		else {
			$test_name = $_;
			$test_name =~ s/$directory\///g;
			print "$test_name\n";
		}
	}
	open (TEST, ">$test_name");
	my $key;
	print TEST "# <num snapshots>, <real time>, <usr time> <sys time>\n";
	for $key (sort { $a <=> $b} (keys (%real_info)) )
	{
		if ( $ARGV[0] ) {
			print TEST "$key,$real_info{$key},$usr_info{$key},$sys_info{$key}\n"; 
		}
		else {
			print TEST "$key $real_info{$key} $usr_info{$key} $sys_info{$key}\n"; 
		}
	}
	close (TEST);
}

while ( <*> ) {
	if (-d $_)  {
		process_dir($_, $ARGV[0]); 
	}
}
