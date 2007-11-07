#!/bin/sh
# replicate.sh : replication test case

#set -x

tet_startup="startup"
tet_cleanup="cleanup"
iclist="ic1 ic2 ic3"
ic1="small_repl1 small_repl2"
ic2="medium_repl1 medium_repl2"
ic3="large_repl1 large_repl2"

# source TET API shell functions
. $TET_ROOT/lib/xpg3sh/tetapi.sh

repl_init()
{
	vol=$1
	#
	# Verify that Zumastor is running, the test volume is mounted and
	# that volume is replicated to some host.
	#
	zumastor status | grep -q "^Status: running" >/dev/null 2>&1
	if [ $? -ne 0 ]; then
		tet_infoline "Zumastor not running"
		return 5
	fi
	if [ ! -d /var/run/zumastor/mount/${vol} ]; then
		tet_infoline "No mounted zumastor test volume ${vol}"
		return 5
	fi
	targets=`(cd /var/lib/zumastor/volumes/${vol}/targets; ls)`
	if [ "${targets}" = "" ]; then
		tet_infoline "No replication target defined for test volume ${vol}"
		return 5
	fi
	targ=`echo $targets | cut -f1 -d\ `
	if [ "${targ}" = "" ]; then
		tet_infoline "No replication target found"
		return 5
	fi
	ssh -o StrictHostKeyChecking=no root@${targ} "cat >>~/.ssh/authorized_keys" <~/.ssh/id_rsa.pub >out.stderr 2>&1
	if [ $? -ne 0 ]; then
		tet_infoline "ssh root@${targ} failed!"
		tet_infoline `cat out.stderr`
		return 5
	fi
	echo $targ
	return 0
}

#
# Create a temporary file with deterministic data in it.  Return the name.
#
create_file()
{
	dir=$1
	file_size=$2
	file=`mktemp -p $dir`
	./mkfile$$ ${file_size} >$file
	echo $file
}

#
# Start a replication, then wait for it to finish.
#
replicate_and_wait()
{
	vol=$1
	targ=$2
	# Sync everything so our replication actually happens.
	sync
	# Get the current hold and send (if any) file contents.
	oldhold=`cat /var/lib/zumastor/volumes/${vol}/targets/${targ}/hold`
	oldsend=`cat /var/lib/zumastor/volumes/${vol}/targets/${targ}/send`
	# Get the mounted device on the target; when this changes we'll know
	# that the new snapshot has been mounted and the replication is really
	# complete.
	mdev=`ssh -o StrictHostKeyChecking=no root@${targ} "mount | grep ${vol} | cut -f1 -d\ "`
	# If the hold and send files aren't updated in five minutes (plus
	# thirty seconds fudge time), fail.
	starttime=`date +%s`
	endtime=`expr $starttime + 330`
	# If the send file hasn't changed in ten seconds, warn.
	sendtime=`expr $starttime + 10`
	# Start the replication.
	zumastor replicate ${vol} ${targ}
	while /bin/true; do
		newhold=`cat /var/lib/zumastor/volumes/${vol}/targets/${targ}/hold`
		newsend=`cat /var/lib/zumastor/volumes/${vol}/targets/${targ}/send`
		nowtime=`date +%s`
		# If the hold file changed, the transfer finished. 
		if [ "$newhold" != "" -a "$newhold" != "$oldhold" ]; then
			break;
		fi
		# If the send file changed, reset our timeout.
		if [ "$newsend" != "$oldsend" ]; then
			endtime=`expr $nowtime + 330`
			sendtime=`expr $nowtime + 10`
			oldsend="${newsend}"
		fi
		# If neither the hold or send files have changed in the last
		# five minutes, time out and fail.
		if [ $nowtime -gt $endtime ]; then
			tet_infoline "Replication unchanged for five minutes, timed out."
			return 5
		fi
		if [ $nowtime -gt $sendtime ]; then
			tet_infoline "Send file unchanged for ten seconds (warning only)."
			sendtime=`expr $nowtime + 10`
		fi
		sleep 5
	done
	# We now have to wait for the snapshot to be mounted on the target.
	newmdev=`ssh -o StrictHostKeyChecking=no root@${targ} "mount | grep ${vol} | cut -f1 -d\ "`
	while [ "${newmdev}" = "${mdev}" ]; do
		sleep 5
		newmdev=`ssh -o StrictHostKeyChecking=no root@${targ} "mount | grep ${vol} | cut -f1 -d\ "`
	done
	return 0
}

