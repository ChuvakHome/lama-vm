#!/usr/bin/env bash

cd $(dirname $0)

EXECUTABLE_NAME=lama-util
ITER_INTERPRETER="$PWD/$EXECUTABLE_NAME"

LAMA_HOME=$PWD/deps/Lama
RUNTIME_HOME=$LAMA_HOME/runtime
REGRESSION_TEST_DIR=$LAMA_HOME/tests/regression
PERFORMANCE_TEST_DIR=$LAMA_HOME/tests/performance

LAMACC=lamac

function compile_file() {
    $LAMACC -I $RUNTIME_HOME -b $1

    echo $?
}

function run_with_lama_rec_interpreter() {
    $LAMACC -I $RUNTIME_HOME -i $1 <$2
}

function run_single_regression_test() {
    testfile=$1
    testfile_input=${testfile/.lama/.input}

    # run Lama interpreter, get expected output
    expected_output=${testfile/.lama/.out0}
    run_with_lama_rec_interpreter $testfile $testfile_input >$expected_output 2>&1

    # run iterative interpreter
    compile_res=$(compile_file $testfile 2>/dev/null)

    if [ "$compile_res" -ne 0 ]; then
        echo -1
        return
    fi

    bytecode_file=${testfile/.lama/.bc}
    interpreter_output=${testfile/.lama/.out1}
    $ITER_INTERPRETER $bytecode_file <$testfile_input >$interpreter_output 2>&1

    cmp $interpreter_output $expected_output 1>/dev/null 2>/dev/null

    echo $?
}

function for_each_regression_testfile() {
    for testfile in $REGRESSION_TEST_DIR/*.lama; do
        test_simple_name=$(basename $testfile)
        test_result=$($1 $testfile)

        test_status="passed"

        if [ "$test_result" -lt 0 ]; then
            test_status="compalation failed"
        elif [ "$test_result" -gt 0 ]; then
            test_status="failed"
        fi

        echo -e "$test_simple_name: $test_status"

        expected_output=${testfile/.lama/.out0}
        actual_output=${testfile/.lama/.out1}

        if [ "$test_result" -gt 0 ]; then
            echo "expected output:"
            cat $expected_output
            echo -e "\nactual output:"
            cat $actual_output
        fi

        echo
    done
}

function run_regression_tests() {
    cd $REGRESSION_TEST_DIR

    for_each_regression_testfile run_single_regression_test
}

run_regression_tests
