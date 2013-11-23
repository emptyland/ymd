Yamada Script Language
======================

[![Build Status](https://travis-ci.org/emptyland/ymd.png)](https://travis-ci.org/emptyland/ymd])
[![Build Status](https://drone.io/github.com/emptyland/ymd/status.png)](https://drone.io/github.com/emptyland/ymd/latest)

Yamada Script is a stupid and simple script lanage.

Build
-----

	$ git clone --branch=dev --depth=100 --quiet git://github.com/emptyland/ymd.git ymd
	$ cd ymd
	$ scons -Q

if you don't have clang:

	$ export CC=gcc
	$ scons -Q

Test
----

	$ cd ymd
	$ src/all_test

