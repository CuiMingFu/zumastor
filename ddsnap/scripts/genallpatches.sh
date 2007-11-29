#!/bin/bash

pushd patches >/dev/null
vers=$(echo *)
popd >/dev/null
for ver in $vers
do
	./scripts/genpatch.sh $ver drivers/md > patches/$ver/00_dm-ddsnap.AUTO
done
