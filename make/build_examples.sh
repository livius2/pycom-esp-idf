#!/bin/bash
#
# Build all examples from the examples directory, out of tree to
# ensure they can run when copied to a new directory.
#
# Runs as part of CI process.
#
# Assumes CWD is an out-of-tree build directory, and will copy examples to individual subdirectories, one by one.
#
[ -z ${IDF_PATH} ] && echo "IDF_PATH is not set" && exit 1

EXAMPLE_NUM=1
RESULT=0

RESULT_WARNINGS=22  # magic number result code for "warnings found"

set -e

for example in ${IDF_PATH}/examples/*; do
    [ -f ${example}/Makefile ] || continue
    echo "Building ${example} as ${EXAMPLE_NUM}..."
    mkdir -p example_builds/${EXAMPLE_NUM}
    cp -r ${example} example_builds/${EXAMPLE_NUM}
    pushd example_builds/${EXAMPLE_NUM}/`basename ${example}`

   # be stricter in the CI build than the default IDF settings
   export EXTRA_CFLAGS="-Werror -Werror=deprecated-declarations"
   export EXTRA_CXXFLAGS=${EXTRA_CFLAGS}

   # build non-verbose first
   BUILDLOG=$(mktemp -t examplebuild.XXXX.log)
   (
       set -e
       make clean defconfig
       make all 2>&1 | tee $BUILDLOG
    ) || (RESULT=$?; make V=1) # only build verbose if there's an error
    popd
    EXAMPLE_NUM=$(( $EXAMPLE_NUM + 1 ))

    if [ $RESULT -eq 0 ] && grep -q ": warning:" $BUILDLOG; then
        echo "Build will fail, due to warnings in this example"
        RESULT=$RESULT_WARNINGS
    fi

    rm -f $BUILDLOG
done

if [ $RESULT -eq $RESULT_WARNINGS ]; then
    echo "Build would have passed, except for warnings."
fi

exit $RESULT

