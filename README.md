
# About

Lama utility for processing Lama bytecode files. The utility has 2 modes: iterative interpreter and idiom analyzer.

# Build

To build the utility run the following command:

```bash
make
```

You can specify some compiler defines to change interpreter behaviour.
The following defines are available:

Name                      |                Description                             
:-------------------------|:----------------------------------------------------------
LAMA_OP_STACK_CAPACITY    | Defines capacity of operand stack of Lama interpreter     
LAMA_CALL_STACK_CAPACITY  | Defines capacity of callstack of Lama interpreter         
INTERPRETER_DEBUG         | Allows or prohibits debug information of Lama interpreter

Some Lama source files may require more operand stack or callstack capacity.


# Usage

The utility has two modes:
- interpreter mode (default mode): iteratively interprets given bytecode file
- idioms analyzer (enables with `-i` option): finds idioms and counts its occurrences

An idiom is a sequence of one or two consecutive instructions in the given bytecode file.

```bash
lama-util [-i] <input>
```

# Tests

Test files are placed in deps/Lama/tests folder. To run tests manually execute the following command:
```bash
bash run-tests.sh
```
or
```bash
./run_tests.sh
```

## Regression tests

Results of regression tests are shown below. Source codes for regression tests can be found in `deps/Lama/tests/regression` folder.

<details>
  <summary>Regression tests results</summary>

  ```
  test001.lama: passed

test002.lama: passed

test003.lama: passed

test004.lama: passed

test005.lama: passed

test006.lama: passed

test007.lama: passed

test008.lama: passed

test009.lama: passed

test010.lama: passed

test011.lama: passed

test012.lama: passed

test013.lama: passed

test014.lama: passed

test015.lama: passed

test016.lama: passed

test017.lama: passed

test018.lama: passed

test019.lama: passed

test020.lama: passed

test021.lama: passed

test022.lama: passed

test023.lama: passed

test024.lama: passed

test025.lama: passed

test026.lama: passed

test027.lama: passed

test028.lama: passed

test029.lama: passed

test034.lama: passed

test036.lama: passed

test040.lama: passed

test041.lama: passed

test042.lama: passed

test045.lama: passed

test046.lama: passed

test050.lama: passed

test054.lama: compalation failed

test059.lama: passed

test063.lama: passed

test072.lama: passed

test073.lama: passed

test074.lama: passed

test077.lama: passed

test078.lama: passed

test079.lama: passed

test080.lama: passed

test081.lama: passed

test082.lama: passed

test083.lama: passed

test084.lama: passed

test085.lama: passed

test086.lama: passed

test088.lama: passed

test089.lama: passed

test090.lama: passed

test091.lama: passed

test092.lama: passed

test093.lama: passed

test094.lama: passed

test095.lama: passed

test096.lama: passed

test097.lama: passed

test098.lama: passed

test099.lama: passed

test100.lama: passed

test101.lama: passed

test102.lama: passed

test103.lama: passed

test104.lama: passed

test105.lama: passed

test106.lama: passed

test107.lama: passed

test110.lama: compalation failed

test111.lama: compalation failed

test112.lama: passed

test801.lama: passed

test802.lama: passed

test803.lama: failed
expected output:
Fatal error: exception Failure("int value expected (Closure ([\"unit\"], <not supported>, <not supported>))\n")

actual output:
1
2
3
4
5
6
*** FAILURE: internal error (file: deps/Lama/tests/regression/test803.bc, code offset: 469): expected an integer
  ```

  Some tests cannot be compiled due to bytecode compiler limitations.

  **Notice**: The last one is failed but actually iterative interpreter found the same error as recursive Lama interpreter.
</details>

## Performance

Results of running `deps/Lama/tests/performance/Sort.lama` on different interpreters are shown in the table below:

Interpreter                               | Time   
:-----------------------------------------|:--------
Source-level Lama recursive interpreter   | 6m 30s  
Bytecode-level Lama recursive interpreter | 1m 56s
Lama iterative interpreter                | 3m 05s