#
# Verify replication works.
#
#	Create file
#	Do replicate
#	Wait for replicate complete
#	Verify replicated data matches.
#
repl_verify_data()
{
	repl_file_size=$1
	tet_infoline "Verify that replication works and data is properly replicated."
	#
	# Init for replication, get the target host.  If this fails, we just
	# abort the test and return.  The repl_init function already did any
	# tet_infoline stuff.
	#
	targ=`repl_init ${ZUMA_TEST_VOL}`
	if [ $? -ne 0 -o "${targ}" = "" ]; then
		tet_result UNRESOLVED
		return
	fi
	#
	# Create a file on the mounted filesystem on the local (source)
	# host.  Start a replicate, wait for it to finish, then find the
	# file on the target and diff it with the local file.
	#
	tfile=`create_file /var/run/zumastor/mount/${ZUMA_TEST_VOL} ${repl_file_size}`
	replicate_and_wait ${ZUMA_TEST_VOL} ${targ}
	if [ $? -ne 0 ]; then
		tet_result UNRESOLVED
		return
	fi
	#
	# Compare the file on the local volume with the same file on the
	# target.
	#
	ssh -o StrictHostKeyChecking=no root@${targ} "cat ${tfile}" | diff -q ${tfile} -
	if [ $? -ne 0 ]; then
		tet_infoline "Replicated file ${tfile} differs!"
		tet_result FAIL
		return
	fi
	rm ${tfile}			# Clean up the test file.

	tet_result PASS
}

#
# Verify deletion of a replicated file works.
#
#	Create file
#	Do replicate
#	Wait for replicate complete
#	Verify replicated data matches.
#	Delete file
#	Do replicate
#	Check that file disappeared
#
repl_verify_delete()
{
	repl_file_size=$1
	tet_infoline "Verify that deletion of a replicated file works."
	#
	# Init for replication, get the target host.  If this fails, we just
	# abort the test and return.  The repl_init function already did any
	# tet_infoline stuff.
	#
	targ=`repl_init ${ZUMA_TEST_VOL}`
	if [ $? -ne 0 -o "${targ}" = "" ]; then
		tet_result UNRESOLVED
		return
	fi
	#
	# Create a file on the mounted filesystem on the local (source)
	# host.  Start a replicate, wait for it to finish, then find the
	# file on the target and diff it with the local file.
	#
	tfile=`create_file /var/run/zumastor/mount/${ZUMA_TEST_VOL} ${repl_file_size}`
	replicate_and_wait ${ZUMA_TEST_VOL} ${targ}
	if [ $? -ne 0 ]; then
		tet_result UNRESOLVED
		return
	fi
	#
	# Compare the file on the local volume with the same file on the
	# target.
	#
	ssh -o StrictHostKeyChecking=no root@${targ} "cat ${tfile}" | diff -q ${tfile} -
	if [ $? -ne 0 ]; then
		tet_infoline "Replicated file ${tfile} differs!"
		tet_result FAIL
		return
	fi
	rm ${tfile}			# Delete the test file.
	#
	# Do another replicate and wait for it to complete.
	#
	replicate_and_wait ${ZUMA_TEST_VOL} ${targ}
	if [ $? -ne 0 ]; then
		tet_result UNRESOLVED
		return
	fi
	#
	# If the file disappeared, it worked properly.
	#
	ssh -o StrictHostKeyChecking=no root@${targ} "test -f ${tfile}"
	if [ $? -eq 0 ]; then
		tet_infoline "Deleted replicated file ${tfile} still exists!"
		tet_result FAIL
		return
	fi

	tet_result PASS
}

#
# These are the actual tests.  They set the appropriate parameter(s) and
# invoke the function that does the work.
#
# 10 megabyte file.
small_repl1()
{
	repl_verify_data 10240
}

small_repl2()
{
	repl_verify_delete 10240
}

# 100 megabyte file.
medium_repl1()
{
	repl_verify_data 102400
}

medium_repl2()
{
	repl_verify_delete 102400
}

# 1 gigabyte file.
large_repl1()
{
	repl_verify_data 1048576
}

large_repl2()
{
	repl_verify_delete 1048576
}

startup()
{
	#
	# Set up ssh if necessary.
	#
	mkdir -p /root/.ssh
	if [ ! -f /root/.ssh/id_rsa.pub ]; then
		ssh-keygen -t rsa -f /root/.ssh/id_rsa -N "" -C $USER@`hostname`
	fi
	#
	# Create and build our file-maker.
	#
	cat <<EOF_mkfile.c >mkfile$$.c
main(argc,argv)
int argc;
char *argv[];
{
	int num, i;
	static char buffer[131072];

	num = 0;
	for (i = 0; i < sizeof(buffer); i++)
		buffer[i] = (i % 94) + 33;
	if (argc > 1)
		num = (atoi(argv[1]) * 1024) / sizeof(buffer);
	if (num < 1)
		num = 1;
	for (i = 0; i < num; i++)
		write(1, buffer, sizeof(buffer));
	close(1);
}
EOF_mkfile.c
	cc -O mkfile$$.c -o mkfile$$
}

cleanup() # clean-up function
{
	rm -f mkfile$$.c mkfile$$
}

# execute shell test case manager - must be last line
. $TET_ROOT/lib/xpg3sh/tcm.sh
