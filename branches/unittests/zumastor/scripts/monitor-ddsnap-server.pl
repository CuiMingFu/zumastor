#!/usr/bin/perl
#
# Monitor the ddsnap server page use for all Zumastor volumes.
#

use POSIX qw(strftime);
use IO::Handle;
use Getopt::Std;

my %options = ();
my $sleeptime = 30;
my $count = -1;
my ($logfile, @servers, $server, $ddsnappid);

getopt ("cdf", \%options);
$count = $options{c} if defined $options{c};
$sleeptime = $options{d} if defined $options{d};
if (defined $options{f}) {
	$logfile = $options{f};
	open (LOGFILE, ">>" . $logfile) or die "Could not open log file $f: $!";
}
else {
	open (LOGFILE, ">&STDOUT") or die "Couldn't dup stdout: $!";
}
select LOGFILE; $| = 1;

my $starttime = time;

sub scan_process {
	my $pid = $_[0];
	my $elapsed = $_[1];
	my $vol = $_[2];

	my $basename = "/proc/" . $pid . "/";
	my $rss = 0;
	my $private_dirty = 0;
	my $private_clean = 0;
	my $shared_dirty = 0;
	my $shared_clean = 0;
	open SMAPS, "<" . $basename . "smaps" or return;
	while (<SMAPS>) {
		if (/^Rss:\D*(\d+)\D*$/) {
			$rss += $1;
		}
		if (/^Private_Clean:\D*(\d+)\D*$/) {
			$private_clean += $1;
		}
		if (/^Private_Dirty:\D*(\d+)\D*$/) {
			$private_dirty += $1;
		}
		if (/^Shared_Clean:\D*(\d+)\D*$/) {
			$shared_clean += $1;
		}
		if (/^Shared_Dirty:\D*(\d+)\D*$/) {
			$shared_dirty += $1;
		}
	}
	print LOGFILE "$vol $elapsed $shared_clean $shared_dirty $private_clean $private_dirty $rss\n";
	LOGFILE->flush();
}

while ($count < 0 || $count-- > 0) {
	if (scalar(@ARGV) > 0) {
		@servers = @ARGV;
	}
	else {
		unless (opendir(SDIR, "/var/run/zumastor/servers")) {
			die("Couldn't open server piddir: $!");
		}
		@servers = grep /^\w+$/, readdir(SDIR);
		closedir SDIR;
	}
	$ddsnappid = "";
	foreach $server (@servers) {
		next unless open(PID, "/var/run/zumastor/$server-server.pid");
		$ddsnappid = <PID>;
		close(PID);
		chomp $ddsnappid;
		scan_process($ddsnappid, (time - $starttime), $server);
	}
	if ($count != 0) {
		sleep($sleeptime);
	}
}
