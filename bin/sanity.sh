#!/bin/bash


cd src
list=$(find testing -name "*.yut")

for yut_file in ${list}; do
	./yamada --test ${yut_file} || exit 1
done
cd -
