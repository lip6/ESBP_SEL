Implementation of Symmetric Explanation Learning (SEL) in Glucose 4.0
=====================================================================

Directory overview:
-------------------

`core/` A core version of the solver glucose (no main here)  
`experiments/` An extended solver with simplification capabilities  
`mtl/` MiniSat Template Library  
`parallel/` A multicore version of glucose **! Not compatible with SEL ! **  
`simp/` An extended solver with simplification capabilities  
`testfiles/` Some test cnfs with a corresponding symmetry file  
`utils/` MiniSat util files  
`README`  
`LICENCE`  
`Changelog`  

To build (release version: without assertions, statically linked, etc):
-----------------------------------------------------------------------

`cd simp`  
`make rs`  
(Like MiniSat...)  

Usage:
------

info: `./glucose --help`  
run: `./glucose testfiles/holes/hole002.cnf`  
run with symmetry: `./glucose testfiles/holes/hole002.cnf` (Glucose automatically  searches for the file `testfiles/holes/hole002.cnf.sym`  

Branches:
---------

`master`: master branch with implementation of SEL  
`sym-learning-scheme`: branch with implementation of Symmetrical Learning Scheme (SLS)  
`full-intch-prop`: experimental branch  
`strongMinimization`: experimental branch  
